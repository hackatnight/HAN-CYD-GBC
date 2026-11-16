#!/bin/sh
# Runs the firmware's touch calibration code (calSolve, rawToPanel, panelToScreen,
# screenToPanel in src/main.cpp) against simulated touch panels. Usage: ./build.sh
set -e
cd "$(dirname "$0")"
python3 - <<'PY'
s = open('../../src/main.cpp').read()
def grab(start, end):
    a = s.index(start); return s[a:s.index(end, a)]
code = grab('// raw controller coords -> "panel" coords', 'static bool readTouchRaw')
code += grab('// Works out the calibration from the raw readings', 'static void calibrateTouch()')
open('touch.inc', 'w').write(code)
PY
for R in 1 0; do g++ -std=c++17 -O1 -DTOUCH_RESISTIVE=$R -o t$R test.cpp && ./t$R; done
echo "3.5\" board (240x320 window on a 320x480 touch layer):"
for R in 1 0; do g++ -std=c++17 -O1 -DBIG=1 -DTOUCH_RESISTIVE=$R -o tb$R test.cpp && ./tb$R; done
