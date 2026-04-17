#!/usr/bin/env bash
# Flash a runtime-freq-v2 firmware .bin to a radio running an ExpressLRS
# WiFi AP (http://10.0.0.1). Usage:
#
#   ./tools/flash_wifi.sh tx   # flashes nomad_tx_firmware.bin
#   ./tools/flash_wifi.sh rx   # flashes dbr4_rx_firmware.bin
#   ./tools/flash_wifi.sh /path/to/any/firmware.bin
#
# Pre-req: your Mac must be connected to the "ExpressLRS TX" or
# "ExpressLRS RX" WiFi network (password: expresslrs) BEFORE running this.
# Enable the TX AP via Lua → ExpressLRS → WiFi Connectivity → Enable WiFi.
# Enable the RX AP by power-cycling the RX 3x or waiting 60s (AUTO_WIFI_ON_INTERVAL).
#
# Exit codes: 0 = upload succeeded and device rebooting; non-zero = failure.

set -uo pipefail

AP_IP="${AP_IP:-10.0.0.1}"
TIMEOUT_PING=5
TIMEOUT_UPLOAD=120

root="$(cd "$(dirname "$0")/.." && pwd)"

case "${1:-}" in
    tx)  fw="$root/firmware_images/nomad_tx_firmware.bin"; target="Nomad TX" ;;
    rx)  fw="$root/firmware_images/dbr4_rx_firmware.bin";  target="DBR4 RX" ;;
    "")  echo "usage: $0 {tx|rx|<path-to-firmware.bin>}" >&2; exit 2 ;;
    *)   fw="$1"; target="custom ($1)" ;;
esac

if [[ ! -f "$fw" ]]; then
    echo "error: firmware file not found: $fw" >&2
    exit 2
fi

echo "[flash] target: $target"
echo "[flash] file:   $fw ($(wc -c < "$fw" | awk '{printf "%.2f KB", $1/1024}'))"
echo "[flash] sha256: $(shasum -a 256 "$fw" | awk '{print $1}')"

# Verify we're talking to the ELRS AP
echo "[flash] probing http://$AP_IP/ ..."
if ! curl -s --max-time "$TIMEOUT_PING" -o /dev/null -w "%{http_code}" "http://$AP_IP/" | grep -qE "200|302"; then
    echo "error: http://$AP_IP/ is not responding." >&2
    echo "   Make sure your Mac's WiFi is connected to the ExpressLRS TX/RX AP." >&2
    echo "   (System Settings → WiFi → ExpressLRS TX or ExpressLRS RX, password: expresslrs)" >&2
    exit 1
fi

echo "[flash] AP reachable. Target identification:"
curl -s --max-time "$TIMEOUT_PING" "http://$AP_IP/target" || true
echo ""

echo "[flash] posting firmware to $AP_IP/update ..."
# ELRS /update expects multipart/form-data, field name "upload"
resp="$(curl --max-time "$TIMEOUT_UPLOAD" -s -w "\nHTTP_CODE:%{http_code}\n" \
    -F "upload=@$fw" "http://$AP_IP/update")"
echo "$resp"

http_code="$(printf '%s\n' "$resp" | awk -F: '/^HTTP_CODE:/ {print $2}')"
if [[ "$http_code" == "200" ]]; then
    echo "[flash] ✓ upload accepted. Device will reboot with new firmware."
    echo "[flash] give it 5–10 seconds, then you'll need to re-join your normal WiFi."
    exit 0
else
    echo "[flash] ✗ upload failed with HTTP $http_code" >&2
    exit 1
fi
