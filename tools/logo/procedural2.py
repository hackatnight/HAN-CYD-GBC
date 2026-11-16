"""
Procedural H@N splash, v2. Everything is code: letters are signed distance fields,
the 3D depth is a stack of slices shrinking toward a vanishing point (darker with depth),
sky / mist / stars are noise and math. 320x240, RGB565 + ordered dither, GIF at 2x.

Timeline: letters fly in from far away (0-1.1 s), "hackatnight.com" types itself with a
blinking cursor, a shine sweeps across the chrome, mist drifts and stars twinkle throughout.
"""
import sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

STYLE = sys.argv[1] if len(sys.argv) > 1 else "chrome"
OUT = sys.argv[2] if len(sys.argv) > 2 else f"/tmp/logo/proc2_{STYLE}.gif"
W, H = 320, 240
FPS = 20
FRAMES = 96                       # 4.8 s loop
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

# ---------------- SDF primitives ----------------
def sd_box(x, y, cx, cy, hx, hy, r=0.0):
    qx = np.abs(x - cx) - hx + r
    qy = np.abs(y - cy) - hy + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r

def sd_seg(x, y, ax, ay, bx, by, w):
    px, py = x - ax, y - ay
    dx, dy = bx - ax, by - ay
    t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1)
    return np.hypot(px - t * dx, py - t * dy) - w

def sd_arc(x, y, cx, cy, r, w, a0, a1):
    """ring from angle a0 to a1 (radians, +y down), round ends"""
    ang = np.arctan2(y - cy, x - cx)
    am = (a0 + a1) / 2
    da = (ang - am + np.pi) % (2 * np.pi) - np.pi
    inside = np.abs(da) <= (a1 - a0) / 2
    ring = np.abs(np.hypot(x - cx, y - cy) - r) - w
    e0 = np.hypot(x - (cx + r * np.cos(a0)), y - (cy + r * np.sin(a0))) - w
    e1 = np.hypot(x - (cx + r * np.cos(a1)), y - (cy + r * np.sin(a1))) - w
    return np.where(inside, ring, np.minimum(e0, e1))

