#!/usr/bin/env python3
"""
Live CRSF link-stats monitor for the runtime-freq-v2 hardware test.

Parses the CRSF stream coming out of the Nomad TX module's USB and prints a
live one-line status showing uplink/downlink LQ, RSSI, SNR, RF mode, TX power.
Flags link-loss events (LQ=0) and recovery with timestamps relative to start.

Optional: tees a DBR4 debug-UART stream (RX-side `[FREQ]` debug lines from
our runtime-freq-v2 RUNTIME_FREQ_DEBUG=1 build) so you can see the
protocol-level events alongside the TX-side link-quality trace.

Usage:
    ./link_monitor.py --tx /dev/cu.usbserial-8
    ./link_monitor.py --tx /dev/cu.usbserial-8 --rx /dev/cu.usbserial-0001
    ./link_monitor.py --tx /dev/cu.usbserial-8 --log run.log

Pass/fail interpretation for the runtime-freq-v2 test procedure:

    Step 1 (steady-state)     : LQ ~100, stays 100 through entire step
    Step 2 (first staged swap): LQ stays 100 through swap (or max one-packet dip)
    Step 3 (ACK-gate, RX off) : Link eventually lost (RX gone) — that's fine;
                                the key check is RX log shows ACK-gate blocked
                                if you happen to power it back on mid-test
    Step 4 (re-Apply back)    : LQ stays 100 through swap
"""

import argparse
import os
import select
import serial
import sys
import threading
import time
from collections import deque

# --- CRSF parser ----------------------------------------------------------

CRSF_SYNC_BYTES = (0xC8, 0xEE, 0xEC, 0xEA)  # FC, radio TX, sensor, module
CRSF_FRAMETYPE_LINK_STATISTICS = 0x14

# RF mode indices (ELRS) — not exhaustive; reads for the ones we care about
RF_MODES = {
    0:  "4Hz",   1:  "25Hz",  2:  "50Hz",   3:  "100Hz", 4:  "100Hz Full",
    5:  "150Hz", 6:  "200Hz", 7:  "250Hz",  8:  "333Hz Full", 9:  "500Hz",
    10: "D250",  11: "D500",  12: "F500",   13: "F1000",
    14: "50Hz Full", 15: "100Hz Full", 16: "150Hz Full",
}

TX_POWER_LABELS = ["0mW", "10mW", "25mW", "50mW", "100mW", "250mW", "500mW",
                   "1W", "2W", "250uW", "500uW", "1mW", "1.5mW", "3mW", "5mW",
                   "6mW", "7mW", "8mW", "9mW", "15mW", "20mW", "30mW", "40mW",
                   "45mW", "60mW", "70mW", "150mW", "200mW", "350mW"]  # best-effort

def crc8_dvb_s2(crc: int, a: int) -> int:
    crc ^= a
    for _ in range(8):
        crc = ((crc << 1) ^ 0xD5) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

def crc8_buf(buf: bytes) -> int:
    crc = 0
    for b in buf:
        crc = crc8_dvb_s2(crc, b)
    return crc

class CrsfParser:
    """Byte-stream parser that yields (frame_type, payload) tuples. Also
    collects non-CRSF bytes into an ASCII buffer so interleaved DBGLN text
    from the firmware (which shares the same UART as CRSF telemetry when
    DEBUG_LOG is enabled) can be recovered as lines via drain_text_lines()."""
    def __init__(self):
        self.buf = bytearray()
        self.text_buf = bytearray()

    def _stash_text(self, b: int):
        # Keep printable ASCII + CR/LF; everything else is noise.
        if b == 0x0A or b == 0x0D or (0x20 <= b <= 0x7E):
            self.text_buf.append(b)

    def drain_text_lines(self):
        # Split on LF, return complete lines, keep partial in buffer.
        while b'\n' in self.text_buf:
            line, _, self.text_buf = self.text_buf.partition(b'\n')
            txt = line.rstrip(b'\r').decode('ascii', errors='replace').strip()
            if txt:
                yield txt
        # Cap buffer so a never-LF stream doesn't grow forever.
        if len(self.text_buf) > 4096:
            self.text_buf = self.text_buf[-1024:]

    def feed(self, data: bytes):
        self.buf.extend(data)
        while True:
            # Resync: any non-sync byte might be interleaved debug text.
            while self.buf and self.buf[0] not in CRSF_SYNC_BYTES:
                self._stash_text(self.buf.pop(0))
            if len(self.buf) < 2:
                return
            length = self.buf[1]
            if length < 2 or length > 62:
                self._stash_text(self.buf.pop(0))
                continue
            total = length + 2
            if len(self.buf) < total:
                return
            frame = bytes(self.buf[:total])
            body = frame[2:-1]
            crc_expected = frame[-1]
            if crc8_buf(body) != crc_expected:
                self._stash_text(self.buf.pop(0))
                continue
            ftype = frame[2]
            payload = frame[3:-1]
            del self.buf[:total]
            yield ftype, payload

