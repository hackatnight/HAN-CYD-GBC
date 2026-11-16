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
SCROLL_MSG = ('   HELLO FROM H@N!   THIS LITTLE DEMO RUNS ON THE H@N-CYD-GBC EMULATOR ON A '
              '$10 TOUCH SCREEN BOARD.   WIREFRAMES, CUBES, RASTER TRICKS AND A SINE SCROLLER, '
              'ALL BUILT FROM SCRATCH.   GREETINGS TO EVERYONE TESTING NEW BOARDS!   ')

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


def build_gb(outdir):
    font = gfx.glcd_font()
    blobs = {}
    blobs['FontBright'] = b''.join(gfx.gb_tile(t) for t in gfx.font_tiles(font, FONT_CHARS, 3, 1))
    blobs['FontDim'] = b''.join(gfx.gb_tile(t) for t in gfx.font_tiles(font, FONT_CHARS, 2))
    blobs['SprTiles'] = b''.join(gfx.gb_tile(t) for t in gfx.sprite_tiles())

    # --- pre-rendered 3D frames
    WIRE_W, WIRE_H, WIRE_N = 16, 7, 48
    CUBE_W, CUBE_H, CUBE_N = 10, 8, 24
    wire = [b''.join(gfx.gb_tile(t) for t in gfx.to_tiles(wire_frame(i, WIRE_N, WIRE_W * 8, WIRE_H * 8)))
            for i in range(WIRE_N)]
    cube = [b''.join(gfx.gb_tile(t) for t in gfx.to_tiles(
        gfx.cubes_frame(i / CUBE_N, CUBE_W * 8, CUBE_H * 8))) for i in range(CUBE_N)]

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

    # names row: which words light up
    names = []
    row1, row2 = 12, 14
    line1 = ['UP', 'DOWN', 'LEFT', 'RIGHT']
    line2 = ['A', 'B', 'SELECT', 'START']
    pos = {}
    for row, words in ((row1, line1), (row2, line2)):
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

    # --- tables
    sine = [round(6 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)] + [0] * 192
    siney = [16 + 110 + round(14 * math.sin(2 * math.pi * i / 64)) for i in range(64)] + [0] * 192
    rain_lo, rain_hi = [], []
    for i in range(256):
        r, g, b = colorsys.hsv_to_rgb((i % 64) / 64.0, 0.55, 1.0)
        lo, hi = gb_rgb555(int(r * 255), int(g * 255), int(b * 255))
        rain_lo.append(lo)
        rain_hi.append(hi)

    lead, bass = music()
    note_lo, note_hi = [0], [0]
    for i in range(1, 65):
        f = 440.0 * 2 ** ((i + 32 - 69) / 12.0)
        x = max(0, min(2047, round(2048 - 131072 / f)))
        note_lo.append(x & 0xFF)
        note_hi.append(x >> 8)

    # palettes per scene: 0 intro, 1 wire, 2 cubes, 3 scroller, 4 input test
    scene_pals = [
        ('#0e1622', '#34495e', '#86a8c4', '#e4ecf2'),   # H@N night
        ('#0e1622', '#5a4a33', '#9a8462', '#f0e2c4'),   # bronze
        ('#06141c', '#145a6e', '#3cb4c8', '#d8fbff'),   # cyan
        ('#120c24', '#3c2a6e', '#8a6ad8', '#ffffff'),   # purple (color 3 = rainbow)
        ('#0e1622', '#34495e', '#86a8c4', '#ffd25a'),   # input test: gold highlights
    ]
    bgpal = []
    for p in scene_pals:
        for c in p:
            bgpal += gb_rgb555(*hexc(c))
    objpal = []
    for c in ('#000000', '#86a8c4', '#c8d6e2', '#ffd25a'):
        objpal += gb_rgb555(*hexc(c))

    # --- intro typed text: 1, dw address = move the cursor; 0 = end
    def stream(parts):
        out = []
        for row, text in parts:
            col = (20 - len(text)) // 2
            a = mapaddr(row, col)
            out += [1, a & 0xFF, a >> 8] + [ord(ch) for ch in text]
        return out + [0]
    intro = stream([(5, 'H@N PRESENTS'), (8, 'THE CYD DEMO'), (11, 'GB  GBC  NES')])

    consts = {
        'WIRE_W': WIRE_W, 'WIRE_H': WIRE_H, 'WIRE_COUNT': WIRE_N, 'WIRE_BYTES': len(wire[0]),
        'WIRE_DELAY': 3, 'WIRE_MAP': mapaddr(4, 2),
        'CUBE_W': CUBE_W, 'CUBE_H': CUBE_H, 'CUBE_COUNT': CUBE_N, 'CUBE_BYTES': len(cube[0]),
        'CUBE_DELAY': 3, 'CUBE_MAP': mapaddr(5, 5),
        'PAD_W': PAD_W, 'PAD_H': PAD_H, 'PAD_BYTES': len(blobs['PadTiles']), 'PAD_MAP': mapaddr(PAD_ROW, PAD_COL),
        'MSG_LEN': len(SCROLL_MSG), 'MUS_LEN': len(lead), 'MUS_SPEED': 7,
        'SC_INTRO': 0, 'SC_WIRE': 1, 'SC_CUBES': 2, 'SC_SCROLL': 3, 'SC_INPUT': 4, 'SC_COUNT': 5,
        'WOB_TOP': 16, 'WOB_BOT': 72,
    }
    head = '\n'.join('%s = %d' % kv for kv in consts.items()) + '\n'

    data = ['org $1800']
    for name in ('FontBright', 'FontDim', 'SprTiles', 'PadTiles'):
        data.append('%s:\n    incbin %s' % (name, name))
    data.append('StarData:\n' + db(stars))
    data.append('StarX0:\n' + db(x0))
    data.append('PadSpots:\n' + db(spots))
    data.append('PadNames:\n' + '\n'.join(names))
    data.append('NoteLo:\n' + db(note_lo))
    data.append('NoteHi:\n' + db(note_hi))
    data.append('MusLead:\n' + db(note_index(n) for n in lead))
    data.append('MusBass:\n' + db(note_index(n) if n else 0 for n in bass))
    data.append('CgbBgPal:\n' + db(bgpal))
    data.append('CgbObjPal:\n' + db(objpal))
    data.append('SceneLen:\n    db 2, 3, 2, 3, 255')
    data.append('IntroStream:\n' + db(intro))
    data.append('ScrollMsg:\n    db "%s"' % SCROLL_MSG)
    data.append(text_entry('TxtModeDmg', 15, 'GAME BOY MODE'))
    data.append(text_entry('TxtModeCgb', 15, 'GAME BOY COLOR MODE'))
    data.append(text_entry('TxtHint', 17, 'A NEXT  B MUSIC'))
    data.append(text_entry('TxtWireTitle', 1, 'WIREFRAME H@N'))
    data.append(text_entry('TxtWireCap', 13, 'LIKE THE BOOT SCREEN'))
    data.append(text_entry('TxtCubeTitle', 1, 'SPINNING CUBES'))
    data.append(text_entry('TxtCubeCap', 15, 'PRE-RENDERED 3D'))
    data.append(text_entry('TxtWob1', 2, '* H@N * H@N * H@N * H@N * H@N *', col=0))
    data.append(text_entry('TxtWob2', 4, 'RASTER WOBBLE'))
    data.append(text_entry('TxtWob3', 6, 'ONE LINE AT A TIME'))
    data.append(text_entry('TxtWob4', 8, '* H@N * H@N * H@N * H@N * H@N *', col=0))
    data.append(text_entry('TxtInputTitle', 1, 'INPUT TEST'))
    data.append(text_entry('TxtInputHint', 17, 'PRESS ANY BUTTON'))
    # tables that must start on a 256-byte boundary
    data.append('org $3A00\nSineTab:\n' + db(sine))
    data.append('SineY:\n' + db(siney))
    data.append('RainLo:\n' + db(rain_lo))
    data.append('RainHi:\n' + db(rain_hi))

    # --- frames into banks 1.. (a frame never crosses a bank)
    frame_tables = []
    bank, used = 1, 0
    for name, frames in (('Wire', wire), ('Cube', cube)):
        entries = []
        for i, fr in enumerate(frames):
            if used + len(fr) > 0x4000:
                bank, used = bank + 1, 0
            label = '%sF%d' % (name, i)
            blobs[label] = fr
            if used == 0:
                data.append('bank %d' % bank)
            data.append('%s:\n    incbin %s' % (label, label))
            used += len(fr)
            entries.append('    db BANK(%s)\n    dw %s' % (label, label))
        frame_tables.append('%sFrames:\n%s' % (name, '\n'.join(entries)))
    # the frame tables live in bank 0, before the banked data
    tables = 'org $3600\n' + '\n'.join(frame_tables)
    data.insert(data.index('org $3A00\nSineTab:\n' + db(sine)), tables)

    src = head + open(os.path.join(HERE, 'gb_demo.asm')).read() + '\n' + '\n'.join(data) + '\n'
    asm = SM83(blobs)
    img = asm.assemble(src)
    code_end = asm.symbols['InputTick']
    out = {}
    for ext, flag, title in (('gb', 0x00, 'HAN DEMO'), ('gbc', 0x80, 'HAN DEMO')):
        rom = fix_header(bytearray(img), title, flag, 0x19)   # MBC5
        path = os.path.join(outdir, 'HAN-DEMO.' + ext)
        open(path, 'wb').write(rom)
        out[ext] = path
    print('Game Boy demo: %d KB, %d wire frames, %d cube frames, banks 1-%d' %
          (len(img) // 1024, WIRE_N, CUBE_N, bank))
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


def build_nes(outdir):
    from asm6502 import A6502
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
    WIRE_W, WIRE_H, WIRE_N = 16, 7, 48
    CUBE_W, CUBE_H, CUBE_N = 10, 8, 24
    PAD_W, PAD_H = 16, 7
    bank = 2
    WIRE_BANK = bank
    for i in range(WIRE_N):
        put(bank, gfx.to_tiles(wire_frame(i, WIRE_N, WIRE_W * 8, WIRE_H * 8)))
        bank += 2
    CUBE_BANK = bank
    for i in range(CUBE_N):
        put(bank, gfx.to_tiles(gfx.cubes_frame(i / CUBE_N, CUBE_W * 8, CUBE_H * 8)))
        bank += 2
    PAD_BANK = bank
    pad, centers = gfx.pad_image(PAD_W * 8, PAD_H * 8, 'nes')
    put(bank, gfx.to_tiles(pad))
    bank += 2
    SPR_BANK = 248                                        # 2 KB at $1000: font + stars + disk
    put(SPR_BANK, gfx.font_tiles(font, FONT_CHARS, 3, 1) + gfx.sprite_tiles())
    assert bank <= SPR_BANK
    chr_rom = b''.join(banks)

    # layout (32 x 30 tiles)
    WIRE_ROW, WIRE_COL = 10, 8
    CUBE_ROW, CUBE_COL = 10, 11
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

    def entry(label, row, text, col=None, screen=0):
        text = text.upper()
        if col is None:
            col = (32 - len(text)) // 2
        return '%s:\n    dw $%04X\n    db "%s", 0' % (label, ntaddr(row, col, screen), text)

    wob = '* H@N * H@N * H@N * H@N * H@N * '
    texts = [
        ('T_MODE', 'TxMode', 22, 'NES MODE'),
        ('T_HINT', 'TxHint', 26, 'A NEXT   B MUSIC   START INPUT'),
        ('T_WIRE_TITLE', 'TxWireT', 5, 'WIREFRAME H@N'),
        ('T_WIRE_CAP', 'TxWireC', 20, 'LIKE THE CYD BOOT SCREEN'),
        ('T_CUBE_TITLE', 'TxCubeT', 5, 'SPINNING CUBES'),
        ('T_CUBE_CAP', 'TxCubeC', 21, 'PRE-RENDERED 3D, 30 FPS'),
        ('T_INPUT_TITLE', 'TxInT', 4, 'INPUT TEST'),
        ('T_INPUT_HINT', 'TxInH', 25, 'PRESS ANY BUTTON'),
    ]
    entries, table = [], []
    for i, (const, label, row, text) in enumerate(texts):
        entries.append(entry(label, row, text))
        table.append(label)
    wobrows = [(5, wob), (7, 'RASTER WOBBLE    RASTER WOBBLE   '), (9, 'MMC3 SCANLINE IRQ  MMC3 IRQ     '), (11, wob)]
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
    intro = stream([(8, 'H@N PRESENTS'), (12, 'THE CYD DEMO'), (16, 'GB  GBC  NES')])

    lead, bass = music()
    pl, ph, tl, th = nes_note_tables()
    sine = [round(6 * math.sin(2 * math.pi * i / 64)) & 0xFF for i in range(64)]
    siney = [150 + round(26 * math.sin(2 * math.pi * i / 64)) for i in range(64)]
    rain = [0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x31, 0x33, 0x35, 0x37]
    scene_pal = [0x0F, 0x01, 0x21, 0x30,   # intro
                 0x0F, 0x07, 0x17, 0x37,   # wireframe: bronze
                 0x0F, 0x0C, 0x1C, 0x3C,   # cubes: cyan
                 0x0F, 0x03, 0x13, 0x30,   # scroller (color 3 cycles)
                 0x0F, 0x01, 0x21, 0x38]   # input test: gold
    spr_pal = [0x0F, 0x21, 0x31, 0x38] * 4

    c = dict(consts)
    c.update({
        'WIRE_W': WIRE_W, 'WIRE_H': WIRE_H, 'WIRE_COUNT': WIRE_N, 'WIRE_BANK': WIRE_BANK,
        'WIRE_NT': ntaddr(WIRE_ROW, WIRE_COL),
        'CUBE_W': CUBE_W, 'CUBE_H': CUBE_H, 'CUBE_COUNT': CUBE_N, 'CUBE_BANK': CUBE_BANK,
        'CUBE_NT': ntaddr(CUBE_ROW, CUBE_COL),
        'PAD_W': PAD_W, 'PAD_H': PAD_H, 'PAD_BANK': PAD_BANK, 'PAD_NT': ntaddr(PAD_ROW, PAD_COL),
        'SPR_BANK': SPR_BANK, 'MSG_LEN': len(SCROLL_MSG), 'MUS_LEN': len(lead), 'MUS_SPEED': 7,
        'SC_INTRO': 0, 'SC_WIRE': 1, 'SC_CUBES': 2, 'SC_SCROLL': 3, 'SC_INPUT': 4, 'SC_COUNT': 5,
        'WOB_TOP': 36, 'WOB_COUNT': 28,
    })
    head = '\n'.join('%s = %d' % kv for kv in c.items()) + '\n'
    data = ['org $8000']
    data.append('TextTable:\n' + '\n'.join('    dw ' + t for t in table))
    data += entries
    data.append('SetupTable:\n    dw SetupIntro-1, SetupWire-1, SetupCubes-1, SetupScroll-1, SetupInput-1')
    data.append('SceneLen:\n    db 2, 3, 2, 3, 255')
    data.append('StarData:\n' + db(stars))
    data.append('StarX0:\n' + db(x0))
    data.append('PadSpots:\n' + db(spots))
    data.append('PadNames:\n' + '\n'.join(names))
    data.append('PulseLo:\n' + db(pl))
    data.append('PulseHi:\n' + db(ph))
    data.append('TriLo:\n' + db(tl))
    data.append('TriHi:\n' + db(th))
    data.append('MusLead:\n' + db(note_index(n) for n in lead))
    data.append('MusBass:\n' + db(note_index(n) if n else 0 for n in bass))
    data.append('SineTab:\n' + db(sine))
    data.append('SineY:\n' + db(siney))
    data.append('RainNes:\n' + db(rain))
    data.append('ScenePal:\n' + db(scene_pal))
    data.append('SprPal:\n' + db(spr_pal))
    data.append('IntroStream:\n' + db(intro))
    data.append('ScrollMsg:\n    db "%s"' % SCROLL_MSG)
    data.append('org $FFFA\n    dw NMI, Reset, IRQ')
    src = head + open(os.path.join(HERE, 'nes_demo.asm')).read() + '\n' + '\n'.join(data) + '\n'
    prg = A6502().assemble(src)
    header = b'NES\x1a' + bytes([2, 32, 0x41, 0x00]) + bytes(8)   # mapper 4, 32 KB PRG, 256 KB CHR
    path = os.path.join(outdir, 'HAN-DEMO.nes')
    open(path, 'wb').write(header + prg + chr_rom)
    print('NES demo: %d KB (MMC3), %d wire frames, %d cube frames, CHR banks used %d' %
          ((16 + len(prg) + len(chr_rom)) // 1024, WIRE_N, CUBE_N, bank))
    return path


if __name__ == '__main__':
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(outdir, exist_ok=True)
    build_gb(outdir)
    build_nes(outdir)
