#!/bin/bash
# Build the firmware images the flasher page offers, and merge each into one .bin
# that flashes at 0x0 (bootloader + partition table + app).
#
# Needs arduino-cli and both ESP32 cores:
#   esp32-bluepad32:esp32 4.1.0 - every Bluetooth build
#   esp32:esp32 2.0.17          - the touch-only build, which has no Bluetooth at all
#     (main.cpp picks HAVE_BT from whether <Bluepad32.h> exists, so the touch build is
#      the same sources compiled against the core that does not have it)
#
# Override the tool paths if yours are elsewhere:
#   ACLI=/path/to/arduino-cli ESPTOOL=/path/to/esptool.py tools/build_all.sh
# EXTRA_PROPS is passed to every compile, for anything your machine needs on top.
#
# esptool is run as a module, so pyserial has to be importable - point PYTHONPATH at a
# pyserial checkout if it is not installed. arduino-cli also wants ctags, which it will
# ask for as {runtime.tools.ctags.path} if its bundled copy is missing; any executable
# that exits cleanly will do, since the sketch is already valid C++:
#   PYTHONPATH=/path/to/pyserial ACLI=/path/to/arduino-cli \
#   EXTRA_PROPS="--build-property runtime.tools.ctags.path=/path/to/ctags-dir" \
#     tools/build_all.sh
#
# With no arguments it builds the boards someone is actually using - the ones below in
# ACTIVE. Every other image stays as it was: rebuilding a board nobody has in their
# hands only churns binaries nobody can test.
#   tools/build_all.sh              the active boards
#   tools/build_all.sh all          every board, for a release that changes all of them
#   tools/build_all.sh BT touch     just these
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-${TMPDIR:-/tmp}/han-build}
ACLI=${ACLI:-arduino-cli}
ESPTOOL=${ESPTOOL:-$HOME/.arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool.py}
OPTS=FlashMode=qio,FlashFreq=80,CPUFreq=240,DebugLevel=none
BT_FQBN=esp32-bluepad32:esp32:esp32:$OPTS
PLAIN_FQBN=esp32:esp32:esp32:$OPTS
BT_MAX=1179648      # app0 in partitions.csv:       0x120000
TOUCH_MAX=720896    # app0 in partitions_touch.csv: 0x0B0000

# key                  board flag      ("-" for the plain Bluetooth build)
# The two boards being developed side by side, and nothing else:
#   ESP32-2432S024C  - the known-good board, which needs two images: BT and touch-only
#   CrowPanel-2.4-2.8 - the Elecrow 2.4" Solo Miner (SKU DHM04624D)
# That is three keys for two boards. Add a key when a board arrives and take it out
# again when nobody is testing it, so a build never churns images nobody can check.
ACTIVE="BT touch CrowPanel-2.4-2.8 E32R28T"

BUILDS="
BT                     -
2432S024N              BOARD_024N
2432S024R              BOARD_024R
2432S028C              BOARD_028C
2432S028R              BOARD_028R
2432S028R-Plus         BOARD_028PLUS
E32R28T                BOARD_E32R28T
2432S028-2USB          BOARD_CYD2USB
2432S032C              BOARD_032C
2432S032N              BOARD_032N
2432S032R              BOARD_032R
3248S035C              BOARD_035C
3248S035R              BOARD_035R
JC2432W328C            BOARD_W328C
CrowPanel-2.4-2.8      BOARD_CROW
"

merge() {   # merge <key> <output dir>
  if [ ! -f "$2/gbc_cyd.ino.bin" ]; then
    echo "   FAILED: $1 produced no app image - the .bin on disk is older than these sources" >&2
    echo "$1" >> "$WORK/failed"   # a file, because the build loop runs in a subshell
    return 0
  fi
  PYTHONPATH="${PYTHONPATH}" python3 "$ESPTOOL" --chip esp32 merge_bin \
    -o "$ROOT/docs/firmware/HAN-CYD-GBC-$1.bin" --flash_mode keep --flash_freq keep --flash_size keep \
    0x1000 "$2/gbc_cyd.ino.bootloader.bin" 0x8000 "$2/gbc_cyd.ino.partitions.bin" 0x10000 "$2/gbc_cyd.ino.bin" > /dev/null
}

wanted() {
  if [ $# -eq 0 ]; then
    for w in $ACTIVE; do [ "$w" = "$KEY" ] && return 0; done
    return 1
  fi
  [ "$1" = "all" ] && return 0
  for w in "$@"; do [ "$w" = "$KEY" ] && return 0; done
  return 1
}

# Fail loudly and early if the toolchain is not where this script expects. A missing
# tool used to vanish into the grep above, leaving "== board" on the log, a stale .bin on
# disk and an exit status of zero - which is how 2.19.27 came to be committed twice, once
# with the previous version's sources.
command -v "$ACLI" >/dev/null 2>&1 || { echo "arduino-cli not found at: $ACLI (set ACLI=...)" >&2; exit 1; }
[ -f "$ESPTOOL" ] || { echo "esptool.py not found at: $ESPTOOL (set ESPTOOL=...)" >&2; exit 1; }
python3 -c "import serial" 2>/dev/null || { echo "esptool needs pyserial: set PYTHONPATH to a pyserial checkout" >&2; exit 1; }

mkdir -p "$WORK"
rm -f "$WORK/failed"
echo "$BUILDS" | while read -r KEY FLAG; do
  [ -z "$KEY" ] && continue
  wanted "$@" || continue
  echo "== $KEY"
  EXTRA=""
  [ "$FLAG" != "-" ] && EXTRA="--build-property compiler.cpp.extra_flags=-D$FLAG"
  "$ACLI" compile --fqbn "$BT_FQBN" --build-property upload.maximum_size=$BT_MAX $EXTRA_PROPS \
    $EXTRA --build-path "$WORK/b_$KEY" --output-dir "$WORK/o_$KEY" "$ROOT" 2>&1 \
    | grep -E "Sketch uses|error:|Error during" | grep -v initializing || true
  merge "$KEY" "$WORK/o_$KEY"
done

# The touch-only build needs a sketch tree without Bluepad32 and the other partition
# table, so it is built from a copy rather than from the repo in place.
KEY=touch
if wanted "$@"; then
  echo "== touch"
  rm -rf "$WORK/sk_touch"
  mkdir -p "$WORK/sk_touch"
  (cd "$ROOT/.." && cp -a "$(basename "$ROOT")" "$WORK/sk_touch/gbc_cyd")
  rm -rf "$WORK/sk_touch/gbc_cyd/.git"
  cp "$ROOT/partitions_touch.csv" "$WORK/sk_touch/gbc_cyd/partitions.csv"
  "$ACLI" compile --fqbn "$PLAIN_FQBN" --build-property upload.maximum_size=$TOUCH_MAX $EXTRA_PROPS \
    --build-path "$WORK/b_touch" --output-dir "$WORK/o_touch" "$WORK/sk_touch/gbc_cyd" 2>&1 \
    | grep -E "Sketch uses|error:|Error during" | grep -v initializing || true
  merge touch "$WORK/o_touch"
fi

if [ -s "$WORK/failed" ]; then
  echo "FAILED: $(tr '\n' ' ' < "$WORK/failed")- the .bin files for these are stale, do not release" >&2
  exit 1
fi
echo "done - now run: python3 tools/release_check.py"
