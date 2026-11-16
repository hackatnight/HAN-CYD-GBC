"""Builds the H@N demo ROMs from scratch: graphics, music and code.

  python3 build.py OUTDIR      -> OUTDIR/HAN-DEMO.gb, HAN-DEMO.gbc (and HAN-DEMO.nes when present)

Everything is original: the code (gb_demo.asm), the graphics (gfx.py: the H@N wireframe
from src/wire/wire_logo.h, cubes, the controller drawing), the music, and the font
(the BSD-licensed Adafruit 5x7 font). No game or BIOS data is included.
"""
import colorsys
import math
import os
import sys
import numpy as np

import gfx
from sm83asm import SM83, fix_header

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- shared content
FONT_CHARS = ''.join(chr(c) for c in range(32, 96))
SCROLL_MSG = ('   GREETINGS FROM H@N!   WIREFRAMES, CUBES AND RASTER TRICKS BUILT FROM SCRATCH.   '
              'GREETZ TO ALL BOARD TESTERS.   VISIT HACKATNIGHT.COM   ')
BAND_TEXT = 'HACKATNIGHT.COM'
SHINE_BLUE = (0x3a, 0x8e, 0xe8)      # the flying text's blue; the shine sweeps toward white


def shine_table():
    """256 colors: mostly blue, with a bright sweep over the first 32 entries."""
    out = []
    for i in range(256):
        t = max(0.0, 1.0 - abs(i - 16) / 16.0) if i < 32 else 0.0
        r, g, b = (round(c + (255 - c) * 0.85 * t) for c in SHINE_BLUE)
        out.append((r, g, b))
    return out

# music: 16th-note steps, 4 bars of 16 (Am F C G); 0 = nothing new on that step
CHORDS = [(69, 72, 76, 81), (65, 69, 72, 77), (72, 76, 79, 84), (67, 71, 74, 79)]
BASS = [45, 41, 48, 43]
ARP = [0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 2, 1, 3]


def music():
    lead, bass = [], []
    for bar in range(4):
        for step in range(16):
            lead.append(CHORDS[bar][ARP[step]])
            bass.append((BASS[bar] + (12 if step % 8 == 4 else 0)) if step % 4 == 0 else 0)
    return lead, bass


def wire_frame(i, n, w, h):
    """The boot screen's motion: the logo swings side to side and nods a little."""
    t = 2 * math.pi * i / n
    return gfx.wire_logo_frame(1.05 * math.sin(t), w, h, ax=0.22 + 0.18 * math.sin(2 * t))


def note_index(midi):
    return midi - 32       # 1 .. 64 (0 = rest)


# ---------------------------------------------------------------- Game Boy
def gb_rgb555(r, g, b):
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    return [v & 0xFF, v >> 8]


def hexc(h):
    return int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)


def db(values, per=16):
    values = list(values)
    return '\n'.join('    db ' + ', '.join(str(v) for v in values[i:i + per]) for i in range(0, len(values), per))


def mapaddr(row, col):
    return 0x9800 + row * 32 + col


def text_entry(label, row, text, col=None):
    text = text.upper()
    if col is None:
        col = (20 - len(text)) // 2
    return '%s:\n    dw $%04X\n    db "%s", 0\n' % (label, mapaddr(row, col), text)


# ---------------------------------------------------------------- shared scene content
# scenes: 0 intro, 1 wire, 2 cubes, 3 sphere, 4 pyramid, 5 D-pad cube, 6 tesseract, 7 scroller, 8 input
SCENES = ['INTRO', 'WIRE', 'CUBES', 'SPHERE', 'PYRAMID', 'DCUBE', 'TESS', 'SCROLL', 'INPUT']
SHAPE_TEXT = {      # title, caption
    'WIRE': ('WIREFRAME H@N', 'SPIN THAT LOGO'),
    'CUBES': ('SPINNING CUBES', 'CUBES, BUT COOLER'),
    'SPHERE': ('DOT BALL', "DOTS ALL, FOLKS"),
    'PYRAMID': ('PYRAMID SCHEME', 'H@N IN ORBIT'),
    'DCUBE': ('D-PAD CUBE', 'YOU SPIN IT'),
    'TESS': ('TESSERACT', '4D, NO GLASSES'),
}
# starfield per scene: 0 left, 1 right, 2 fall, 3 rise, 4 diagonal, 5 twinkle, 6 warp out,
# 7 warp in, 8 right in the top rows, 9 follows the D-pad cube
STAR_TYPES = [0, 6, 2, 5, 4, 9, 7, 8, 3]
STAR_COUNTS = [16, 16, 16, 16, 16, 16, 16, 12, 6]
SCENE_LEN = [2, 3, 2, 3, 3, 4, 3, 3, 255]       # x 256 frames
# shape colors: (dim, bright) for SELECT; H@N colors for START
SHAPE_COLS = [('#5a4a33', '#9a8462'), ('#145a6e', '#3cb4c8'), ('#6a5410', '#e0b830'), ('#5a1e5a', '#d050c8'),
              ('#1e5a2a', '#4ccc5c'), ('#6a1e1e', '#e05040'), ('#1e2e7a', '#5a7af0'), ('#40485a', '#a8b4c8')]
