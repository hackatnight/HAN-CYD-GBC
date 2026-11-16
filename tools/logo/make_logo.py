import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

S = 4                       # supersampling
W, H = 320*S, 140*S         # final 320x140
img_mask = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(img_mask)

def P(pts, ox, oy, sx, sy):
    return [(ox + x*sx, oy + y*sy) for x, y in pts]

# --- glyphs in unit coords (0..1 wide-ish, 0..1 tall), spiky "metal" style ---
def glyph_H(ox, oy, sx, sy):
    stem = [(-0.10,-0.16),(0.10,0.02),(0.30,0.00),(0.36,-0.10),(0.30,0.10),(0.27,0.90),(0.34,1.12),(0.12,0.98),(-0.08,1.06),(0.03,0.90),(0.05,0.12)]
    d.polygon(P(stem, ox, oy, sx, sy), fill=255)
    stem2 = [(0.64,-0.10),(0.72,0.02),(0.92,0.00),(1.12,-0.16),(0.97,0.12),(0.95,0.90),(1.10,1.06),(0.90,0.98),(0.68,1.12),(0.74,0.90),(0.70,0.10)]
    d.polygon(P(stem2, ox, oy, sx, sy), fill=255)
    bar = [(0.22,0.42),(0.50,0.30),(0.80,0.42),(0.80,0.58),(0.50,0.72),(0.22,0.58)]
    d.polygon(P(bar, ox, oy, sx, sy), fill=255)

def glyph_N(ox, oy, sx, sy):
    l = [(-0.10,-0.12),(0.10,0.02),(0.30,0.04),(0.27,0.90),(0.36,1.12),(0.12,0.98),(-0.08,1.06),(0.04,0.90),(0.05,0.12)]
    r = [(0.68,-0.12),(0.90,0.00),(1.12,-0.16),(0.96,0.10),(0.95,0.86),(1.04,1.12),(0.78,1.00),(0.72,0.95),(0.70,0.10)]
    diag = [(0.05,0.04),(0.30,0.02),(0.62,0.40),(0.80,0.30),(0.70,0.52),(0.95,0.80),(0.96,1.02),(0.74,0.98),(0.10,0.24)]
    for g in (l, r, diag): d.polygon(P(g, ox, oy, sx, sy), fill=255)

def glyph_at(ox, oy, sx, sy):
    # ring (outer - inner) with a blade tail, plus inner "a" bowl
    m = Image.new('L', (W, H), 0); md = ImageDraw.Draw(m)
    cx, cy = ox + 0.52*sx, oy + 0.50*sy
    rx, ry = 0.62*sx, 0.60*sy
    md.ellipse([cx-rx, cy-ry, cx+rx, cy+ry], fill=255)
    md.ellipse([cx-rx*0.70, cy-ry*0.68, cx+rx*0.70, cy+ry*0.68], fill=0)
    # inner bowl
    br = 0.24
    md.ellipse([cx-br*sx*1.05, cy-br*sy, cx+br*sx*0.75, cy+br*sy], fill=255)
    md.ellipse([cx-br*sx*0.55, cy-br*sy*0.48, cx+br*sx*0.30, cy+br*sy*0.48], fill=0)
    # stem of the a, running into the ring
    md.polygon(P([(0.66,0.22),(0.84,0.20),(0.80,0.80),(0.66,0.74)], ox, oy, sx, sy), fill=255)
    # cut a gap bottom-right where the blade tail exits
    md.polygon(P([(0.80,0.70),(1.25,0.62),(1.25,1.15),(0.88,0.98)], ox, oy, sx, sy), fill=0)
    # blade tail
    md.polygon(P([(0.80,0.70),(0.98,0.74),(1.30,0.98),(0.92,0.90),(0.70,0.80)], ox, oy, sx, sy), fill=255)
    # top spike
    md.polygon(P([(0.38,-0.08),(0.20,-0.24),(0.58,-0.06)], ox, oy, sx, sy), fill=255)
    img_mask.paste(255, (0, 0), m)