def sd_bezier(x, y, p0, p1, p2, w, n=8):
    d = None
    pts = [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
            (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in np.linspace(0, 1, n + 1)]
    for a, b in zip(pts[:-1], pts[1:]):
        s = sd_seg(x, y, a[0], a[1], b[0], b[1], w)
        d = s if d is None else np.minimum(d, s)
    return d

U = np.minimum
def S(a, b): return np.maximum(a, -b)
def I(a, b): return np.maximum(a, b)

ST, HH = 13, 47
def logo_sdf(x, y):
    # H
    Hs = U(sd_box(x, y, 42, 100, ST, HH, 3), sd_box(x, y, 90, 100, ST, HH, 3))
    Hs = U(Hs, sd_box(x, y, 66, 98, 12, 9, 2))
    Hs = S(Hs, sd_seg(x, y, 26, 46, 34, 54, 4))
    Hs = S(Hs, sd_seg(x, y, 106, 46, 98, 54, 4))
    # @ : outer arc open at the lower right, bowl, stem, and a curve from the stem foot
    cx, cy = 160, 100
    outer = sd_arc(x, y, cx, cy, 44, 7.5, 1.05, 1.05 + 2 * np.pi - 0.78)   # leaves a gap from ~20 to ~60 deg
    bowl = np.abs(np.hypot(x - (cx - 5), y - (cy + 2)) - 15) - 6.5
    stem = sd_box(x, y, cx + 14, cy + 2, 6, 20, 3)
    foot = sd_bezier(x, y, (cx + 14, cy + 18), (cx + 22, cy + 32), (cx + 44 * math.cos(0.27), cy + 44 * math.sin(0.27)), 6.5)
    At = U(U(outer, bowl), U(stem, foot))
    # N
    N1 = U(sd_box(x, y, 230, 100, ST, HH, 3), sd_box(x, y, 278, 100, ST, HH, 3))
    diag = I(sd_seg(x, y, 230, 54, 278, 146, 13), np.abs(y - 100) - HH)
    Ns = U(N1, diag)
    Ns = S(Ns, sd_seg(x, y, 214, 46, 222, 54, 4))
    Ns = S(Ns, sd_seg(x, y, 294, 154, 286, 146, 4))
    return U(U(Hs, At), Ns)

# logo space -> screen: center of the logo goes to (160, 92)
LCX, LCY = 160.0, 100.0
SCX, SCY = 160.0, 90.0
VPX, VPY = 168.0, 205.0           # vanishing point (below the logo: we look down on it)
DEPTH, DSTEP = 30, 0.0115         # slices, shrink per slice

def sample(scale):
    """distance field of the logo drawn at `scale` around the vanishing point"""
    lx = (xx - VPX) / scale + VPX - SCX + LCX
    ly = (yy - VPY) / scale + VPY - SCY + LCY
    return logo_sdf(lx, ly) * scale, ly

def render_logo(g):
    """g = fly-in scale (0..1]. Returns front distance, front y (logo space), depth slice index map"""
    d, ly = sample(g)
    kmap = np.zeros_like(d)
    body = np.full_like(d, 1e9)
    for k in range(DEPTH, 0, -1):                 # back to front: nearer slices overwrite
        dk, _ = sample(g * (1 - DSTEP * k))
        hit = dk < 0.5
        kmap = np.where(hit, k, kmap)
        body = np.minimum(body, dk)
    return d, ly, kmap, body

# ---------------- noise ----------------
_rng = np.random.default_rng(3)
_G = [_rng.random((64, 64)).astype(np.float32) for _ in range(6)]
def vnoise(x, y, o):
    g = _G[o]
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    xf, yf = x - xi, y - yi
    xf = xf * xf * (3 - 2 * xf); yf = yf * yf * (3 - 2 * yf)
    a = g[yi % 64, xi % 64]; b = g[yi % 64, (xi + 1) % 64]
    c = g[(yi + 1) % 64, xi % 64]; e = g[(yi + 1) % 64, (xi + 1) % 64]
    return (a * (1 - xf) + b * xf) * (1 - yf) + (c * (1 - xf) + e * xf) * yf

def fbm(x, y, oct=4):
    v, amp, f = 0, 0.5, 1
    for o in range(oct):
        v = v + amp * vnoise(x * f, y * f, o)
        amp *= 0.5; f *= 2.07
    return v

# ---------------- static sky ----------------
t_ = yy / H
sky = np.stack([6 + 16 * t_, 10 + 24 * t_, 22 + 40 * t_], -1)
glow = np.exp(-((xx - 262) ** 2 + (yy - 24) ** 2) / (2 * 60 ** 2))[..., None]
sky = sky + glow * np.array([34, 44, 64])

rng = np.random.default_rng(7)
NS = 130
sx = rng.integers(2, W - 2, NS); sy = (rng.random(NS) ** 1.4 * 200).astype(int) + 2
sb = 0.35 + 0.65 * rng.random(NS); sph = rng.random(NS); sf = 0.5 + 1.5 * rng.random(NS)
big = rng.random(NS) < 0.12

# ---------------- colors ----------------
def steel_env(v):
    stops = [(0.00, (200, 214, 230)), (0.30, (120, 138, 160)), (0.47, (235, 242, 250)),
             (0.52, (52, 60, 74)), (0.78, (88, 100, 118)), (1.00, (150, 165, 182))]
    out = np.zeros(v.shape + (3,), np.float32)
    for (a, ca), (b, cb) in zip(stops[:-1], stops[1:]):
        m = (v >= a) & (v <= b)
        f = ((v - a) / (b - a))[..., None]
        out[m] = (np.array(ca) * (1 - f) + np.array(cb) * f)[m]
    return out

if STYLE == "bronze":
    BEV_LO, BEV_HI = np.array([70, 52, 30]), np.array([236, 206, 150])
    SIDE = np.array([118, 96, 66])
else:
    BEV_LO, BEV_HI = np.array([34, 40, 52]), np.array([240, 246, 255])
    SIDE = np.array([96, 110, 132])
Lv = np.array([-0.55, -0.7, 0.45]); Lv = Lv / np.linalg.norm(Lv)

# ---------------- text ----------------
TEXT = "hackatnight.com"
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 19)
cw = font.getbbox("M")[2]
TX0 = int(W / 2 - cw * len(TEXT) / 2)
TY = 196

