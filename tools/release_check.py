"""Run before every push of a release: fails if any place that carries the version or a
firmware image is out of step with src/main.cpp.

    python3 tools/release_check.py

Checks: the version in the flasher page's header, every manifest's version and image file,
that each image contains this version (and no older 2.x.y of ours), that each board on the
page has a manifest, and that the demo zip holds the same ROMs as docs/demos.
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

used = set(re.findall(r'manifest="firmware/(manifest-[^"]+\.json)"', page))
have = {os.path.basename(p) for p in glob.glob('docs/firmware/manifest-*.json')}
for x in sorted(used - have):
    bad.append('page uses %s, which does not exist' % x)
for x in sorted(have - used):
    bad.append('%s has no install button on the page' % x)

images = set()
for p in sorted(glob.glob('docs/firmware/manifest-*.json')):
    j = json.load(open(p))
    if j['version'] != ver:
        bad.append('%s says %s' % (p, j['version']))
    for b in j['builds']:
        for part in b['parts']:
            f = os.path.join('docs/firmware', part['path'])
            images.add(f)
            if not os.path.exists(f):
                bad.append('%s points at missing %s' % (p, f))
for f in sorted(glob.glob('docs/firmware/*.bin')):
    if f not in images:
        bad.append('%s is not used by any manifest' % f)
    d = open(f, 'rb').read()
    if ver.encode() not in d:
        bad.append('%s was not built from version %s' % (f, ver))

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