HAN_COLS = ['#f0e2c4', '#d8fbff', '#78dcff', '#ffd25a', '#ff7ad0', '#7aff8a', '#ff8a4a', '#ffffff']
COLOR_DEFAULTS = {'WIRE': (0, 0), 'CUBES': (1, 1), 'SPHERE': (2, 2), 'PYRAMID': (3, 3), 'DCUBE': (1, 4), 'TESS': (6, 5)}


def shape_frames(name, wt, ht, counts):
    """Pixel frames for a shape scene: wt x ht tiles."""
    w, h = wt * 8, ht * 8
    if name == 'WIRE':
        return [wire_frame(i, counts, w, h) for i in range(counts)]
    if name == 'CUBES':
        return [gfx.cubes_frame(i / counts, w, h) for i in range(counts)]
    if name == 'SPHERE':
        return [gfx.sphere_frame(i, counts, w, h) for i in range(counts)]
    if name == 'PYRAMID':
        return [gfx.pyramid_frame(i, counts, w, h) for i in range(counts)]
    if name == 'TESS':
        return [gfx.tesseract_frame(i, counts, w, h) for i in range(counts)]
    yaw_n, pitch_n = counts          # D-pad cube: pitch-major, yaw over 90 deg, pitch over 180 deg
    return [gfx.dcube_frame(math.pi / 2 * y / yaw_n, math.pi * p / pitch_n, w, h)
            for p in range(pitch_n) for y in range(yaw_n)]


def warp_tables(cx, cy, xmax, ymax, hide):
    """16 rays x 32 steps of a star flying out from (cx, cy); off screen -> y = hide."""
    xs, ys = [], []
    for r in range(16):
        a = 2 * math.pi * (r * 7 % 16) / 16 + 0.2
        for t in range(32):
            d = 5 + 150 * (t / 31.0) ** 2
            x, y = cx + d * math.cos(a), cy + d * math.sin(a) * 0.85
            if 0 <= x < xmax and 0 <= y < ymax:
                xs.append(int(x)); ys.append(int(y))
            else:
                xs.append(0); ys.append(hide)
    return xs, ys


def gb_voice(v):
    vol = max(1, min(15, round(v.vol * 42)))
    per = 1 if v.decay >= 5 else 2 if v.decay >= 3 else 4 if v.decay >= 1 else 6
    duty = {0.125: 0x00, 0.25: 0x40, 0.5: 0x80}.get(v.duty, 0x80)
    return (vol << 4) | per, duty