def text_masks(n, cursor):
    m = Image.new("L", (W, H), 0); o = Image.new("L", (W, H), 0)
    s = TEXT[:n]
    ImageDraw.Draw(m).text((TX0, TY), s, font=font, anchor="lm", fill=255)
    ImageDraw.Draw(o).text((TX0, TY), s, font=font, anchor="lm", fill=255, stroke_width=2, stroke_fill=255)
    if cursor:
        x = TX0 + cw * n
        ImageDraw.Draw(m).rectangle((x + 1, TY - 9, x + cw - 2, TY + 8), fill=255)
    return np.asarray(m, np.float32)[..., None] / 255, np.asarray(o, np.float32)[..., None] / 255

BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) + 0.5) / 16 - 0.5
by = np.tile(BAYER, (H // 4, W // 4))
def to565(img):
    q = np.zeros_like(img)
    for c, bits in enumerate((5, 6, 5)):
        st = 255 / (2 ** bits - 1)
        q[..., c] = np.clip(np.round(img[..., c] / st + by * 0.8), 0, 2 ** bits - 1) * st
    return np.clip(q, 0, 255).astype(np.uint8)

def ease_out(u): return 1 - (1 - u) ** 3

cache = None
frames = []
for f in range(FRAMES):
    T = f / FPS                                   # seconds
    # ---- background: sky, stars, drifting mist ----
    img = sky.copy()
    for i in range(NS):
        b = sb[i] * (0.3 + 0.7 * (0.5 + 0.5 * math.sin(2 * math.pi * (T * sf[i] * 0.8 + sph[i]))))
        c = np.array([200, 215, 245]) * b
        x, y = sx[i], sy[i]
        img[y, x] = np.maximum(img[y, x], c)
        if big[i] and b > 0.72:
            for dx_, dy_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                img[y + dy_, x + dx_] = np.maximum(img[y + dy_, x + dx_], c * 0.5)
    if STYLE == "bronze" and 1.6 < T < 2.6:     # shooting star
        u = (T - 1.6) / 1.0
        hx, hy = 30 + 230 * u, 14 + 34 * u
        for k in range(30):
            px_, py_ = int(hx - k * 2.2), int(hy - k * 0.33)
            if 0 <= px_ < W and 0 <= py_ < H:
                img[py_, px_] = np.maximum(img[py_, px_], np.array([255, 240, 210]) * (1 - k / 30) * (1 - 0.5 * u))
    # mist: two layers drifting at different speeds, thicker toward the bottom
    m1 = fbm(xx / 36.0 + T * 0.9, yy / 9.0 + 0.2 * np.sin(xx / 50.0 + T * 0.8))
    m2 = fbm(xx / 20.0 - T * 0.6 + 7, yy / 7.0 + 3)
    low = np.clip((yy - 95) / 120.0, 0, 1)
    mist = np.clip((m1 * 0.6 + m2 * 0.5 - 0.45) * 1.6, 0, 1) * (0.06 + 0.45 * low) + 0.05 * low
    mist = np.clip(mist, 0, 0.55)[..., None]
    img = img * (1 - mist) + np.array([150, 165, 190]) * mist

    # ---- logo: fly in, then static (cached) ----
    g = 0.04 + 0.96 * ease_out(min(1.0, T / 1.1))
    if g < 1.0 or cache is None:
        d, ly, kmap, body = render_logo(g)
        gy_, gx_ = np.gradient(d)
        if g >= 1.0:
            cache = (d, ly, kmap, body, gx_, gy_)
    else:
        d, ly, kmap, body, gx_, gy_ = cache
    bright = min(1.0, 0.25 + 0.75 * ease_out(min(1.0, T / 1.1)))   # comes out of the dark
    # shadow around the whole shape
    sh = np.clip(1 - (np.minimum(d, body) + 0.5) / 3.5, 0, 1)[..., None]
    img = img * (1 - sh * 0.8)
    # depth slices: darker the deeper
    depth = (kmap / DEPTH)[..., None]
    side = SIDE * (1 - depth) ** 1.6 + np.array([6, 7, 10]) * depth
    img = np.where(((kmap > 0) & (d >= 0))[..., None], side * bright, img)
    # front face: chrome by height + lit bevel
    ys_l = ly[d < 0]
    if ys_l.size:
        top, bot = LCY - HH, LCY + HH
        face = steel_env(np.clip((ly - top) / (bot - top), 0, 1))
        B = 7.0 * g
        hgt = np.clip(-d / max(B, 0.5), 0, 1)
        slope = (1 - hgt) * 1.6
        nx, ny, nz = -gx_ * slope, -gy_ * slope, np.ones_like(d)
        nl = np.sqrt(nx * nx + ny * ny + nz * nz)
        lam = np.clip((nx * Lv[0] + ny * Lv[1] + nz * Lv[2]) / nl, 0, 1)
        bev = BEV_LO + (BEV_HI - BEV_LO) * lam[..., None] ** 1.3
        edge = (1 - hgt)[..., None]
        letter = face * (1 - edge * 0.9) + bev * edge * 0.9
        # shine sweep (2.3 - 3.2 s)
        if 2.3 < T < 3.2:
            s = -40 + (W + 80) * (T - 2.3) / 0.9
            shine = np.exp(-((xx + 0.55 * yy - 0.55 * 90 - s) / 11.0) ** 2)[..., None]
            letter = letter + shine * (255 - letter) * (0.75 + 0.25 * edge)
        ins = np.clip(0.5 - d, 0, 1)[..., None]
        img = img * (1 - ins) + letter * bright * ins

    # ---- hackatnight.com: types itself, then the cursor blinks ----
    if T >= 1.1:
        n = min(len(TEXT), int((T - 1.1) / 0.075) + 1)
        typing = n < len(TEXT)
        cursor = typing or (int(T * 2.5) % 2 == 0)
        tm, om = text_masks(n, cursor)
        img = img * (1 - om * 0.8)
        tg = np.clip((yy - (TY - 9)) / 18, 0, 1)[..., None]
        tcol = np.array([235, 242, 250]) * (1 - tg) + (np.array([170, 135, 90]) if STYLE == "bronze" else np.array([120, 140, 165])) * tg
        img = img * (1 - tm) + tcol * tm
    frames.append(Image.fromarray(to565(img)))

big_frames = [fr.resize((W * 3 // 2, H * 3 // 2), Image.BILINEAR) for fr in frames]
pal_src = Image.new("RGB", (W * 3 // 2, H * 3))
pal_src.paste(big_frames[30], (0, 0)); pal_src.paste(big_frames[52], (0, H * 3 // 2))
pal = pal_src.quantize(256, method=Image.Quantize.MEDIANCUT)
q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in big_frames]
q[0].save(OUT, save_all=True, append_images=q[1:], duration=int(1000 / FPS), loop=0, optimize=False)
for i in (8, 40, 52, 80):
    frames[i].resize((W * 2, H * 2), Image.NEAREST).save(OUT.replace(".gif", f"_f{i}.png"))
print("wrote", OUT)
