"""
Procedural H@N splash prototype (no stored image): letters are signed distance fields,
sky / stars / clouds are noise and math. Renders 320x240 frames at RGB565 + ordered
dither (what the ILI9341 shows), writes an animated GIF scaled 2x for viewing.

Style A "chrome": steel bevels, twinkling stars, shine sweep.
Style B "bronze": bronze bevel edges (menu colors), twinkling stars, shooting star, shine sweep.
"""
import sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
STYLE = sys.argv[1] if len(sys.argv) > 1 else "chrome"
OUT = sys.argv[2] if len(sys.argv) > 2 else f"/tmp/logo/proc_{STYLE}.gif"
FRAMES, MS = 60, 40

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

# ---------------- SDF primitives ----------------
def sd_box(x, y, cx, cy, hx, hy, r=0.0):
    qx = np.abs(x - cx) - hx + r
    qy = np.abs(y - cy) - hy + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r

def sd_ring(x, y, cx, cy, r, w):
    return np.abs(np.hypot(x - cx, y - cy) - r) - w

def sd_seg(x, y, ax, ay, bx, by, w):
    px, py = x - ax, y - ay
    dx, dy = bx - ax, by - ay
    t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1)
    return np.hypot(px - t * dx, py - t * dy) - w

U = np.minimum                    # union
def I(a, b): return np.maximum(a, b)       # intersection
def S(a, b): return np.maximum(a, -b)      # subtraction

def logo_sdf(x, y):
    # letters around a 100 px cap height, centered later by offsetting x/y
    st = 13          # stem half width
    hh = 47          # half height
    # H: stems with chamfered tops, cross bar
    Hs = U(sd_box(x, y, 42, 100, st, hh, 3), sd_box(x, y, 90, 100, st, hh, 3))
    Hs = U(Hs, sd_box(x, y, 66, 98, 12, 9, 2))
    # angled cuts on the outer top corners (metal style)
    Hs = S(Hs, sd_seg(x, y, 26, 46, 34, 54, 4))
    Hs = S(Hs, sd_seg(x, y, 106, 46, 98, 54, 4))
    # @: outer ring open at lower right, inner "a" ring + stem + tail into the ring
    cx, cy = 160, 100
    ring = sd_ring(x, y, cx, cy, 44, 8)
    ang = np.arctan2(y - cy, x - cx)                 # 0 = right, +down
    gap = (ang > -0.05) & (ang < 0.75)               # opening, lower right
    ring = np.where(gap & (np.hypot(x - cx, y - cy) > 30), np.maximum(ring, 4.0), ring)
    inner = sd_ring(x, y, cx - 4, cy + 1, 14, 6.5)
    stem = sd_box(x, y, cx + 16, cy + 1, 6, 20, 2)
    tail = sd_seg(x, y, cx + 16, cy + 19, cx + 37, cy + 15, 6)
    At = U(U(ring, inner), U(stem, tail))
    # N: stems + diagonal clipped to the letter height
    N1 = U(sd_box(x, y, 230, 100, st, hh, 3), sd_box(x, y, 278, 100, st, hh, 3))
    diag = I(sd_seg(x, y, 230, 54, 278, 146, 13), np.abs(y - 100) - hh)
    N1 = S(N1, sd_seg(x, y, 214, 46, 222, 54, 4))
    N1 = S(N1, sd_seg(x, y, 294, 154, 286, 146, 4))
    Ns = U(N1, diag)
    return U(U(Hs, At), Ns)

# center the logo
OX, OY = 0.0, 0.0
d0 = logo_sdf(xx + 0, yy + 0)
ys_in, xs_in = np.where(d0 < 0)
OX = (xs_in.min() + xs_in.max()) / 2 - W / 2
OY = (ys_in.min() + ys_in.max()) / 2 - 92
LX, LY = xx + OX, yy + OY
d = logo_sdf(LX, LY)

# extrusion (3D depth): the shape pushed down-right
EX, EY, DEPTH = 0.75, 1.0, 9
ext = np.full_like(d, 1e9)
ext_k = np.zeros_like(d)
for k in range(1, DEPTH + 1):
    dk = logo_sdf(LX - EX * k, LY - EY * k)
    hit = (dk < 0) & (ext > 0)
    ext_k = np.where(hit, k, ext_k)
    ext = np.minimum(ext, dk)
outline = np.minimum(d, ext)

