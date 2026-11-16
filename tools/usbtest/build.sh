#!/bin/sh
# Builds the host harness from the transfer code in src/main.cpp.
set -e
cd "$(dirname "$0")"
python3 - <<'PY'
src = open('../../src/main.cpp').read()
a = src.index('#define XFER_CHUNK 4096')
b = src.index('// The transfer screen.')
open('xfer.inc', 'w').write(src[a:b])
PY
cat > harness.cpp <<'CPP'
#include "stubs.h"
#include "xfer.inc"
int main() {
  while (true) {
    if (const char *c = xferPollLine()) { if (!xferCommand(c)) break; }
    else usleep(500);
  }
}
CPP
g++ -O1 -o harness harness.cpp
