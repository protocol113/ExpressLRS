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

**⚠️ IMPORTANT:** the `firmware.bin` files in `firmware_images/` were built with `MY_BINDING_PHRASE="ttblue"` already baked in at compile time, so the pair will auto-rebind to your existing RX. If you're flashing these raw via `esptool` or a fresh WiFi upload without the Configurator UI, you may need to re-enter your bind phrase on the `/update` page before confirming — the web flasher has a field for it. If your existing RX was already bound with `ttblue`, they'll reconnect without a rebind.

If LittleFS on either radio is wiped (fresh flash), upload the hardware layout JSON via the `/options` page:

- Nomad TX: `firmware_images/nomad_hardware.json` (copy of `src/hardware/TX/Radiomaster Nomad.json`)
- DBR4 RX: `firmware_images/dbr4_hardware.json` (copy of `src/hardware/RX/Generic LR1121 True Diversity.json`)

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

### Step 3 — ACK-gate (no RX reachable)

This is the direct v1-Nomad-failure reproduction test. If Step 3 passes, the ACK gate is doing its job.

1. After Step 2 succeeded (status = `ACTIVE: EU868`), re-select **Preset: IN866** in Lua.
2. **Power off the RX entirely** before hitting Apply.
3. Hit **Apply** on TX.
4. Expected: status goes `STGD: EU868`, then after a few seconds silently returns to its prior state (`ACTIVE: EU868` or `RDV: FCC915` depending on what it was on). **TX must NOT flip to IN866 alone.**
5. Repower the RX. After re-link, Lua status should show the pre-Step-3 active config.

### Step 4 — Re-Apply different preset

The Revert button is intentionally not in this build (see "Known non-blockers"). To undo a stage, just Apply a different preset — the swap back goes through the same ACK-gated path as any other Apply.

1. From `ACTIVE: EU868`, pick **Preset: FCC915** (the original rendezvous).
2. Apply. Status cycles: `STGD` → `SWTCH` → `ACTIVE: FCC915`.
3. You're back on the starting domain.

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
