"""Run before every push of a release: fails if any place that carries the version or a
firmware image is out of step with src/main.cpp.

    python3 tools/release_check.py

Checks: the flasher page's header matches src/main.cpp; every board someone is actually
testing is built from that version; every other board's manifest agrees with the image it
points at, whatever version that is; every board on the page has a manifest and every
manifest can be reached from the page; and the demo zip holds the same ROMs as docs/demos.

Only the boards in ACTIVE (tools/build_all.sh) have to be current. The rest are built when
somebody has one in their hands, so they sit at whatever version they were last built
from - that is expected, and reported rather than failed.
"""
import glob, json, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
bad = []
ver = re.search(r'#define APP_VERSION "([\d.]+)"', open('src/main.cpp').read()).group(1)

page = open('docs/index.html').read()
m = re.search(r'Version ([\d.]+\d)', page)
if not m or m.group(1) != ver:
    bad.append('docs/index.html header says %s' % (m.group(1) if m else 'nothing'))

# A board can be reached either from an install button directly or by picking it in
# one of the dropdowns, which point an install button at it.
used = set(re.findall(r'manifest="firmware/(manifest-[^"]+\.json)"', page))
used |= set(re.findall(r'<option value="firmware/(manifest-[^"]+\.json)"', page))
have = {os.path.basename(p) for p in glob.glob('docs/firmware/manifest-*.json')}
for x in sorted(used - have):
    bad.append('page offers %s, which does not exist' % x)
for x in sorted(have - used):
    bad.append('%s cannot be reached from the page' % x)

active = set(re.search(r'^ACTIVE="([^"]*)"', open('tools/build_all.sh').read(), re.M).group(1).split())

images = set()
behind = []
for p in sorted(glob.glob('docs/firmware/manifest-*.json')):
    key = re.match(r'manifest-(.*)\.json', os.path.basename(p)).group(1)
    j = json.load(open(p))
    want = j['version']
    if key in active and want != ver:
        bad.append('%s is being tested but its manifest says %s, not %s' % (key, want, ver))
    elif want != ver:
        behind.append('%s is on %s' % (key, want))
    for b in j['builds']:
        for part in b['parts']:
            f = os.path.join('docs/firmware', part['path'])
            images.add(f)
            if not os.path.exists(f):
                bad.append('%s points at missing %s' % (p, f))
            elif want.encode() not in open(f, 'rb').read():
                # the image must be the one its own manifest promises, current or not
                bad.append('%s was not built from %s, which %s offers' % (f, want, os.path.basename(p)))
for f in sorted(glob.glob('docs/firmware/*.bin')):
    if f not in images:
        bad.append('%s is not used by any manifest' % f)

z = zipfile.ZipFile('docs/demos/HAN-DEMO-ROMS.zip')
for n in ('HAN-DEMO.gb', 'HAN-DEMO.gbc', 'HAN-DEMO.nes'):
    if z.read(n) != open('docs/demos/' + n, 'rb').read():
        bad.append('the demo zip has a different %s than docs/demos' % n)

if bad:
    print('RELEASE CHECK FAILED for %s:' % ver)
    for b in bad:
        print('  - ' + b)
    sys.exit(1)
print('Release check OK: %s, %d images, %d manifests' % (ver, len(images), len(have)))
if behind:
    print('  not rebuilt (nobody is testing them): ' + ', '.join(behind))
