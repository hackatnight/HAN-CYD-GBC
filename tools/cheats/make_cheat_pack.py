# Builds an SD card cheat pack for H@N-CYD-GBC from the libretro cheat database.
#   git clone --depth 1 https://github.com/libretro/libretro-database
#   python3 make_cheat_pack.py libretro-database/cht out
# Then copy out/CHEATS to the root of the SD card. Keeps only codes the firmware can use
# (GameShark, Pro Action Replay, raw address=value), up to 48 per game.
import os, re, sys, collections
if len(sys.argv) != 3:
    sys.exit('usage: make_cheat_pack.py <libretro-database/cht> <output folder>')
SRC = sys.argv[1]
OUT = os.path.join(sys.argv[2], 'CHEATS')
HEX = re.compile(r'^[0-9A-F]+$')

def parse_part(p, nes):
    p = p.strip().upper().replace(' ', '')
    if not p: return None
    sep = ':' if ':' in p else ('=' if '=' in p else None)
    if sep:
        a, v = p.split(sep, 1)
        if HEX.match(a) and HEX.match(v) and len(a) <= 4 and len(v) <= 2: return True
        return False
    if not HEX.match(p): return False          # Game Genie letters / dashes
    if not nes and len(p) == 8: return True    # GameShark ttVVLLHH
    if nes and len(p) in (6, 8): return True   # PAR AAAAVV / 00AAAAVV
    return False

def usable(code, nes):
    parts = [x for x in code.split('+') if x.strip()]
    if not parts or len(parts) > 4: return False
    return all(parse_part(x, nes) is True for x in parts)

def read_cht(path):
    cheats = collections.OrderedDict()
    txt = open(path, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'^\s*cheat(\d+)_(desc|code|enable)\s*=\s*"?(.*?)"?\s*$', txt, re.M):
        i, f, v = int(m.group(1)), m.group(2), m.group(3)
        cheats.setdefault(i, {})[f] = v
    return [c for _, c in sorted(cheats.items()) if 'code' in c]

TAG = re.compile(r'\s*\((GameShark|Game Genie|Pro Action Replay|Action Replay|Code Breaker|Xploder|Raw)\)', re.I)
def game_name(fn):
    n = fn[:-4]
    n = re.sub(r'\s*\(diff\)', '', n, flags=re.I)
    return TAG.sub('', n).strip()

stats = {}
for sysdir, nes, outdir in (('Nintendo - Game Boy', False, OUT + '/GB'), ('Nintendo - Game Boy Color', False, OUT + '/GBC'),
                            ('Nintendo - Nintendo Entertainment System', True, OUT + '/NES')):
    games = collections.OrderedDict()
    total = kept = 0
    for fn in sorted(os.listdir(os.path.join(SRC, sysdir))):
        if not fn.endswith('.cht'): continue
        for c in read_cht(os.path.join(SRC, sysdir, fn)):
            total += 1
            code = c['code'].replace('"', '')
            if not usable(code, nes): continue
            desc = (c.get('desc') or code).replace('"', "'").strip() or code
            g = games.setdefault(game_name(fn), [])
            if all(x[1] != code for x in g):
                g.append((desc, code)); kept += 1
    os.makedirs(outdir, exist_ok=True)
    nfiles = 0
    for g, lst in games.items():
        if not lst: continue
        lst = lst[:48]                         # the firmware keeps up to 48 per game
        with open(os.path.join(outdir, g + '.cht'), 'w', encoding='utf-8') as f:
            f.write('cheats = %d\n\n' % len(lst))
            for i, (d, c) in enumerate(lst):
                f.write('cheat%d_desc = "%s"\ncheat%d_code = "%s"\ncheat%d_enable = false\n\n' % (i, d, i, c, i))
        nfiles += 1
    stats[sysdir] = (total, kept, nfiles)
for k, (t, kp, n) in stats.items():
    print(f'{k}: {kp} of {t} cheats usable, {n} game files')
