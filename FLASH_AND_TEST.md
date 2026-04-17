# runtime-freq-v2 — Flash & Test (morning procedure)

Quick reference for the first on-device run. Everything below assumes you've just
come back to this worktree (`runtime-freq-v2`) and the firmware binaries are
already built under `src/.pio/build/`.

## What's in this build

- **Bind phrase:** `ttblue` (from main checkout's `src/user_defines.txt`)
- **Regulatory domains:** `FCC_915` + `ISM_2400` (compile-time rendezvous)
- **Debug flag:** `-DRUNTIME_FREQ_DEBUG=1` — prints state transitions prefixed `[FREQ]` on the serial log
- **DEBUG_LOG:** enabled, so `DBGLN` output flows over UART
- **Targets built:**
  - Nomad TX → `src/.pio/build/Unified_ESP32_LR1121_TX_via_WIFI/firmware.bin`
  - DBR4 RX → `src/.pio/build/Unified_ESP32_LR1121_RX_via_WIFI/firmware.bin`

## Flashing

Both are ESP32 WiFi-upload builds. After power-on, AP `ExpressLRS TX` / `ExpressLRS RX` comes up within ~60 s (`AUTO_WIFI_ON_INTERVAL=60`). Flash via `http://10.0.0.1` on each.

If LittleFS on either radio is wiped (fresh flash), upload the hardware layout JSON via the `/options` page:

- Nomad TX: `src/hardware/TX/Radiomaster Nomad.json`
- DBR4 RX: `src/hardware/RX/Generic LR1121 True Diversity.json`

## What to test

### Step 1 — confirm steady-state link (no regression)

1. Power both up. They should auto-bind (same bind phrase, persisted UID).
2. Normal RC control works, telemetry flows. Lua script on the radio connects.
3. Open **Freq Config** folder in Lua. Status line shows: `RDV: FCC915`.

If any of the above fails, you've broken something in the steady-state path — do NOT proceed, report the symptom.

### Step 2 — first staged swap

1. In Lua → **Freq Config → Preset** → pick `EU868`.
2. Hit **Apply**. Status in Lua should flash `Staging...`.
3. Within ~1–3 s: Lua status line advances: `STGD: FCC915` → `SWTCH: EU868` → `ACTIVE: EU868`.
4. Link stays up throughout — no dropouts, no rebind required. This is the direct reproduction of what v1 broke on the Nomad.

### Step 3 — fallback watchdog (intentional failure)

1. While `ACTIVE: EU868`, **physically interfere** with the 868 band (walk away, shield the RX, whatever).
2. Within 1.5 s of the staged window TX should abort & revert. Status transitions to `FALLBK`, then auto-recovers to `RDV: FCC915` on the next stage attempt.

Actually — for Step 3 the cleaner test is to **induce a STAGE with no RX reachable**:
1. Power off the RX entirely.
2. TX Lua → Preset: `EU868` → Apply.
3. Status should go `STGD` → (no ACK arrives) → silently abort at epoch (no swap). Lua stays on `RDV: FCC915`. Link was never alive on RX anyway, but the key assertion is TX didn't flip alone.

### Step 4 — Revert

1. After a successful ACTIVE swap, hit **Revert**.
2. Both sides drop back to rendezvous within one watchdog cycle. Status: `FALLBK: FCC915`.

## Getting debug logs

The UART debug path on the DBR4 is what you said you haven't fully cracked before. Rough procedure:

- DBR4 has a UART exposed for flashing (TX/RX/GND pins). Connect your USB-UART adapter:
  - FTDI `RX` → DBR4 `TX`
  - FTDI `TX` → DBR4 `RX`
  - FTDI `GND` → DBR4 `GND`
  - Do **not** connect 3.3V — DBR4 is self-powered off its own battery/supply.
- Open a serial monitor at **420000 baud** (standard ELRS debug baud — not 115200).
- Look for lines prefixed `[FREQ]`. You should see entries like:
  - `[FREQ] stage name=EU868 epoch=2500 requireAck=0` (RX, when it receives STAGE)
  - `[FREQ] swap active<-staged name=EU868 at nonce=2500`
  - `[FREQ] switching -> ACTIVE (first valid packet)`
  - `[FREQ] ack-gate blocked swap at epoch=2500 (no ack)` (TX, if RX never replied)
  - `[FREQ] watchdog fired after 1500ms -> revert`

For the Nomad TX, the radio's existing module UART output comes through its CRSF serial. You can pull debug logs from the TX side by enabling USB connection to the Nomad module itself, or via the EdgeTX serial bridge.

## Rolling back

If anything goes sideways on hardware:

1. `git checkout master` (the upstream branch) on this worktree would get you back to unmodified ELRS, but it's a different worktree.
2. Simpler: in the main checkout, `git checkout master` and re-flash from there. Your bind phrase stays the same, so already-bound pairs will reconnect.

## Known non-blockers (not bugs)

- **`CalibImage` isn't re-run on swap.** RX sensitivity may be ~1 dB worse when hopping well outside the compile-time domain (e.g. FCC915 → EU433). Not a functional problem, acknowledged for a follow-up PR.
- **Dual-band swap not implemented.** PR 5 only switches the sub-GHz primary band; the 2.4 GHz secondary stays at its compile-time config. PR 7 will add that.
- **TX web UI for custom presets not implemented.** Only the 8 built-in regulatory domains are in the preset list. PR 6 adds custom ranges via `custom_preset.json` sideload.