sx, sy = 74*S, 76*S
top = 14*S
glyph_H(22*S, top, sx, sy)
glyph_at(124*S, top, sx*0.92, sy)
glyph_N(222*S, top, sx, sy)

mask = img_mask
# --- 3D extrusion (down-right), dark navy getting darker with depth ---
canvas = np.zeros((H, W, 3), np.float32)
depth = 9*S
m = np.array(mask, np.float32)/255.0
for i in range(depth, 0, -1):
    sh = np.zeros_like(m)
    sh[i:, i//2:] = m[:-i, :W-(i//2)] if i//2 > 0 else m[:-i, :]
    t = i/depth
    col = np.array([34, 30, 72])*(1.0-0.65*t)
    canvas = canvas*(1-sh[...,None]) + col*sh[...,None]

# --- chrome face: vertical gradient with a bright "horizon" band ---
ys = np.linspace(0, 1, H)[:, None]
yrel = np.clip((ys*H - top + 0.16*sy) / (1.32*sy), 0, 1)
def lerp(a, b, t): return a + (b-a)*t
c_top  = np.array([178, 178, 214], np.float32)
c_mid  = np.array([72, 70, 128], np.float32)
c_band = np.array([222, 220, 246], np.float32)
c_low  = np.array([40, 38, 86], np.float32)
c_bot  = np.array([126, 124, 182], np.float32)
yr = yrel[..., None]
face = np.where(yr < 0.46, lerp(c_top, c_mid, yr/0.46),
        np.where(yr < 0.52, lerp(c_band, c_low, (yr-0.46)/0.06),
                 lerp(c_low, c_bot, (yr-0.52)/0.48)))
face = np.broadcast_to(face, (H, W, 3)).copy()
# diagonal brushed-metal streaks
yy, xx = np.mgrid[0:H, 0:W]
streak = 0.5 + 0.5*np.sin((xx*0.9 + yy*1.6) / (5.0*S)) * np.sin((xx - yy) / (23.0*S))
face *= (0.74 + 0.40*streak)[..., None]
# bevel: light from top-left
blur = np.array(mask.filter(ImageFilter.GaussianBlur(3*S)), np.float32)/255.0
gy, gx = np.gradient(blur)
shade = -(gx + gy) * 9.0 * S
face *= np.clip(1.0 + shade, 0.55, 1.7)[..., None]
canvas = canvas*(1-m[...,None]) + face*m[...,None]
# dark outline
edge = np.array(mask.filter(ImageFilter.MaxFilter(2*S+1)), np.float32)/255.0 - m
edge = np.clip(edge, 0, 1)
canvas = canvas*(1-edge[...,None]*0.85) + np.array([8,8,22])*edge[...,None]*0.85
canvas = np.clip(canvas, 0, 255).astype(np.uint8)
logo = Image.fromarray(canvas, 'RGB')

# --- hackatnight.com under it ---
font = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf', 17*S)
txt = 'hackatnight.com'
tm = Image.new('L', (W, H), 0); td = ImageDraw.Draw(tm)
tw = td.textlength(txt, font=font)
tx, ty = (W - tw)/2, 112*S
td.text((tx, ty), txt, font=font, fill=255)
ta = np.array(tm, np.float32)/255.0
tsh = np.zeros_like(ta); tsh[2*S:, 2*S:] = ta[:-2*S, :-2*S]
base = np.array(logo, np.float32)
base = base*(1-tsh[...,None]) + np.array([20,20,50])*tsh[...,None]
tg = lerp(np.array([225,225,255],np.float32), np.array([120,120,200],np.float32), np.clip((ys*H-ty)/(19*S),0,1)[...,None])
base = base*(1-ta[...,None]) + np.broadcast_to(tg,(H,W,3))*ta[...,None]
logo = Image.fromarray(np.clip(base,0,255).astype(np.uint8))

final = logo.resize((320, 140), Image.LANCZOS)
final.save('/tmp/logo/han_logo_320.png')
final.resize((960, 420), Image.NEAREST).save('/tmp/logo/han_logo_preview.png')
