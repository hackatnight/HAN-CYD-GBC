#!/bin/sh
# Runs the firmware's BOOT-button menu control (bootNav in src/main.cpp) against simulated
# taps and holds. Usage: ./build.sh
set -e
cd "$(dirname "$0")"
python3 - <<'PY'
s = open('../../src/main.cpp').read()
a = s.index('// tap = down, hold 0.6-2 s and let go = OK, hold 2 s = back')
b = s.index('static int btNav()', a)
open('bootnav.inc', 'w').write(s[a:b])
PY
g++ -std=c++17 -O1 -o t test.cpp && ./t
rm -f t bootnav.inc