# bevel normal from the distance field gradient
gy_, gx_ = np.gradient(d)
B = 7.0
hgt = np.clip(-d / B, 0, 1)
slope = (1 - hgt) * 1.6
nx, ny, nz = -gx_ * slope, -gy_ * slope, np.ones_like(d)
nl = np.sqrt(nx * nx + ny * ny + nz * nz)
nx, ny, nz = nx / nl, ny / nl, nz / nl
L = np.array([-0.55, -0.7, 0.45]); L = L / np.linalg.norm(L)
lam = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)

# ---------------- background ----------------
def value_noise(x, y, seed):
    rng = np.random.default_rng(seed)
    g = rng.random((64, 64)).astype(np.float32)
    xi, yi = np.floor(x).astype(int), np.floor(y).astype(int)
    xf, yf = x - xi, y - yi
    xf = xf * xf * (3 - 2 * xf); yf = yf * yf * (3 - 2 * yf)
    a = g[yi % 64, xi % 64]; b = g[yi % 64, (xi + 1) % 64]
    c = g[(yi + 1) % 64, xi % 64]; e = g[(yi + 1) % 64, (xi + 1) % 64]
    return (a * (1 - xf) + b * xf) * (1 - yf) + (c * (1 - xf) + e * xf) * yf

def fbm(x, y):
    v, amp, f = 0, 0.5, 1
    for o in range(5):
        v = v + amp * value_noise(x * f, y * f, 11 + o)
        amp *= 0.5; f *= 2.03
    return v

t_ = yy / H
sky = np.stack([8 + 18 * t_, 12 + 26 * t_, 24 + 42 * t_], -1)
# faint moon glow top right
glow = np.exp(-((xx - 262) ** 2 + (yy - 26) ** 2) / (2 * 55 ** 2))[..., None]
sky = sky + glow * np.array([40, 50, 70])
# clouds: bottom band, lit from above
cn = fbm(xx / 46.0, yy / 22.0)
band = np.clip((yy - 150) / 60.0, 0, 1)
dens = np.clip((cn - 0.55 + 0.55 * band) * 2.6, 0, 1) * np.clip((yy - 128) / 30.0, 0, 1)
cn_up = fbm(xx / 46.0, (yy - 3) / 22.0)
dens_up = np.clip((cn_up - 0.55 + 0.55 * np.clip((yy - 153) / 60.0, 0, 1)) * 2.6, 0, 1)
light = np.clip((dens - dens_up) * 3 + 0.4, 0, 1)
cloud_col = np.stack([45 + 95 * light, 55 + 100 * light, 78 + 105 * light], -1)
bg = sky * (1 - dens[..., None]) + cloud_col * dens[..., None]
# higher wisps behind the logo
wisp = np.clip((fbm(xx / 70.0 + 9, yy / 14.0) - 0.62) * 3, 0, 1) * np.clip(1 - np.abs(yy - 95) / 60, 0, 1) * 0.35
bg = bg * (1 - wisp[..., None]) + np.array([70, 82, 104]) * wisp[..., None]

# stars (fixed seed: the same every boot)
rng = np.random.default_rng(7)
NS = 70
sx = rng.integers(2, W - 2, NS); sy = (rng.random(NS) ** 1.6 * 150).astype(int) + 2
sb = 0.4 + 0.6 * rng.random(NS); sph = rng.random(NS); sf = 0.6 + 1.4 * rng.random(NS)
big = rng.random(NS) < 0.12

# ---------------- letter colors ----------------
def steel_env(v):
    """chrome reflection by height on the letter face: sky, bright horizon, dark ground, bounce"""
    stops = [(0.00, (200, 214, 230)), (0.30, (120, 138, 160)), (0.47, (235, 242, 250)),
             (0.52, (52, 60, 74)), (0.78, (88, 100, 118)), (1.00, (150, 165, 182))]
    out = np.zeros(v.shape + (3,), np.float32)
    for (a, ca), (b, cb) in zip(stops[:-1], stops[1:]):
        m = (v >= a) & (v <= b)
        f = ((v - a) / (b - a))[..., None]
        out[m] = (np.array(ca) * (1 - f) + np.array(cb) * f)[m]
    return out

ys_l = LY[d < 0]
top, bot = ys_l.min(), ys_l.max()
face_v = np.clip((LY - top) / (bot - top), 0, 1)
face = steel_env(face_v)
if STYLE == "bronze":
    bevel_lo, bevel_hi = np.array([70, 52, 30]), np.array([236, 206, 150])
else:
    bevel_lo, bevel_hi = np.array([34, 40, 52]), np.array([240, 246, 255])
