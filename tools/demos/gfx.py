"""Graphics for the H@N demo ROMs: font, pre-rendered 3D frames, the controller drawing.

Images are 2D lists/arrays of color indices 0..3 (0 = background). The wireframe H@N
model is the same one the firmware's boot screen uses (src/wire/wire_logo.h).
"""
import math
import os
import re
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
FONT_FILE = os.path.expanduser('~/Arduino/libraries/LovyanGFX/src/lgfx/Fonts/glcdfont.h')
FONT_CACHE = os.path.join(HERE, 'glcdfont.dat')   # 256 x 5 bytes, kept in the repo


# ---------------------------------------------------------------- font
def glcd_font():
    """The classic 5x7 Adafruit GFX font (BSD license), column-major, bit 0 at the top."""
    if os.path.exists(FONT_CACHE):
        data = open(FONT_CACHE, 'rb').read()
    else:
        src = open(FONT_FILE).read()
        body = src[src.index('{', src.index('font[]')):]
        body = body[:body.index('};')]
        data = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))[:256 * 5]
        open(FONT_CACHE, 'wb').write(data)
    return data


def char_bitmap(font, ch):
    """8x8 0/1 bitmap of a character, the 5x7 glyph placed at x 1..5, y 0..6."""
    g = np.zeros((8, 8), np.uint8)
    o = ord(ch) * 5
    for col in range(5):
        bits = font[o + col]
        for row in range(7):
            if bits >> row & 1:
                g[row, col + 1] = 1
    return g


def font_tiles(font, chars, color=3, shadow=None):
    out = []
    for ch in chars:
        b = char_bitmap(font, ch)
        t = np.zeros((8, 8), np.uint8)
        if shadow is not None:
            sh = np.zeros((8, 8), np.uint8)
            sh[1:, 1:] = b[:-1, :-1]
            t[sh == 1] = shadow
        t[b == 1] = color
        out.append(t)
    return out


# ---------------------------------------------------------------- tiles
def to_tiles(img):
    """Split an image (H x W, multiples of 8) into 8x8 tiles, row by row."""
    img = np.asarray(img, np.uint8)
    h, w = img.shape
    return [img[y:y + 8, x:x + 8] for y in range(0, h, 8) for x in range(0, w, 8)]


def gb_tile(t):
    """Game Boy 2bpp tile: per row, a low-bit byte then a high-bit byte, leftmost pixel = bit 7."""
    out = bytearray()
    for row in t:
        lo = hi = 0
        for x, c in enumerate(row):
            lo |= (int(c) & 1) << (7 - x)
            hi |= (int(c) >> 1 & 1) << (7 - x)
        out += bytes((lo, hi))
    return bytes(out)


def nes_tile(t):
    """NES 2bpp tile: 8 low-plane bytes, then 8 high-plane bytes."""
    lo = bytearray()
    hi = bytearray()
    for row in t:
        a = b = 0
        for x, c in enumerate(row):
            a |= (int(c) & 1) << (7 - x)
            b |= (int(c) >> 1 & 1) << (7 - x)
        lo.append(a)
        hi.append(b)
    return bytes(lo + hi)


# ---------------------------------------------------------------- 3D
def wire_model():
    """The H@N outline from src/wire/wire_logo.h: a list of closed contours (x, y), y down."""
    src = open(os.path.join(ROOT, 'src', 'wire', 'wire_logo.h')).read()
    lens = [int(x) for x in re.search(r'WIRE_LEN\[\] = \{([^}]*)\}', src).group(1).split(',')]
    pts = [(int(a) / 256.0, int(b) / 256.0) for a, b in re.findall(r'\{(-?\d+),(-?\d+)\}', src)]
    out, p = [], 0
    for n in lens:
        out.append(pts[p:p + n])
        p += n
    return out


def _draw(lines, w, h, scale=4):
    """Draw (x0, y0, x1, y1, color) lines; brighter colors win. Supersampled, then each
    output pixel takes the brightest color that covers enough of it."""
    im = Image.new('L', (w * scale, h * scale), 0)
    d = ImageDraw.Draw(im)
    for c in (1, 2, 3):
        for x0, y0, x1, y1, col in lines:
            if col == c:
                d.line([(x0 * scale, y0 * scale), (x1 * scale, y1 * scale)], fill=c, width=scale)
    a = np.asarray(im).reshape(h, scale, w, scale)
    out = np.zeros((h, w), np.uint8)
    for c in (1, 2, 3):
        cover = (a >= c).sum(axis=(1, 3))
        out[cover >= scale * scale * 0.3] = c
    return out


