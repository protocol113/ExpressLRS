#!/usr/bin/env bash
# Flash a runtime-freq-v2 firmware directly over UART using esptool.
# The CP2102 adapter's DTR/RTS lines must be wired to the ESP32's GPIO0/EN
# for auto-bootloader entry. Our Nomad TX adapter is; your DBR4 RX adapter
# may or may not be.
#
# Usage:
#   ./tools/flash_uart.sh tx <port>    # e.g. /dev/cu.usbserial-8
#   ./tools/flash_uart.sh rx <port>    # e.g. /dev/cu.usbserial-0001
#
# Flashes bootloader + partitions + boot_app0 + firmware in one shot at the
# standard ESP32 addresses.

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-}"
port="${2:-}"
mode="${3:-auto}"          # auto (DTR/RTS) or manual (user holds boot button)
baud="${BAUD:-460800}"

case "$target" in
    tx) build="$root/src/.pio/build/Unified_ESP32_LR1121_TX_via_WIFI"
        label="Nomad TX" ;;
    rx) build="$root/src/.pio/build/Unified_ESP32_LR1121_RX_via_WIFI"
        label="DBR4 RX" ;;
    *)  echo "usage: $0 {tx|rx} <port> [auto|manual]" >&2; exit 2 ;;
esac

if [[ -z "$port" ]]; then
    echo "usage: $0 {tx|rx} <port> [auto|manual]" >&2
    exit 2
fi
if [[ "$mode" != "auto" && "$mode" != "manual" ]]; then
    echo "error: mode must be 'auto' (DTR/RTS wired) or 'manual' (user holds boot button)" >&2
    exit 2
fi
if [[ ! -d "$build" ]]; then
    echo "error: build dir not found: $build" >&2
    echo "run 'pio run -e Unified_ESP32_LR1121_${target^^}_via_WIFI' first." >&2
    exit 2
fi
for f in bootloader.bin partitions.bin boot_app0.bin firmware.bin; do
    if [[ ! -f "$build/$f" ]]; then
        echo "error: missing $f in $build" >&2
        exit 2
    fi
done

echo "[flash-uart] target: $label"
echo "[flash-uart] port:   $port @ $baud"
echo "[flash-uart] build:  $build"
echo "[flash-uart] mode:   $mode"
echo "[flash-uart] firmware sha256: $(shasum -a 256 "$build/firmware.bin" | awk '{print $1}')"

if [[ "$mode" == "manual" ]]; then
    echo ""
    echo "===================================================================="
    echo "  MANUAL BOOTLOADER ENTRY — $label"
    echo "===================================================================="
    echo ""
    echo "  1. Cut power to the $label."
    echo "  2. Hold the BOOT button (or short GPIO0 to GND)."
    echo "  3. WHILE HOLDING BOOT, re-apply power."
    echo "  4. Release BOOT ~1 second after power-on."
    echo "  5. Press ENTER here to start the flash."
    echo ""
    read -r -p "  Ready? [ENTER to continue, Ctrl-C to cancel] " _
    before_flag="no-reset"
else
    before_flag="default-reset"
fi

python3 -m esptool \
    --chip esp32 \
    --port "$port" \
    --baud "$baud" \
    --before "$before_flag" \
    --after hard-reset \
    write-flash \
    --flash-mode dio \
    --flash-freq 40m \
    --flash-size detect \
    0x1000 "$build/bootloader.bin" \
    0x8000 "$build/partitions.bin" \
    0xe000 "$build/boot_app0.bin" \
    0x10000 "$build/firmware.bin"

echo "[flash-uart] ✓ done. Device hard-reset back to normal operation."