bev = bevel_lo + (bevel_hi - bevel_lo) * lam[..., None] ** 1.3
edge = (1 - hgt)[..., None]
letter = face * (1 - edge * 0.9) + bev * edge * 0.9
side = np.stack([30 + 0 * ext_k, 36 + 0 * ext_k, 46 + 0 * ext_k], -1) + (DEPTH - ext_k)[..., None] * np.array([5, 6, 7])
if STYLE == "bronze":
    side = side * np.array([1.15, 1.0, 0.8])

# ---------------- text ----------------
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 21)
tm = Image.new("L", (W, H), 0)
ImageDraw.Draw(tm).text((W / 2, 205), "hackatnight.com", font=font, anchor="mm", fill=255)
to = Image.new("L", (W, H), 0)
ImageDraw.Draw(to).text((W / 2, 205), "hackatnight.com", font=font, anchor="mm", fill=255, stroke_width=2, stroke_fill=255)
tmask = np.asarray(tm, np.float32)[..., None] / 255
omask = np.asarray(to, np.float32)[..., None] / 255
tgrad = np.clip((yy - 196) / 18, 0, 1)[..., None]
tcol = np.array([235, 242, 250]) * (1 - tgrad) + (np.array([150, 120, 80]) if STYLE == "bronze" else np.array([110, 128, 150])) * tgrad

BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) + 0.5) / 16 - 0.5
by = np.tile(BAYER, (H // 4, W // 4))

def to565(img):
    q = np.zeros_like(img)
    for c, bits in enumerate((5, 6, 5)):
        st = 255 / (2 ** bits - 1)
        q[..., c] = np.clip(np.round(img[..., c] / st + by * 0.8), 0, 2 ** bits - 1) * st
    return np.clip(q, 0, 255).astype(np.uint8)

frames = []
for f in range(FRAMES):
    t = f / FRAMES
    img = bg.copy()
    # stars twinkle
    for i in range(NS):
        b = sb[i] * (0.35 + 0.65 * (0.5 + 0.5 * math.sin(2 * math.pi * (t * sf[i] * 2 + sph[i]))))
        c = np.array([200, 215, 245]) * b
        x, y = sx[i], sy[i]
        img[y, x] = np.maximum(img[y, x], c)
        if big[i] and b > 0.75:
            for dx_, dy_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                img[y + dy_, x + dx_] = np.maximum(img[y + dy_, x + dx_], c * 0.55)
    # shooting star (style B)
    if STYLE == "bronze" and 0.15 < t < 0.45:
        u = (t - 0.15) / 0.30
        hx, hy = 40 + 200 * u, 18 + 40 * u
        for k in range(26):
            px_, py_ = int(hx - k * 2.0), int(hy - k * 0.4)
            if 0 <= px_ < W and 0 <= py_ < H:
                img[py_, px_] = np.maximum(img[py_, px_], np.array([255, 240, 210]) * (1 - k / 26) * (1 - u * 0.6))
    # drop shadow / dark outline, extrusion, letters
    sh = np.clip(1 - (outline + 0.5) / 3, 0, 1)[..., None]
    img = img * (1 - sh * 0.85)
    img = np.where((ext < 0)[..., None] & (d >= 0)[..., None], side, img)
    ins = np.clip(0.5 - d, 0, 1)[..., None]           # 1 px anti-aliasing
    # shine: diagonal band sweeping over the chrome in the first 60% of the loop
    s = -40 + (W + 80) * (t / 0.6)
    u_ = xx + 0.55 * yy - 0.55 * 100
    shine = np.exp(-((u_ - s) / 11.0) ** 2)[..., None] * (1.0 if t < 0.6 else 0.0)
    lit = letter + shine * (np.array([255, 255, 255]) - letter) * (0.75 + 0.25 * edge)
    img = img * (1 - ins) + lit * ins
    # text with dark outline and its own shine
    img = img * (1 - omask * 0.85)
    tsh = np.exp(-((xx + 0.55 * yy - 0.55 * 205 - (s - 30)) / 7.0) ** 2)[..., None] * (1.0 if t < 0.6 else 0.0)
    img = img * (1 - tmask) + (tcol + tsh * (255 - tcol) * 0.8) * tmask
    frames.append(Image.fromarray(to565(img)))

big_frames = [fr.resize((W * 2, H * 2), Image.NEAREST) for fr in frames]
pal = big_frames[int(FRAMES * 0.3)].quantize(256, method=Image.Quantize.MEDIANCUT)
q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in big_frames]
q[0].save(OUT, save_all=True, append_images=q[1:], duration=MS, loop=0, optimize=False)
frames[int(FRAMES * 0.25)].resize((W * 2, H * 2), Image.NEAREST).save(OUT.replace(".gif", "_still.png"))
print("wrote", OUT)
