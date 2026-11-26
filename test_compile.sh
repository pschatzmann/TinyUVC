#!/usr/bin/env bash
#
# test_compile.sh -- compiles every example for every supported platform
# with arduino-cli.
#
# Usage:
#   ./test_compile.sh                         # everything
#   ./test_compile.sh --board rp2040          # one platform only
#   ./test_compile.sh --example TestPatternWebcam
#   ./test_compile.sh --list                  # show boards and examples
#   ./test_compile.sh -v                      # show compiler output of failures
#
# Requires arduino-cli with the cores esp32:esp32, rp2040:rp2040 and
# STMicroelectronics:stm32, and the libraries TinyCamera, arduino-audio-tools,
# TinyJPEG, TinyGPU and Adafruit TinyUSB Library (STM32).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLES_DIR="$SCRIPT_DIR/examples"
COMPILE_TIMEOUT=600

# key|fqbn
BOARDS=(
  "esp32s3|esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=opi"
  "rp2040|rp2040:rp2040:rpipico:usbstack=tinyusb"
  "rp2350|rp2040:rp2040:rpipico2:usbstack=tinyusb"
  "rp2350psram|rp2040:rp2040:adafruit_feather_rp2350_hstx:usbstack=tinyusb"
  "rp2040rtos|rp2040:rp2040:rpipico:usbstack=tinyusb,os=freertos"
  "rp2350rtos|rp2040:rp2040:rpipico2:usbstack=tinyusb,os=freertos"
  "stm32f4|STMicroelectronics:stm32:GenF4:pnum=BLACKPILL_F411CE,usb=none"
  "stm32h7|STMicroelectronics:stm32:GenH7:pnum=WeActMiniH750VBTX,usb=none"
)

# rp2350psram: a RP2350 board with PSRAM (RP2350_PSRAM_CS), for UVCMemory.
# board specific examples. The video classes of arduino-audio-tools need
# FreeRTOS (RP2040: Tools > Operating System > FreeRTOS SMP); the rtos rows
# only build those examples.
declare -A ONLY
ONLY["ESP32CameraWebcam"]="esp32s3"
ONLY["RP2040CameraWebcam"]="rp2040 rp2350"
ONLY["STM32CameraWebcam"]="stm32h7"
ONLY["TestPatternWebcam"]="esp32s3 rp2040 rp2350 rp2350psram stm32f4 stm32h7"
ONLY["TinyGPUWireframeWebcam"]="esp32s3 rp2040 rp2350 rp2350psram"
ONLY["AudioToolsJpegWebcam"]="esp32s3 rp2040rtos rp2350rtos"
ONLY["AudioToolsAviWebcam"]="esp32s3 rp2040rtos rp2350rtos"

ONLY_BOARD=""
ONLY_EXAMPLE=""
VERBOSE=0
LIST=0
while [ $# -gt 0 ]; do
  case "$1" in
    --board) ONLY_BOARD="$2"; shift 2 ;;
    --example) ONLY_EXAMPLE="$2"; shift 2 ;;
    -v|--verbose) VERBOSE=1; shift ;;
    --list) LIST=1; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

command -v arduino-cli >/dev/null || { echo "arduino-cli not found" >&2; exit 2; }

EXAMPLES=()
while IFS= read -r -d '' ino; do
  EXAMPLES+=("$(basename "$(dirname "$ino")")")
done < <(find "$EXAMPLES_DIR" -mindepth 2 -maxdepth 2 -name "*.ino" -print0 | sort -z)

if [ "$LIST" -eq 1 ]; then
  printf 'Boards:\n'; for row in "${BOARDS[@]}"; do printf '  %s\n' "${row/|/ -> }"; done
  printf 'Examples:\n'; printf '  %s\n' "${EXAMPLES[@]}"
  exit 0
fi

PASS=0; FAIL=0; SKIP=0
FAILED=()
for row in "${BOARDS[@]}"; do
  IFS='|' read -r key fqbn <<<"$row"
  [ -n "$ONLY_BOARD" ] && [ "$key" != "$ONLY_BOARD" ] && continue
  for ex in "${EXAMPLES[@]}"; do
    [ -n "$ONLY_EXAMPLE" ] && [ "$ex" != "$ONLY_EXAMPLE" ] && continue
    printf '%-11s %-24s ' "$key" "$ex"
    if [ -n "${ONLY[$ex]+x}" ] && [[ " ${ONLY[$ex]} " != *" $key "* ]]; then
      echo "SKIP (board specific)"; SKIP=$((SKIP + 1)); continue
    fi
    log="$(mktemp)"
    if timeout "$COMPILE_TIMEOUT" arduino-cli compile --fqbn "$fqbn" \
        "$EXAMPLES_DIR/$ex" >"$log" 2>&1; then
      if grep -q "TinyUVC.*warning:" "$log"; then echo "PASS (TinyUVC warnings)"; else echo "PASS"; fi
      PASS=$((PASS + 1))
    else
      echo "FAIL"; FAIL=$((FAIL + 1)); FAILED+=("$key:$ex")
      [ "$VERBOSE" -eq 1 ] && grep -E "error|Error" "$log" | head -20
    fi
    rm -f "$log"
  done
done

echo
echo "PASS: $PASS   FAIL: $FAIL   SKIP: $SKIP"
if [ "$FAIL" -gt 0 ]; then printf '  failed: %s\n' "${FAILED[@]}"; exit 1; fi
exit 0