def wire_logo_frame(ay, w, h, ax=0.28):
    """One frame of the spinning wireframe H@N (back edges 1, sides 2, front edges 3)."""
    contours = wire_model()
    aspect = 684 / 256.0
    hpx = min(h * 0.80, w * 0.92 / aspect)
    fov, depth, dist = hpx * 6.0, 0.2, 6.0
    ca, sa, cb, sb = math.cos(ay), math.sin(ay), math.cos(ax), math.sin(ax)
    lines = []

    def proj(x, y, z):
        x2, z2 = x * ca - z * sa, x * sa + z * ca
        y2, z3 = y * cb - z2 * sb, y * sb + z2 * cb + dist
        return w / 2 + x2 * fov / z3, h / 2 + y2 * fov / z3

    for c in contours:
        n = len(c)
        f = [proj(x, y, -depth) for x, y in c]
        b = [proj(x, y, depth) for x, y in c]
        for i in range(n):
            j = (i + 1) % n
            lines.append((*b[i], *b[j], 1))
        for i in range(0, n, 4 if n > 20 else 1):
            lines.append((*f[i], *b[i], 2))
        for i in range(n):
            j = (i + 1) % n
            lines.append((*f[i], *f[j], 3))
    return _draw(lines, w, h)


CUBE_V = [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
CUBE_E = [(a, b) for a in range(8) for b in range(a + 1, 8)
          if sum(CUBE_V[a][k] != CUBE_V[b][k] for k in range(3)) == 1]


def cubes_frame(t, w, h):
    """Two wireframe cubes spinning opposite ways; nearer edges are brighter.
    t in 0..1 covers a quarter turn, which loops seamlessly (a cube looks the same
    every 90 degrees)."""
    lines = []
    for cx, spin, tilt, size in ((-0.25, 1, 0.55, 0.21), (0.25, -1, -0.45, 0.21)):
        ay = spin * t * math.pi / 2
        ca, sa, cb, sb = math.cos(ay), math.sin(ay), math.cos(tilt), math.sin(tilt)
        pts = []
        for x, y, z in CUBE_V:
            x2, z2 = x * ca - z * sa, x * sa + z * ca
            y2, z3 = y * cb - z2 * sb, y * sb + z2 * cb
            dist = 4.2
            s = size * h * 3.0 / (z3 + dist)
            pts.append((w / 2 + cx * w + x2 * s, h / 2 + y2 * s, z3))
        for a, b in CUBE_E:
            z = (pts[a][2] + pts[b][2]) / 2
            col = 3 if z < -0.35 else 2 if z < 0.35 else 1
            lines.append((pts[a][0], pts[a][1], pts[b][0], pts[b][1], col))
    return _draw(lines, w, h)


# ---------------------------------------------------------------- controller drawing
def pad_image(w, h, style='gb'):
    """A controller outline for the input test. Returns (image, {button: (x, y) center})."""
    s = 4
    im = Image.new('L', (w * s, h * s), 0)
    d = ImageDraw.Draw(im)
    S = lambda *v: [x * s for x in v]
    d.rounded_rectangle(S(1, 1, w - 2, h - 2), radius=10 * s, outline=1, width=s)
    cx, cy = 26, h // 2 - 2
    arm = 6
    centers = {'up': (cx, cy - 10), 'down': (cx, cy + 10), 'left': (cx - 10, cy), 'right': (cx + 10, cy)}
    d.rectangle(S(cx - arm, cy - 16, cx + arm, cy + 16), fill=2)
    d.rectangle(S(cx - 16, cy - arm, cx + 16, cy + arm), fill=2)
    if style == 'gb':
        ab = {'b': (w - 40, cy + 6), 'a': (w - 22, cy - 4)}
    else:
        ab = {'b': (w - 42, cy + 2), 'a': (w - 22, cy + 2)}
    for k, (x, y) in ab.items():
        d.ellipse(S(x - 8, y - 8, x + 8, y + 8), outline=3, width=s)
        centers[k] = (x, y)
    for k, x in (('select', w // 2 - 12), ('start', w // 2 + 12)):
        y = h - 14
        d.rounded_rectangle(S(x - 8, y - 3, x + 8, y + 3), radius=3 * s, fill=2)
        centers[k] = (x, y)
    a = np.asarray(im).reshape(h, s, w, s)
    out = np.zeros((h, w), np.uint8)
    for c in (1, 2, 3):
        out[(a >= c).sum(axis=(1, 3)) >= s * s * 0.35] = c
    return out, centers


def sprite_tiles():
    """Star (dim dot), star (bright plus), and a filled disk for pressed buttons."""
    dot = np.zeros((8, 8), np.uint8)
    dot[3:5, 3:5] = 2
    plus = np.zeros((8, 8), np.uint8)
    plus[3:5, 2:6] = 3
    plus[2:6, 3:5] = 3
    disk = np.zeros((8, 8), np.uint8)
    for y in range(8):
        for x in range(8):
            if (x - 3.5) ** 2 + (y - 3.5) ** 2 <= 12.5:
                disk[y, x] = 3
    return [dot, plus, disk]


def preview(img, scale=4, path=None):
    pal = [(14, 22, 34), (52, 73, 94), (134, 168, 196), (228, 236, 242)]
    a = np.asarray(img, np.uint8)
    rgb = np.array(pal, np.uint8)[a]
    im = Image.fromarray(rgb).resize((a.shape[1] * scale, a.shape[0] * scale), Image.NEAREST)
    if path:
        im.save(path)
    return im