def build_gb(outdir):
    import tunes
    font = gfx.glcd_font()
    blobs = {}
    blobs['FontBright'] = b''.join(gfx.gb_tile(t) for t in gfx.font_tiles(font, FONT_CHARS, 3, 1))
    blobs['FontDim'] = b''.join(gfx.gb_tile(t) for t in gfx.font_tiles(font, FONT_CHARS, 2))
    blobs['SprTiles'] = b''.join(gfx.gb_tile(t) for t in gfx.sprite_tiles() + [np.zeros((8, 8), np.uint8)])

    # --- shapes: (width, height) tiles, map row/col, frames, VBlanks per frame, mode
    YAW_N, PITCH_N = 8, 16
    shapes = {
        'WIRE': (16, 7, 4, 2, 48, 3, 0), 'CUBES': (10, 8, 5, 5, 24, 3, 0),
        'SPHERE': (10, 9, 3, 5, 32, 3, 0), 'PYRAMID': (12, 9, 3, 4, 40, 2, 0),
        'DCUBE': (10, 9, 3, 5, (YAW_N, PITCH_N), 1, 1), 'TESS': (10, 9, 3, 5, 40, 3, 0)}
    shape_rows = {'WIRE': (1, 13), 'CUBES': (1, 15)}           # title, caption rows
    band_rows = {'INTRO': 14, 'WIRE': 15, 'CUBES': 3, 'SCROLL': 10, 'INPUT': 15}
    frames = {}
    for name, (wt, ht, r, c, n, dl, mode) in shapes.items():
        frames[name] = [b''.join(gfx.gb_tile(t) for t in gfx.to_tiles(f)) for f in shape_frames(name, wt, ht, n)]

    # --- controller drawing for the input test
    PAD_W, PAD_H = 16, 7
    pad, centers = gfx.pad_image(PAD_W * 8, PAD_H * 8, 'gb')
    blobs['PadTiles'] = b''.join(gfx.gb_tile(t) for t in gfx.to_tiles(pad))
    PAD_ROW, PAD_COL = 3, 2
    order = ['a', 'b', 'select', 'start', 'right', 'left', 'up', 'down']   # pad bits 0..7
    spots = []
    for k in order:
        x, y = centers[k]
        spots += [PAD_ROW * 8 + y + 16 - 4, PAD_COL * 8 + x + 8 - 4]
    names = []
    pos = {}
    for row, words in ((11, ['UP', 'DOWN', 'LEFT', 'RIGHT']), (13, ['A', 'B', 'SELECT', 'START'])):
        text = ' '.join(words)
        col = (20 - len(text)) // 2
        for w in words:
            pos[w] = (row, col)
            col += len(w) + 1
    for k in order:
        w = k.upper()
        r, c = pos[w]
        names.append('    dw $%04X\n    db %d, "%s"' % (mapaddr(r, c), len(w), w))

    # --- stars: y, speed, tile ; starting x
    rng = np.random.RandomState(7)
    stars, x0 = [], []
    for i in range(16):
        speed = 1 + i % 3
        stars += [int(rng.randint(16 + 4, 16 + 140)), speed, 120 if speed < 3 else 121]
        x0.append(int(rng.randint(0, 256)))
    warp_x, warp_y = warp_tables(84, 84, 168, 160, 0)
    twink = [123, 120, 120, 121, 120, 123, 123, 120, 121, 121, 120, 123, 120, 120, 123, 121]

    # --- tables
    sine = [round(6 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)] + [0] * 192
    sine_small = [round(3 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)] + [0] * 192
    shine_lo, shine_hi = [], []
    for r, g, b in shine_table():
        lo, hi = gb_rgb555(r, g, b)
        shine_lo.append(lo)
        shine_hi.append(hi)
    siney = [16 + 110 + round(14 * math.sin(2 * math.pi * i / 64)) for i in range(64)] + [0] * 192
    rain_lo, rain_hi = [], []
    for i in range(256):
        r, g, b = colorsys.hsv_to_rgb((i % 64) / 64.0, 0.55, 1.0)
        lo, hi = gb_rgb555(int(r * 255), int(g * 255), int(b * 255))
        rain_lo.append(lo)
        rain_hi.append(hi)

    # --- music: notes from MIDI 24, pulse and wave periods
    note_lo, note_hi, wave_lo, wave_hi = [], [], [], []
    for m in range(24, 120):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        x = max(0, min(2047, round(2048 - 131072 / f)))
        note_lo.append(x & 0xFF); note_hi.append(x >> 8)
        x = max(0, min(2047, round(2048 - 65536 / f)))
        wave_lo.append(x & 0xFF); wave_hi.append(x >> 8)
    wave_tri = []
    for i in range(16):
        a_, b_ = (i * 2) if i < 8 else (31 - i * 2), (i * 2 + 1) if i < 8 else (30 - i * 2)
        wave_tri.append(((min(15, a_) & 15) << 4) | (min(15, b_) & 15))
    mus = ['TuneTab:']
    chan_data = []
    for ti, (tname, song) in enumerate(tunes.TUNES):
        d = tunes.tune_data(song)
        ptrs = []
        for slot in ('p1', 'p2', 'wave', 'noise'):
            if d[slot]:
                lab = 'Mus%d_%s' % (ti, slot)
                chan_data.append('%s:\n%s' % (lab, db(d[slot][0])))
                ptrs.append(lab)
            else:
                ptrs.append('0')
        e1, d1 = gb_voice(d['p1'][1]) if d['p1'] else (0, 0)
        e2, d2 = gb_voice(d['p2'][1]) if d['p2'] else (0, 0)
        wl = 0x20 if d['wave'] and d['wave'][1].vol >= 0.35 else 0x40
        nv = d['noise'][1].vol if d['noise'] else 0.2
        lvl = lambda k: max(1, min(15, round(k * nv / 0.22)))
        drums = [(lvl(13) << 4) | 2, (lvl(6) << 4) | 1, (lvl(11) << 4) | 2]
        mus.append('    dw %d, %d, %s\n    db %d, %d, %d, %d, %d, %d, %d, %d\n    ds 12' % (
            d['speed'], d['steps'], ', '.join(ptrs), e1, d1, e2, d2, wl, *drums))
    music_src = '\n'.join(mus + chan_data)

    # --- palettes per scene (palette 0: text; palette 1: the flying HACKATNIGHT.COM)
    scene_pals = [
        ('#0e1622', '#34495e', '#86a8c4', '#e4ecf2'),   # intro
        ('#0e1622', '#5a4a33', '#9a8462', '#f0e2c4'),   # wire: bronze
        ('#06141c', '#145a6e', '#3cb4c8', '#d8fbff'),   # cubes: cyan
        ('#100c1a', '#4a3a20', '#b89a50', '#ffe6a0'),   # dot ball: gold
        ('#140a18', '#4a2050', '#b060b8', '#ffd0f4'),   # pyramid: magenta
        ('#06141c', '#1e4a5a', '#5ab4c8', '#e0faff'),   # D-pad cube
        ('#080c20', '#24306a', '#6a80e0', '#dce4ff'),   # tesseract: blue
        ('#120c24', '#3c2a6e', '#8a6ad8', '#ffffff'),   # scroller (color 3 = rainbow)
        ('#0e1622', '#34495e', '#86a8c4', '#ffd25a'),   # input test: gold highlights
    ]
    bgpal = []
    for p in scene_pals:
        for c in p:
            bgpal += gb_rgb555(*hexc(c))
        for c in (p[0], '#10285a', '#2f6fd0', '#%02x%02x%02x' % SHINE_BLUE):
            bgpal += gb_rgb555(*hexc(c))
    objpal = []
    for c in ('#000000', '#86a8c4', '#c8d6e2', '#ffd25a'):
        objpal += gb_rgb555(*hexc(c))
    shape_cols = []
    for dim, br in SHAPE_COLS:
        shape_cols += gb_rgb555(*hexc(dim)) + gb_rgb555(*hexc(br))
    han_cols = []
    for c in HAN_COLS:
        han_cols += gb_rgb555(*hexc(c))

    def stream(parts):
        out = []
        for row, text in parts:
            col = (20 - len(text)) // 2
            a = mapaddr(row, col)
            out += [1, a & 0xFF, a >> 8] + [ord(ch) for ch in text]
        return out + [0]
    intro_dmg = stream([(5, 'H@N PRESENTS'), (8, 'GAME BOY DEMO'), (11, BAND_TEXT)])
    intro_cgb = stream([(5, 'H@N PRESENTS'), (8, 'GAME BOY COLOR DEMO'), (11, BAND_TEXT)])
    brow = [band_rows.get(n, 15) for n in SCENES]

    consts = {
        'PAD_W': PAD_W, 'PAD_H': PAD_H, 'PAD_BYTES': len(blobs['PadTiles']), 'PAD_MAP': mapaddr(PAD_ROW, PAD_COL),
        'MSG_LEN': len(SCROLL_MSG), 'TUNE_COUNT': len(tunes.TUNES), 'MUS_BANK': 1,
        'YAW_N': YAW_N, 'PITCH_N': PITCH_N,
        'WOB_TOP': 16, 'WOB_BOT': 72, 'BAND_H': 12,
    }
    for i, n in enumerate(SCENES):
        consts['SC_' + n] = i
    consts['SC_COUNT'] = len(SCENES)
    head = '\n'.join('%s = %d' % kv for kv in consts.items()) + '\n'

    def hint(label, text):
        return text_entry(label, 17, text.center(20))
    data = []
    for name in ('FontBright', 'FontDim', 'SprTiles', 'PadTiles'):
        data.append('%s:\n    incbin %s' % (name, name))
    data.append('StarData:\n' + db(stars))
    data.append('StarX0:\n' + db(x0))
    data.append('StarTypeTab:\n' + db(STAR_TYPES))
    data.append('StarCountTab:\n' + db(STAR_COUNTS))
    data.append('StarJump:\n    dw StLeft, StRight, StFall, StRise, StDiag, StTwinkle, StWarpOut, StWarpIn, StRight, StDpad')
    data.append('TwinkTab:\n' + db(twink))
    data.append('WarpX:\n' + db(warp_x))
    data.append('WarpY:\n' + db(warp_y))
    data.append('PadSpots:\n' + db(spots))
    data.append('PadNames:\n' + '\n'.join(names))
    data.append('NoteLo:\n' + db(note_lo))
    data.append('NoteHi:\n' + db(note_hi))
    data.append('WaveLo:\n' + db(wave_lo))
    data.append('WaveHi:\n' + db(wave_hi))
    data.append('WaveTri:\n' + db(wave_tri))
    data.append('DrumPoly:\n    db $61, $01, $43')
    data.append('CgbBgPal:\n' + db(bgpal))
    data.append('CgbObjPal:\n' + db(objpal))
    data.append('ShapeColTab:\n' + db(shape_cols))
    data.append('HanColTab:\n' + db(han_cols))
    data.append('ColorDefaults:\n' + db(v for n in SCENES for v in COLOR_DEFAULTS.get(n, (0, 0))))
    data.append('SceneLen:\n' + db(SCENE_LEN))
    data.append('IntroStreamDmg:\n' + db(intro_dmg))
    data.append('IntroStreamCgb:\n' + db(intro_cgb))
    data.append('ScrollMsg:\n    db "%s"' % SCROLL_MSG)
    data.append('BandText:\n    db "%s", 0' % BAND_TEXT)
    data.append('BandTopTab:\n' + db(r * 8 - 2 for r in brow))
    data.append('BandMapTab:\n' + '\n'.join('    dw $%04X' % mapaddr(r, 0) for r in brow))
    # hints: three per scene, shown in turn on row 17
    data.append(hint('HNav', 'A NEXT  B TUNE'))
    data.append(hint('HCol', 'SEL/START: COLORS'))
    data.append(hint('HDir', 'D-PAD: SPIN WAY'))
    data.append(hint('HCube', 'D-PAD: SPIN CUBE'))
    data.append(hint('HInput', 'PRESS ANY BUTTON'))
    hints_c, hints_d = [], []
    for n in SCENES:
        if n == 'INPUT':
            hc = hd = ['HInput'] * 3
        elif n == 'DCUBE':
            hc, hd = ['HNav', 'HCube', 'HCol'], ['HNav', 'HCube', 'HCube']
        elif n in SHAPE_TEXT:
            hc, hd = ['HNav', 'HCol', 'HDir'], ['HNav', 'HDir', 'HNav']
        else:
            hc = hd = ['HNav'] * 3
        hints_c += hc
        hints_d += hd
    data.append('HintTab:\n    dw ' + ', '.join(hints_c))
    data.append('HintTabDmg:\n    dw ' + ', '.join(hints_d))
    tune_names = [n for n, _ in tunes.TUNES]
    labels = []
    for i, n in enumerate(tune_names + ['MUSIC OFF']):
        txt = ('TUNE %d: %s' % (i + 1, n)) if i < len(tune_names) else n
        data.append(hint('TName%d' % i, txt))
        labels.append('TName%d' % i)
    data.append('TuneNames:\n    dw ' + ', '.join(labels))
    # shape scenes
    stab = []
    for n in SCENES[1:7]:
        wt, ht, r, c, cnt, dl, mode = shapes[n]
        title, cap = SHAPE_TEXT[n]
        tr, cr = shape_rows.get(n, (1, 13))
        data.append(text_entry('T%sTitle' % n, tr, title))
        data.append(text_entry('T%sCap' % n, cr, cap))
        nb = len(frames[n][0])
        stab.append('    dw T%sTitle, T%sCap, $%04X\n    db %d, %d\n    dw %sFrames\n    db %d\n    dw %d\n    db %d, %d, 0' % (
            n, n, mapaddr(r, c), wt, ht, n.capitalize(), len(frames[n]), nb, dl, mode))
    data.append('ShapeTab:\n' + '\n'.join(stab))
    data.append(text_entry('TxtWob1', 2, '* H@N * H@N * H@N * H@N * H@N *', col=0))
    data.append(text_entry('TxtWob2', 4, 'WIBBLY WOBBLY TEXT'))
    data.append(text_entry('TxtWob3', 6, 'NOW WITH 144 BENDS'))
    data.append(text_entry('TxtWob4', 8, '* H@N * H@N * H@N * H@N * H@N *', col=0))
    data.append(text_entry('TxtInputTitle', 1, 'INPUT TEST'))
    # tables that must start on a 256-byte boundary
    data.append('align 256\nSineTab:\n' + db(sine) + '\nSineY:\n' + db(siney) + '\nRainLo:\n' + db(rain_lo) +
                '\nRainHi:\n' + db(rain_hi) + '\nSineSmall:\n' + db(sine_small) + '\nShineLo:\n' + db(shine_lo) +
                '\nShineHi:\n' + db(shine_hi))
    tables_at = len(data)

    # --- banked: music first in bank 1, then the frames (a frame never crosses a bank)
    data.append('bank 1\n' + music_src)
    music_bytes = sum(len(d[k][0]) for _, sg in tunes.TUNES for d in [tunes.tune_data(sg)]
                      for k in ('p1', 'p2', 'wave', 'noise') if d[k]) + 32 * len(tunes.TUNES)
    frame_tables = []
    bank, used = 1, music_bytes
    for name in SCENES[1:7]:
        entries = []
        for i, fr in enumerate(frames[name]):
            if used + len(fr) > 0x4000:
                bank, used = bank + 1, 0
                data.append('bank %d' % bank)
            label = '%sF%d' % (name, i)
            blobs[label] = fr
            data.append('%s:\n    incbin %s' % (label, label))
            used += len(fr)
            entries.append('    db BANK(%s)\n    dw %s' % (label, label))
        frame_tables.append('%sFrames:\n%s' % (name.capitalize(), '\n'.join(entries)))
    data.insert(tables_at, '\n'.join(frame_tables))

    src = head + open(os.path.join(HERE, 'gb_demo.asm')).read() + '\n' + '\n'.join(data) + '\n'
    asm = SM83(blobs)
    img = asm.assemble(src)
    size = 0x8000
    while size < len(img):
        size *= 2
    img = bytes(img) + b'\xff' * (size - len(img))
    out = {}
    for ext, flag, title in (('gb', 0x00, 'HAN DEMO'), ('gbc', 0x80, 'HAN DEMO')):
        rom = fix_header(bytearray(img), title, flag, 0x19)   # MBC5
        path = os.path.join(outdir, 'HAN-DEMO.' + ext)
        open(path, 'wb').write(rom)
        out[ext] = path
    end0 = max(v for k, v in asm.symbols.items() if isinstance(v, int) and v < 0x4000 and asm.label_bank.get(k, 0) == 0)
    print('Game Boy demo: %d KB, banks 1-%d, bank 0 used to $%04X' % (len(img) // 1024, bank, end0))
    return out


# ---------------------------------------------------------------- NES
def ntaddr(row, col, screen=0):
    return 0x2000 + screen * 0x400 + row * 32 + col


def nes_note_tables():
    pl, ph, tl, th = [0], [0], [0], [0]
    for i in range(1, 65):
        f = 440.0 * 2 ** ((i + 32 - 69) / 12.0)
        p = max(8, min(2047, round(1789773 / (16 * f)) - 1))
        t = max(8, min(2047, round(1789773 / (32 * f)) - 1))
        pl.append(p & 0xFF)
        ph.append(p >> 8)
        tl.append(t & 0xFF)
        th.append(t >> 8)
    return pl, ph, tl, th


def nes_voice(v):
    vol = max(1, min(15, round(v.vol * 40)))
    dec = 2 if v.decay >= 5 else 3 if v.decay >= 3 else 6 if v.decay >= 1 else 12
    duty = {0.125: 0x00, 0.25: 0x40, 0.5: 0x80}.get(v.duty, 0x80)
    return vol, duty, dec


def build_nes(outdir):
    from asm6502 import A6502
    import tunes
    font = gfx.glcd_font()
    banks = [bytes(1024)] * 256          # 1 KB CHR banks (64 tiles each)

    def put(bank, tiles):
        data = b''.join(gfx.nes_tile(t) for t in tiles)
        data = data.ljust(((len(data) + 1023) // 1024) * 1024, b'\0')
        for i in range(0, len(data), 1024):
            banks[bank + i // 1024] = data[i:i + 1024]
        return len(data) // 1024

    put(0, gfx.font_tiles(font, FONT_CHARS, 3, 1))        # background tiles 0-63
    put(1, gfx.font_tiles(font, FONT_CHARS, 2))           # tiles 192-255 (dim)
    # shapes: (width, height) tiles, nametable row/col, frames, VBlanks per frame, mode.
    # Every frame is a pair of 1 KB banks (up to 128 tiles), and all of them share 256 KB.
    YAW_N, PITCH_N = 4, 8
    shapes = {
        'WIRE': (16, 7, 10, 8, 24, 3, 0), 'CUBES': (10, 8, 10, 11, 12, 3, 0),
        'SPHERE': (11, 11, 8, 10, 8, 4, 0), 'PYRAMID': (12, 10, 8, 10, 24, 3, 0),
        'DCUBE': (11, 11, 8, 10, (YAW_N, PITCH_N), 1, 1), 'TESS': (11, 11, 8, 10, 16, 4, 0)}
    bank = 2
    first = {}
    nframes = {}
    for name in SCENES[1:7]:
        wt, ht, r, c, n, dl, mode = shapes[name]
        first[name] = bank
        fr = shape_frames(name, wt, ht, n)
        nframes[name] = len(fr)
        for f in fr:
            put(bank, gfx.to_tiles(f))
            bank += 2
    PAD_W, PAD_H = 16, 7
    PAD_BANK = bank
    pad, centers = gfx.pad_image(PAD_W * 8, PAD_H * 8, 'nes')
    put(bank, gfx.to_tiles(pad))
    bank += 2
    SPR_BANK = 248                                        # 2 KB at $1000: font + stars + disk
    put(SPR_BANK, gfx.font_tiles(font, FONT_CHARS, 3, 1) + gfx.sprite_tiles())
    assert bank <= SPR_BANK, bank
    chr_rom = b''.join(banks)

    # layout (32 x 30 tiles)
    PAD_ROW, PAD_COL = 8, 8
    order = ['a', 'b', 'select', 'start', 'up', 'down', 'left', 'right']      # NES pad bits 0..7
    spots = []
    for k in order:
        x, y = centers[k]
        spots += [PAD_ROW * 8 + y - 4 - 1, PAD_COL * 8 + x - 4]
    pos = {}
    for row, words in ((18, ['UP', 'DOWN', 'LEFT', 'RIGHT']), (20, ['A', 'B', 'SELECT', 'START'])):
        text = ' '.join(words)
        col = (32 - len(text)) // 2
        for w in words:
            pos[w] = (row, col)
            col += len(w) + 1
    names = []
    for k in order:
        w = k.upper()
        r, c = pos[w]
        names.append('    dw $%04X\n    db %d, "%s"' % (ntaddr(r, c), len(w), w))

    rng = np.random.RandomState(11)
    stars, x0 = [], []
    for i in range(16):
        speed = 1 + i % 3
        stars += [int(rng.randint(16, 216)), speed, 64 if speed < 3 else 65]
        x0.append(int(rng.randint(0, 256)))
    wx, wy = warp_tables(124, 112, 248, 228, 0xFF)
    twink = [67, 64, 64, 65, 64, 67, 67, 64, 65, 65, 64, 67, 64, 64, 67, 65]

    def entry(label, row, text, col=None, screen=0):
        text = text.upper()
        if col is None:
            col = (32 - len(text)) // 2
        return '%s:\n    dw $%04X\n    db "%s", 0' % (label, ntaddr(row, col, screen), text)

    wob = '* H@N * H@N * H@N * H@N * H@N * '
    texts = [('T_INPUT_TITLE', 'TxInT', 4, 'INPUT TEST')]
    entries, table = [], []
    for i, (const, label, row, text) in enumerate(texts):
        entries.append(entry(label, row, text))
        table.append(label)
    wobrows = [(5, wob), (7, 'WIBBLY WOBBLY TEXT'.center(32)), (9, 'BENT BY THE MMC3, LINE BY LINE'.center(32)), (11, wob)]
    wconsts = []
    for n, (row, text) in enumerate(wobrows, 1):
        for scr, suffix in ((0, ''), (1, 'B')):
            label = 'TxWob%d%s' % (n, suffix)
            entries.append(entry(label, row, text[:32], col=0, screen=scr))
            table.append(label)
            wconsts.append('T_WOB%d%s' % (n, suffix))
    consts = {c: i * 2 for i, c in enumerate([t[0] for t in texts] + wconsts)}

    def stream(parts):
        out = []
        for row, text in parts:
            col = (32 - len(text)) // 2
            a = ntaddr(row, col)
            out += [1, a & 0xFF, a >> 8] + [ord(ch) for ch in text]
        return out + [0]
    intro = stream([(8, 'H@N PRESENTS'), (12, 'NES DEMO'), (16, BAND_TEXT)])

    # music
    pl, ph, tl, th = [], [], [], []
    for m in range(24, 120):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        p_ = max(8, min(2047, round(1789773 / (16 * f)) - 1))
        t_ = max(8, min(2047, round(1789773 / (32 * f)) - 1))
        pl.append(p_ & 0xFF); ph.append(p_ >> 8); tl.append(t_ & 0xFF); th.append(t_ >> 8)
    mus = ['TuneTab:']
    chan_data = []
    for ti, (tname, song) in enumerate(tunes.TUNES):
        d = tunes.tune_data(song)
        ptrs = []
        for slot in ('p1', 'p2', 'wave', 'noise'):
            if d[slot]:
                lab = 'Mus%d_%s' % (ti, slot)
                chan_data.append('%s:\n%s' % (lab, db(d[slot][0])))
                ptrs.append(lab)
            else:
                ptrs.append('0')
        v1 = nes_voice(d['p1'][1]) if d['p1'] else (0, 0, 0)
        v2 = nes_voice(d['p2'][1]) if d['p2'] else (0, 0, 0)
        nv = d['noise'][1].vol if d['noise'] else 0.2
        lvl = lambda k: max(1, min(15, round(k * nv / 0.22)))
        mus.append('    dw %d, %d, %s\n    db %d, %d, %d, %d, %d, %d, 0, 0, 0, 0, %d, %d, %d\n    ds 7' % (
            d['speed'], d['steps'], ', '.join(ptrs), *v1, *v2, lvl(12), lvl(5), lvl(9)))
    sine = [round(6 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)]
    siney = [150 + round(26 * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    rain = [0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x31, 0x33, 0x35, 0x37]
    scene_pal = [0x0F, 0x01, 0x21, 0x30,   # intro
                 0x0F, 0x07, 0x17, 0x37,   # wireframe: bronze
                 0x0F, 0x0C, 0x1C, 0x3C,   # cubes: cyan
                 0x0F, 0x08, 0x28, 0x38,   # dot ball: gold
                 0x0F, 0x04, 0x24, 0x34,   # pyramid: magenta
                 0x0F, 0x0C, 0x2C, 0x3C,   # D-pad cube
                 0x0F, 0x02, 0x22, 0x32,   # tesseract: blue
                 0x0F, 0x03, 0x13, 0x30,   # scroller (color 3 cycles)
                 0x0F, 0x01, 0x21, 0x38]   # input test: gold
    spr_pal = [0x0F, 0x21, 0x31, 0x38] * 4
    shape_nes = [(0x07, 0x17), (0x0C, 0x1C), (0x08, 0x28), (0x04, 0x24), (0x0A, 0x2A), (0x06, 0x16), (0x02, 0x22), (0x00, 0x10)]
    han_nes = [0x37, 0x3C, 0x31, 0x38, 0x34, 0x3A, 0x27, 0x30]

    # attributes per scene: palette 2 on the picture, palette 1 on the strip's rows
    band_rows = {'SCROLL': 2}
    attrs = []
    for n in SCENES:
        pal = np.zeros((30, 32), np.uint8)
        br = band_rows.get(n, 22)
        pal[br:br + 2, :] = 1
        if n in shapes:
            wt, ht, r, c = shapes[n][:4]
            pal[r:r + ht, c:c + wt] = 2
        at = []
        for by in range(8):
            for bx in range(8):
                v = 0
                for q, (dy, dx) in enumerate(((0, 0), (0, 2), (2, 0), (2, 2))):
                    ys, xs = by * 4 + dy, bx * 4 + dx
                    blk = pal[ys:ys + 2, xs:xs + 2]
                    v |= (int(blk.max()) if blk.size else 0) << (2 * q)
                at.append(v)
        attrs += at
    brows = [band_rows.get(n, 22) for n in SCENES]

    c = dict(consts)
    c.update({
        'PAD_W': PAD_W, 'PAD_H': PAD_H, 'PAD_BANK': PAD_BANK, 'PAD_NT': ntaddr(PAD_ROW, PAD_COL),
        'SPR_BANK': SPR_BANK, 'MSG_LEN': len(SCROLL_MSG), 'TUNE_COUNT': len(tunes.TUNES),
        'YAW_N': YAW_N, 'PITCH_N': PITCH_N, 'WOB_SLOT': 14, 'WOB_COUNT': 28,
    })
    for i, n in enumerate(SCENES):
        c['SC_' + n] = i
    c['SC_COUNT'] = len(SCENES)
    head = '\n'.join('%s = %d' % kv for kv in c.items()) + '\n'

    def hint(label, text):
        return entry(label, 26, text.center(32), col=0)
    data = ['org $8000']
    data.append('TextTable:\n' + '\n'.join('    dw ' + t for t in table))
    data += entries
    data.append('SetupTable:\n    dw SetupIntro-1' + ', SetupShape-1' * 6 + ', SetupScroll-1, SetupInput-1')
    data.append('SceneLen:\n' + db(SCENE_LEN))
    data.append('StarData:\n' + db(stars))
    data.append('StarX0:\n' + db(x0))
    data.append('StarTypeTab:\n' + db(STAR_TYPES))
    data.append('StarCountTab:\n' + db([min(n, 12) if i == 7 else n for i, n in enumerate(STAR_COUNTS)]))
    data.append('StarJump:\n    dw StLeft, StRight, StFall, StRise, StDiag, StTwinkle, StWarpOut, StWarpIn, StRight, StDpad')
    data.append('TwinkTab:\n' + db(twink))
    data.append('WarpX0:\n' + db(wx[:256]) + '\nWarpX1:\n' + db(wx[256:]))
    data.append('WarpY0:\n' + db(wy[:256]) + '\nWarpY1:\n' + db(wy[256:]))
    data.append('PadSpots:\n' + db(spots))
    data.append('PadNames:\n' + '\n'.join(names))
    data.append('PulseLo:\n' + db(pl))
    data.append('PulseHi:\n' + db(ph))
    data.append('TriLo:\n' + db(tl))
    data.append('TriHi:\n' + db(th))
    data.append('DrumPer:\n    db $0C, $01, $06')
    data.append('\n'.join(mus + chan_data))
    data.append('SineTab:\n' + db(sine))
    data.append('SineY:\n' + db(siney))
    data.append('RainNes:\n' + db(rain))
    data.append('ScenePal:\n' + db(scene_pal))
    data.append('SprPal:\n' + db(spr_pal))
    data.append('ShapeColNes:\n' + db(v for pr in shape_nes for v in pr))
    data.append('HanColNes:\n' + db(han_nes))
    data.append('ColorDefaults:\n' + db(v for n in SCENES for v in COLOR_DEFAULTS.get(n, (0, 0))))
    data.append('AttrTab:\n' + db(attrs))
    data.append('IntroStream:\n' + db(intro))
    data.append('BandText:\n    db "%s", 0' % BAND_TEXT)
    data.append('BandK:\n' + db((r * 8 - 8) // 2 - 1 for r in brows))
    data.append('BandNT:\n' + '\n'.join('    dw $%04X' % ntaddr(r, 0) for r in brows))
    data.append('ShineNes:\n' + db([0x11, 0x11, 0x21, 0x31, 0x30, 0x31, 0x21, 0x11]))
    data.append('SwayTab:\n' + db(round(2 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)))
    data.append('ScrollMsg:\n    db "%s"' % SCROLL_MSG)
    data.append(hint('HNav', 'A NEXT   B TUNE'))
    data.append(hint('HCol', 'SELECT / START: COLORS'))
    data.append(hint('HDir', 'LEFT / RIGHT: SPIN DIRECTION'))
    data.append(hint('HCube', 'D-PAD: SPIN THE CUBE'))
    data.append(hint('HInput', 'PRESS ANY BUTTON'))
    hints = []
    for n in SCENES:
        if n == 'INPUT':
            hints += ['HInput'] * 3
        elif n == 'DCUBE':
            hints += ['HNav', 'HCube', 'HCol']
        elif n in shapes:
            hints += ['HNav', 'HCol', 'HDir']
        else:
            hints += ['HNav'] * 3
    data.append('HintTab:\n    dw ' + ', '.join(hints))
    labels = []
    tnames = [n for n, _ in tunes.TUNES]
    for i, n in enumerate(tnames + ['MUSIC OFF']):
        data.append(hint('TName%d' % i, ('TUNE %d: %s' % (i + 1, n)) if i < len(tnames) else n))
        labels.append('TName%d' % i)
    data.append('TuneNames:\n    dw ' + ', '.join(labels))
    stab = []
    for n in SCENES[1:7]:
        wt, ht, r, cc, cnt, dl, mode = shapes[n]
        title, cap = SHAPE_TEXT[n]
        data.append(entry('T%sTitle' % n, 5, title))
        data.append(entry('T%sCap' % n, 20, cap))
        stab.append('    dw T%sTitle, T%sCap, $%04X\n    db %d, %d, %d, %d, %d, %d, 0, 0, 0, 0' % (
            n, n, ntaddr(r, cc), wt, ht, first[n], nframes[n], dl, mode))
    data.append('ShapeTab:\n' + '\n'.join(stab))
    data.append('org $FFFA\n    dw NMI, Reset, IRQ')
    src = head + open(os.path.join(HERE, 'nes_demo.asm')).read() + '\n' + '\n'.join(data) + '\n'
    asm = A6502()
    prg = asm.assemble(src)
    header = b'NES\x1a' + bytes([2, 32, 0x41, 0x00]) + bytes(8)   # mapper 4, 32 KB PRG, 256 KB CHR
    path = os.path.join(outdir, 'HAN-DEMO.nes')
    open(path, 'wb').write(header + prg + chr_rom)
    print('NES demo: %d KB (MMC3), CHR banks used %d, data ends $%04X, code ends $%04X' %
          ((16 + len(prg) + len(chr_rom)) // 1024, bank, asm.symbols['ShapeTab'] + 16 * 6,
           max(v for k, v in asm.symbols.items() if 0xC000 <= v < 0xFFFA)))
    return path


if __name__ == '__main__':
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(outdir, exist_ok=True)
    build_gb(outdir)
    build_nes(outdir)
