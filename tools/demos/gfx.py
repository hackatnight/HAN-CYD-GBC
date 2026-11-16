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


# ---------------------------------------------------------------- vector shapes with a small H@N
# Colors: 1 = far side of the shape, 2 = near side, 3 = the small H@N (recolored separately).
def _rot(p, ay=0.0, ax=0.0):
    x, y, z = p
    ca, sa = math.cos(ay), math.sin(ay)
    x, z = x * ca - z * sa, x * sa + z * ca
    cb, sb = math.cos(ax), math.sin(ax)
    y, z = y * cb - z * sb, y * sb + z * cb
    return x, y, z


def _proj(p, w, h, scale, dist=5.0, cx=0.0, cy=0.0):
    x, y, z = p
    s = scale * dist / (z + dist)
    return w / 2 + (x + cx) * s, h / 2 + (y + cy) * s


def _han_strokes():
    """A simple stroke H@N (polylines, x -1.5..1.5, y -0.5..0.5) that stays readable when small."""
    S = []
    S.append([(-1.5, -0.5), (-1.5, 0.5)])
    S.append([(-0.95, -0.5), (-0.95, 0.5)])
    S.append([(-1.5, 0.0), (-0.95, 0.0)])
    ring = [(0.5 * math.cos(a), 0.5 * math.sin(a)) for a in
            [math.radians(d) for d in range(20, 360, 30)] + [math.radians(380)]]
    S.append([(x, y) for x, y in ring[:-1]] + [(0.5 * math.cos(math.radians(20)), 0.18)])
    S.append([(0.2 * math.cos(math.radians(d)), 0.2 * math.sin(math.radians(d))) for d in range(0, 361, 45)])
    S.append([(0.2, -0.2), (0.2, 0.18), (0.47, 0.18)])
    S.append([(0.95, 0.5), (0.95, -0.5), (1.5, 0.5), (1.5, -0.5)])
    return S


def small_han(w, h, scale, center=(0.0, 0.0, 0.0), size=0.62, ay=0.0, ax=0.18, dist=5.0, depth=0.12):
    """Line list for a small extruded stroke H@N (color 3) centred at a view-space point."""
    lines = []
    k = size / 1.5
    cxp, cyp, czp = center
    def P(x, y, z):
        x, y, z = _rot((x * k, y * k, z), ay, ax)
        return _proj((x + cxp, y + cyp, z + czp), w, h, scale, dist)
    for st in _han_strokes():
        f = [P(x, y, -depth) for x, y in st]
        b = [P(x, y, depth) for x, y in st]
        for i in range(len(st) - 1):
            lines.append((*f[i], *f[i + 1], 3))
            lines.append((*b[i], *b[i + 1], 3))
        lines.append((*f[0], *b[0], 3))
        lines.append((*f[-1], *b[-1], 3))
    return lines


def _edges(verts, edges, w, h, scale, dist=5.0, split=0.0):
    out = []
    pts = [_proj(v, w, h, scale, dist) for v in verts]
    for a, b in edges:
        z = (verts[a][2] + verts[b][2]) / 2
        out.append((*pts[a], *pts[b], 2 if z < split else 1))
    return out


def _dots(points, w, h, scale, dist=5.0, r=1.1):
    """Round dots for the dot ball, drawn as tiny crosses so they survive the 0.3 cover rule."""
    out = []
    for p in points:
        x, y = _proj(p, w, h, scale, dist)
        c = 2 if p[2] < 0 else 1
        rr = r * (1.25 if c == 2 else 0.8)
        out.append((x - rr, y, x + rr, y, c))
        out.append((x, y - rr, x, y + rr, c))
    return out


def sphere_frame(i, n, w, h, rings=7, per=16):
    """Dot ball: latitude rings of dots; it turns one dot spacing every n/4 frames... the
    whole loop (n frames) turns 4 spacings, and the H@N inside nods once."""
    t = i / n
    ay = 4 * t * 2 * math.pi / per
    pts = []
    for r in range(rings):
        lat = math.pi * (r + 1) / (rings + 1) - math.pi / 2
        for k in range(per):
            lon = 2 * math.pi * k / per + (math.pi / per if r % 2 else 0)
            p = (math.cos(lat) * math.cos(lon), math.sin(lat), math.cos(lat) * math.sin(lon))
            pts.append(_rot(p, ay, 0.42))
    pts += [_rot((0, -1, 0), 0, 0.42), _rot((0, 1, 0), 0, 0.42)]
    scale = min(w, h) * 0.44
    lines = _dots(pts, w, h, scale)
    lines += small_han(w, h, scale, size=0.58, ay=0.55 * math.sin(2 * math.pi * t), ax=0.1)
    return _draw(lines, w, h)


def pyramid_frame(i, n, w, h):
    """Square pyramid turning a full circle, with a small H@N orbiting it the other way."""
    t = i / n
    ay = 2 * math.pi * t
    verts = [(-0.85, 0.65, -0.85), (0.85, 0.65, -0.85), (0.85, 0.65, 0.85), (-0.85, 0.65, 0.85), (0, -0.95, 0)]
    edges = [(0, 1), (1, 2), (2, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4)]
    v = [_rot(p, ay, 0.38) for p in verts]
    scale = min(w * 0.27, h * 0.42)
    lines = _edges(v, edges, w, h, scale)
    th = -2 * math.pi * t
    c = _rot((1.1 * math.cos(th), 0.2, 1.1 * math.sin(th)), 0, 0.38)
    lines += small_han(w, h, scale, center=c, size=0.45, ay=0.5 * math.sin(th), ax=0.15)
    return _draw(lines, w, h)


def dcube_frame(yaw, pitch, w, h):
    """The D-pad cube at one yaw/pitch, with the small H@N standing still inside it."""
    v = [_rot(_rot(p, yaw, 0.0), 0.0, pitch) for p in CUBE_V]
    scale = min(w, h) * 0.27
    lines = _edges(v, CUBE_E, w, h, scale)
    lines += small_han(w, h, scale, size=0.85, ay=0.0, ax=0.0)
    return _draw(lines, w, h)


TESS_V = [(x, y, z, q) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1) for q in (-1, 1)]
TESS_E = [(a, b) for a in range(16) for b in range(a + 1, 16)
          if sum(TESS_V[a][k] != TESS_V[b][k] for k in range(4)) == 1]


def tesseract_frame(i, n, w, h):
    """A 4D hypercube turning in the XW and YZ planes (a quarter turn loops), seen in 3D,
    with the small H@N swinging in its core."""
    t = i / n
    a = t * math.pi / 2
    ca, sa = math.cos(a), math.sin(a)
    out = []
    for x, y, z, q in TESS_V:
        x, q = x * ca - q * sa, x * sa + q * ca
        y, z = y * ca - z * sa, y * sa + z * ca
        k = 2.6 / (3.6 - q)                 # 4D -> 3D perspective
        out.append(_rot((x * k, y * k, z * k), 0.6, 0.45))
    scale = min(w, h) * 0.25
    lines = _edges(out, TESS_E, w, h, scale)
    lines += small_han(w, h, scale, size=0.8, ay=0.6 * math.sin(2 * math.pi * t), ax=0.12)
    return _draw(lines, w, h)