def to_signed(b: int) -> int:
    return b - 256 if b >= 128 else b

def parse_link_stats(p: bytes):
    """Parse a CRSF_FRAMETYPE_LINK_STATISTICS payload (10 bytes)."""
    if len(p) < 10:
        return None
    return dict(
        up_rssi1 = to_signed(p[0]),
        up_rssi2 = to_signed(p[1]),
        up_lq    = p[2],
        up_snr   = to_signed(p[3]),
        ant      = p[4],
        rf_mode  = p[5],
        tx_power = p[6],
        dn_rssi  = to_signed(p[7]),
        dn_lq    = p[8],
        dn_snr   = to_signed(p[9]),
    )

# --- Printing / event tracking -------------------------------------------

START = time.monotonic()

def ts() -> str:
    return f"{time.monotonic() - START:7.2f}s"

def rf_mode_str(v: int) -> str:
    return RF_MODES.get(v, f"mode{v}")

def tx_pwr_str(v: int) -> str:
    return TX_POWER_LABELS[v] if v < len(TX_POWER_LABELS) else f"pwr{v}"

def format_status(s: dict) -> str:
    return (f"UP LQ={s['up_lq']:3d} RSSI={s['up_rssi1']:4d}/{s['up_rssi2']:4d} "
            f"SNR={s['up_snr']:+3d} | DN LQ={s['dn_lq']:3d} RSSI={s['dn_rssi']:4d} "
            f"SNR={s['dn_snr']:+3d} | {rf_mode_str(s['rf_mode']):<12s} {tx_pwr_str(s['tx_power'])}")

# --- Main ----------------------------------------------------------------

def open_port_safely(port: str, baud: int) -> serial.Serial:
    """Open a serial port without asserting DTR/RTS — on some CP2102 adapters
    (e.g. our Nomad TX), those lines are wired to the ESP32 EN/GPIO0 pins, so
    default pyserial behavior can inadvertently trigger a module reset or
    bootloader entry just by opening the port. Set both low before open()."""
    s = serial.Serial()
    s.port = port
    s.baudrate = baud
    s.timeout = 0.1
    s.dtr = False
    s.rts = False
    s.open()
    # Belt-and-suspenders: re-assert low after open in case the OS flipped them.
    s.dtr = False
    s.rts = False
    return s

def rx_debug_thread(port_glob: str, logf):
    """Tee DBR4 debug UART lines. Resilient to USB re-enumeration — if the
    port disappears (e.g. DBR4 power-cycle, OSError 6), re-discovers the port
    from the glob pattern and retries indefinitely. Accepts a full path or a
    glob prefix (e.g. /dev/cu.usbserial-0001 or /dev/cu.usbserial*)."""
    import glob as _glob
    buf = bytearray()
    while True:
        s = None
        try:
            candidates = _glob.glob(port_glob) if '*' in port_glob else [port_glob]
            candidates = [p for p in candidates if p != getattr(rx_debug_thread, '_taken_tx', None)]
            if not candidates:
                time.sleep(1.0)
                continue
            port = candidates[0]
            s = open_port_safely(port, 420000)
            print(f"[{ts()}] RX: opened {port}")
            while True:
                n = s.in_waiting
                if n:
                    buf.extend(s.read(n))
                while b'\n' in buf:
                    line, _, buf = buf.partition(b'\n')
                    txt = line.rstrip(b'\r').decode('utf-8', errors='replace').strip()
                    if not txt:
                        continue
                    marker = "[RX FREQ]" if '[FREQ]' in txt else "[RX     ]"
                    out = f"[{ts()}] {marker} {txt}"
                    print(out)
                    if logf:
                        logf.write(out + '\n')
                        logf.flush()
                time.sleep(0.02)
        except (OSError, serial.SerialException) as e:
            print(f"[{ts()}] RX: port lost ({e}); reopening...", file=sys.stderr)
            try:
                if s is not None: s.close()
            except Exception:
                pass
            time.sleep(0.5)

