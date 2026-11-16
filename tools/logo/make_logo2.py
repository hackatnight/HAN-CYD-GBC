import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi

S = 4
FW, FH = 320, 172
W, H = FW*S, FH*S

def P(pts, ox, oy, sx, sy): return [(ox + x*sx, oy + y*sy) for x, y in pts]

def glyph_mask():
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    sx, sy = 78*S, 80*S
    top = 22*S
    # H : chunky stems with blade serifs, pointed crossbar
    ox = 14*S
    d.polygon(P([(-0.14,-0.14),(0.08,0.00),(0.34,-0.02),(0.42,-0.14),(0.36,0.10),(0.33,0.88),(0.42,1.10),(0.16,0.98),(-0.12,1.08),(0.02,0.88),(0.04,0.12)],ox,top,sx,sy), fill=255)
    d.polygon(P([(0.58,-0.14),(0.68,0.00),(0.94,0.02),(1.16,-0.14),(0.98,0.12),(0.96,0.88),(1.14,1.08),(0.86,0.98),(0.60,1.10),(0.68,0.88),(0.66,0.10)],ox,top,sx,sy), fill=255)
    d.polygon(P([(0.26,0.40),(0.50,0.28),(0.76,0.40),(0.76,0.60),(0.50,0.74),(0.26,0.60)],ox,top,sx,sy), fill=255)
    # @ : thick ring + bowl + blade tail
    ox = 118*S; ax = sx*0.95
    cx, cy = ox + 0.52*ax, top + 0.50*sy
    rx, ry = 0.64*ax, 0.62*sy
    at = Image.new('L', (W, H), 0); ad = ImageDraw.Draw(at)
    ad.ellipse([cx-rx, cy-ry, cx+rx, cy+ry], fill=255)
    ad.ellipse([cx-rx*0.66, cy-ry*0.64, cx+rx*0.66, cy+ry*0.64], fill=0)
    ad.ellipse([cx-0.25*ax, cy-0.25*sy, cx+0.21*ax, cy+0.25*sy], fill=255)
    ad.ellipse([cx-0.11*ax, cy-0.11*sy, cx+0.07*ax, cy+0.11*sy], fill=0)
    ad.polygon(P([(0.66,0.22),(0.82,0.20),(0.80,0.78),(0.66,0.72)],ox,top,ax,sy), fill=255)
    ad.polygon(P([(0.82,0.66),(1.30,0.58),(1.30,1.20),(0.90,1.00)],ox,top,ax,sy), fill=0)
    ad.polygon(P([(0.80,0.68),(1.00,0.72),(1.36,1.02),(0.94,0.92),(0.68,0.80)],ox,top,ax,sy), fill=255)
    ad.polygon(P([(0.36,-0.06),(0.16,-0.26),(0.60,-0.04)],ox,top,ax,sy), fill=255)
    m.paste(255, (0,0), at)
    # N
    ox = 224*S
    d.polygon(P([(-0.14,-0.12),(0.08,0.02),(0.34,0.04),(0.31,0.88),(0.42,1.12),(0.14,0.98),(-0.12,1.08),(0.02,0.88),(0.04,0.12)],ox,top,sx,sy), fill=255)
    d.polygon(P([(0.64,-0.12),(0.88,0.00),(1.14,-0.16),(0.98,0.10),(0.97,0.86),(1.08,1.12),(0.80,1.00),(0.72,0.96),(0.68,0.10)],ox,top,sx,sy), fill=255)
    d.polygon(P([(0.04,0.04),(0.32,0.02),(0.62,0.38),(0.82,0.28),(0.72,0.52),(0.98,0.80),(0.98,1.04),(0.74,0.98),(0.10,0.26)],ox,top,sx,sy), fill=255)
    return np.array(m, np.float32)/255.0, top, sy

def text_mask(txt, size, y):
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    f = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Black.otf' if False else '/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf', size)
    tw = d.textlength(txt, font=f)
    d.text(((W-tw)/2, y), txt, font=f, fill=255)
    return np.array(m, np.float32)/255.0

def lerp(a, b, t): return a + (b - a)*t