def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tx', required=True, help='TX CRSF serial port (e.g. /dev/cu.usbserial-8)')
    ap.add_argument('--rx', default=None, help='RX debug serial port (optional)')
    ap.add_argument('--baud', type=int, default=420000)
    ap.add_argument('--log', default=None, help='Also write every printed line to this file')
    ap.add_argument('--quiet', action='store_true', help='Only print on state changes, not every frame')
    args = ap.parse_args()

    logf = open(args.log, 'w') if args.log else None

    # RX debug tee (optional, in background)
    if args.rx:
        t = threading.Thread(target=rx_debug_thread, args=(args.rx, logf), daemon=True)
        t.start()
        print(f"[{ts()}] Listening RX debug on {args.rx} @ 420000")

    # TX CRSF parse — must NOT toggle DTR/RTS (wired to EN/GPIO0 on Nomad CP2102).
    # Resilient to re-enumeration: accepts a glob (e.g. /dev/cu.usbserial-*)
    # and re-opens whenever the OS drops the device.
    import glob as _glob
    def open_tx():
        candidates = _glob.glob(args.tx) if '*' in args.tx else [args.tx]
        rx_held = getattr(rx_debug_thread, '_taken_rx', None)
        candidates = [p for p in candidates if p != rx_held]
        if not candidates:
            return None, None
        port = candidates[0]
        return open_port_safely(port, args.baud), port

    tx, tx_port = None, None
    while tx is None:
        try:
            tx, tx_port = open_tx()
            if tx is None:
                print(f"[{ts()}] TX: no port matching {args.tx} yet; waiting...", file=sys.stderr)
                time.sleep(1.0)
        except Exception as e:
            print(f"[{ts()}] TX: open {args.tx} failed: {e}; retry in 1s", file=sys.stderr)
            time.sleep(1.0)
    print(f"[{ts()}] Listening TX CRSF on {tx_port} @ {args.baud}")
    print(f"[{ts()}] Press Ctrl-C to stop. Link-state changes flagged with '>>>'")
    print("-" * 100)

    parser = CrsfParser()
    last_up_lq = None
    last_dn_lq = None
    last_rfmode = None
    last_print = 0.0
    link_lost_at = None
    link_recovered_note_at = None

    try:
        while True:
            try:
                n = tx.in_waiting
            except (OSError, serial.SerialException) as e:
                print(f"[{ts()}] TX: port lost ({e}); reopening...", file=sys.stderr)
                try: tx.close()
                except Exception: pass
                tx = None
                while tx is None:
                    try:
                        tx, tx_port = open_tx()
                        if tx is None:
                            time.sleep(0.5)
                    except Exception:
                        time.sleep(0.5)
                print(f"[{ts()}] TX: reopened {tx_port}")
                parser = CrsfParser()  # discard partial frame
                continue
            _n_alias = n
            n = _n_alias
            if n:
                chunk = tx.read(n)
                for ftype, payload in parser.feed(chunk):
                    if ftype != CRSF_FRAMETYPE_LINK_STATISTICS:
                        continue
                    s = parse_link_stats(payload)
                    if s is None:
                        continue

                    # Detect state-change events
                    events = []
                    if last_up_lq is not None:
                        if last_up_lq > 0 and s['up_lq'] == 0:
                            link_lost_at = time.monotonic()
                            events.append(">>> LINK LOST (uplink LQ 0)")
                        elif last_up_lq == 0 and s['up_lq'] > 0:
                            if link_lost_at is not None:
                                dur = time.monotonic() - link_lost_at
                                events.append(f">>> LINK RECOVERED after {dur:.2f}s")
                                link_lost_at = None
                            else:
                                events.append(">>> LINK UP")
                    if last_rfmode is not None and s['rf_mode'] != last_rfmode:
                        events.append(f">>> RF MODE: {rf_mode_str(last_rfmode)} -> {rf_mode_str(s['rf_mode'])}")

                    # Rate-limit the baseline line to 1/sec unless there's an event
                    now = time.monotonic()
                    should_print = bool(events) or (not args.quiet and (now - last_print) >= 1.0)
                    if should_print:
                        line = f"[{ts()}] {format_status(s)}"
                        print(line)
                        if logf:
                            logf.write(line + '\n')
                        for ev in events:
                            evline = f"[{ts()}] {ev}"
                            print(evline)
                            if logf:
                                logf.write(evline + '\n')
                        if logf:
                            logf.flush()
                        last_print = now

                    last_up_lq = s['up_lq']
                    last_dn_lq = s['dn_lq']
                    last_rfmode = s['rf_mode']
                # Drain TX-side interleaved debug text lines (DBGLN output
                # from the firmware sharing this UART with CRSF telemetry).
                for txt in parser.drain_text_lines():
                    marker = "[TX FREQ]" if '[FREQ]' in txt else "[TX     ]"
                    out = f"[{ts()}] {marker} {txt}"
                    print(out)
                    if logf:
                        logf.write(out + '\n')
                        logf.flush()
            else:
                time.sleep(0.02)
    except KeyboardInterrupt:
        print(f"\n[{ts()}] stopped.")
    finally:
        tx.close()
        if logf:
            logf.close()

if __name__ == '__main__':
    main()