def render(mask, depth, dirx, diry, bevel, top_y, height_px):
    m = mask > 0.5
    # ---- extrusion: nearest step index per pixel ----
    ext_idx = np.full(m.shape, 10**6, np.int32)
    for k in range(depth, 0, -1):
        dx, dy = int(round(k*dirx)), int(round(k*diry))
        sh = np.zeros_like(m)
        sh[dy:, dx:] = m[:H-dy, :W-dx]
        ext_idx[sh] = k
    ext = (ext_idx < 10**6) & ~m
    t = np.clip(ext_idx/depth, 0, 1)
    # side shading: bright lavender lip near the face -> deep indigo at the back,
    # plus vertical falloff so lower sides are darker
    near = np.array([120,112,190], np.float32); far = np.array([16,12,40], np.float32)
    ys = np.arange(H, dtype=np.float32)[:, None]
    vfall = np.clip((ys - top_y)/(height_px*1.25), 0, 1)
    side = lerp(near, far, (t**0.7)[..., None]) * (1.0 - 0.35*vfall)[..., None]
    # streaks on the sides too
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    side *= (0.85 + 0.25*(0.5+0.5*np.sin((xx - yy*0.7)/(2.2*S))))[..., None]

    # ---- face: bevel height from distance transform ----
    dist = ndi.distance_transform_edt(m)
    hgt = np.clip(dist/bevel, 0, 1)
    hgt = hgt*hgt*(3-2*hgt)                      # smoothstep -> rounded bevel
    hgt = ndi.gaussian_filter(hgt, 1.2*S)
    gy, gx = np.gradient(hgt*bevel*0.9)
    nz = np.ones_like(gx)
    nl = np.sqrt(gx*gx + gy*gy + nz*nz)
    nx, ny, nz = -gx/nl, -gy/nl, nz/nl
    # reflection of the view ray (0,0,1)
    rx, ry, rz = 2*nz*nx, 2*nz*ny, 2*nz*nz - 1
    # chrome environment: sky (ry<0, looking up) / dark horizon / purple floor
    sky_hi = np.array([246,244,255], np.float32); sky_lo = np.array([150,146,214], np.float32)
    horizon = np.array([22,18,58], np.float32)
    floor_hi = np.array([104,90,172], np.float32); floor_lo = np.array([214,206,248], np.float32)
    # flat parts reflect according to their height on the letter (classic chrome band)
    vrel = np.clip((ys - top_y)/height_px, 0, 1)
    pseudo = ry + (vrel - 0.47)*1.6           # vertical position acts like a tilt
    up = np.clip(-pseudo, 0, 1)[..., None]; dn = np.clip(pseudo, 0, 1)[..., None]
    env = np.where(pseudo[..., None] < 0,
                   lerp(lerp(sky_lo, sky_hi, np.clip(up*1.4, 0, 1)), horizon, np.clip(1 - up*6, 0, 1)),
                   lerp(lerp(horizon, floor_hi, np.clip(dn*5, 0, 1)), floor_lo, np.clip((dn-0.2)*1.6, 0, 1)))
    # specular glint from top-left light
    L = np.array([-0.55, -0.75, 0.37]); L /= np.linalg.norm(L)
    Hh = L + np.array([0,0,1.0]); Hh /= np.linalg.norm(Hh)
    spec = np.clip(nx*Hh[0] + ny*Hh[1] + nz*Hh[2], 0, 1)**60
    diff = np.clip(nx*L[0] + ny*L[1] + nz*L[2], 0, 1)
    face = env*(0.55 + 0.6*diff)[..., None] + 255*spec[..., None]*0.9
    # brushed-metal diagonal streaks
    face *= (0.80 + 0.30*(0.5+0.5*np.sin((xx*1.0 + yy*1.8)/(3.0*S))*np.sin((xx - yy)/(17.0*S))))[..., None]

    out = np.zeros((H, W, 3), np.float32)
    out[ext] = side[ext]
    out[m] = face[m]
    # dark outline around face+extrusion and a thin dark line between face and sides
    allm = m | ext
    ring = ndi.binary_dilation(allm, iterations=S) & ~allm
    out[ring] = [6, 4, 18]
    lip = ndi.binary_dilation(m, iterations=max(1, S//2)) & ext
    out[lip] = out[lip]*0.45
    return out, allm | ring

mask, top, sy = glyph_mask()
logo, cover = render(mask, depth=10*S, dirx=0.55, diry=1.0, bevel=7*S, top_y=top - 0.14*sy, height_px=1.28*sy)

tmask = text_mask('hackatnight.com', 21*S, 140*S)
txt, tcover = render(tmask, depth=3*S, dirx=0.6, diry=1.0, bevel=1.6*S, top_y=140*S - 6*S, height_px=24*S)

img = np.zeros((H, W, 3), np.float32)
img[cover] = logo[cover]
img[tcover] = txt[tcover]
img = np.clip(img, 0, 255).astype(np.uint8)
small = Image.fromarray(img).resize((FW, FH), Image.LANCZOS)

# Amiga-style ordered dither down to 5-6-5 bits (what the CYD panel shows anyway)
a = np.array(small, np.float32)
bayer = (np.array([[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]], np.float32) + 0.5)/16 - 0.5
by = np.tile(bayer, (FH//4 + 1, FW//4 + 1))[:FH, :FW]
for c, bits in enumerate((5, 6, 5)):
    step = 255/(2**bits - 1)
    a[..., c] = np.round(a[..., c]/step + by*0.9)*step
small = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
small.save('/tmp/logo/han_logo2_320.png')
small.resize((FW*3, FH*3), Image.NEAREST).save('/tmp/logo/han_logo2_preview.png')
print('ok')
