/*
 * H@N-CYD-GBC: code-drawn boot splash. Nothing is stored as an image:
 *  - the letters H@N are signed distance fields (boxes, rings, segments, a curve),
 *    evaluated once into a 280x120 lookup table (1/8 px steps, ~33 KB while the
 *    splash runs, freed afterwards);
 *  - the 3D depth is a stack of slices that shrink toward a vanishing point below
 *    the logo, darker the deeper they are; while flying in fewer slices are drawn;
 *  - the face is chrome (a reflection gradient by height), the bevel is lit from the
 *    top left using the distance field's slope; a shine band can sweep across it;
 *  - sky, glow and stars are math (stars come from a pixel hash: the same every boot),
 *    the mist is two layers of value noise drifting at different speeds.
 * Pure C with no Arduino dependencies, so it can be rendered and checked on a PC.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "splash.h"

/* ---------------- logo space ---------------- */
#define LCX 160.0f
#define LCY 100.0f
#define HH 47.0f
#define ST 13.0f
#define MX0 20
#define MY0 40
#define MW 280
#define MH 120
#define DEPTH 30
#define DSTEP 0.0115f

static int8_t *dmap = NULL;           /* distance * 8, clamped */
static float *glow_x = NULL, *glow_y = NULL;
static uint8_t *mistg = NULL;         /* mist noise, quarter resolution */
static int mist_gw, mist_gh;
static int W, H;
static float Z, SCX, SCY, VPX, VPY;
static int TEXTY, MISTTOP, HAZETOP, STARMAXY;
static float SHINEY;
static float (*env_lut)[3] = NULL;   /* 65 entries, allocated with the rest */
#define MAXSTARS 220
static int nstars;
static uint16_t *star_x = NULL, *star_y = NULL;
static uint8_t *star_big = NULL;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/* ---------------- SDF primitives ---------------- */
static float sd_box(float x, float y, float cx, float cy, float hx, float hy, float r)
{
    float qx = fabsf(x - cx) - hx + r, qy = fabsf(y - cy) - hy + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = qx > qy ? qx : qy;
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

static float sd_seg(float x, float y, float ax, float ay, float bx, float by, float w)
{
    float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    float t = clampf((px * dx + py * dy) / (dx * dx + dy * dy), 0, 1);
    px -= t * dx; py -= t * dy;
    return sqrtf(px * px + py * py) - w;
}

static float sd_arc(float x, float y, float cx, float cy, float r, float w, float a0, float a1)
{
    float ang = atan2f(y - cy, x - cx);
    float am = (a0 + a1) * 0.5f;
    float da = fmodf(ang - am + 3.14159265f, 6.2831853f);
    if (da < 0) da += 6.2831853f;
    da -= 3.14159265f;
    if (fabsf(da) <= (a1 - a0) * 0.5f) return fabsf(sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r) - w;
    float e0x = x - (cx + r * cosf(a0)), e0y = y - (cy + r * sinf(a0));
    float e1x = x - (cx + r * cosf(a1)), e1y = y - (cy + r * sinf(a1));
    float e0 = sqrtf(e0x * e0x + e0y * e0y), e1 = sqrtf(e1x * e1x + e1y * e1y);
    return (e0 < e1 ? e0 : e1) - w;
}

static float sd_bezier(float x, float y, float x0, float y0, float x1, float y1, float x2, float y2, float w)
{
    float best = 1e9f, px = x0, py = y0;
    for (int i = 1; i <= 8; i++) {
        float t = i / 8.0f, u = 1 - t;
        float nx = u * u * x0 + 2 * u * t * x1 + t * t * x2;
        float ny = u * u * y0 + 2 * u * t * y1 + t * t * y2;
        float d = sd_seg(x, y, px, py, nx, ny, w);
        if (d < best) best = d;
        px = nx; py = ny;
    }
    return best;
}

#define UNI(a, b) ((a) < (b) ? (a) : (b))
#define SUB(a, b) ((a) > -(b) ? (a) : -(b))
#define INT(a, b) ((a) > (b) ? (a) : (b))

static float logo_sdf(float x, float y)
{
    /* H */
    float h = UNI(sd_box(x, y, 42, 100, ST, HH, 3), sd_box(x, y, 90, 100, ST, HH, 3));
    h = UNI(h, sd_box(x, y, 66, 98, 12, 9, 2));
    h = SUB(h, sd_seg(x, y, 26, 46, 34, 54, 4));
    h = SUB(h, sd_seg(x, y, 106, 46, 98, 54, 4));
    /* @: outer arc open at the lower right, bowl, stem, curve from the stem foot */
    const float cx = 160, cy = 100;
    float outer = sd_arc(x, y, cx, cy, 44, 7.5f, 1.05f, 1.05f + 6.2831853f - 0.78f);
    float bx = x - (cx - 5), by = y - (cy + 2);
    float bowl = fabsf(sqrtf(bx * bx + by * by) - 15) - 6.5f;
    float stem = sd_box(x, y, cx + 14, cy + 2, 6, 20, 3);
    float foot = sd_bezier(x, y, cx + 14, cy + 18, cx + 22, cy + 32, cx + 44 * cosf(0.27f), cy + 44 * sinf(0.27f), 6.5f);
    float at = UNI(UNI(outer, bowl), UNI(stem, foot));
    /* N */
    float n = UNI(sd_box(x, y, 230, 100, ST, HH, 3), sd_box(x, y, 278, 100, ST, HH, 3));
    float diag = INT(sd_seg(x, y, 230, 54, 278, 146, 13), fabsf(y - 100) - HH);
    n = UNI(n, diag);
    n = SUB(n, sd_seg(x, y, 214, 46, 222, 54, 4));
    n = SUB(n, sd_seg(x, y, 294, 154, 286, 146, 4));
    return UNI(UNI(h, at), n);
}

/* ---------------- lookups ---------------- */
static inline float dist_at(float qx, float qy)
{
    float fx = qx - MX0, fy = qy - MY0;
    if (fx < 0 || fy < 0 || fx >= MW - 1 || fy >= MH - 1) return 16.0f;
    int ix = (int)fx, iy = (int)fy;
    float ax = fx - ix, ay = fy - iy;
    const int8_t *p = dmap + iy * MW + ix;
    float a = p[0], b = p[1], c = p[MW], d = p[MW + 1];
    return ((a + (b - a) * ax) * (1 - ay) + (c + (d - c) * ax) * ay) * 0.125f;
}

/* ---------------- noise / hash ---------------- */
static inline uint32_t hash3(int x, int y, int o)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)o * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float vnoise(float x, float y, int o)
{
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy;
    float ax = x - fx, ay = y - fy;
    ax = ax * ax * (3 - 2 * ax); ay = ay * ay * (3 - 2 * ay);
    float a = (hash3(ix, iy, o) & 0xFFFF) * (1 / 65535.f), b = (hash3(ix + 1, iy, o) & 0xFFFF) * (1 / 65535.f);
    float c = (hash3(ix, iy + 1, o) & 0xFFFF) * (1 / 65535.f), d = (hash3(ix + 1, iy + 1, o) & 0xFFFF) * (1 / 65535.f);
    return (a + (b - a) * ax) * (1 - ay) + (c + (d - c) * ax) * ay;
}

static float fbm(float x, float y, int base)
{
    float v = 0, amp = 0.5f, f = 1;
    for (int o = 0; o < 4; o++) { v += amp * vnoise(x * f, y * f, base + o); amp *= 0.5f; f *= 2.07f; }
    return v;
}

/* stars: a pixel hash decides, so the field is the same every boot */
static inline int star_at(int x, int y, float t, float *b, int *big)
{
    if (y >= STARMAXY || x < 1 || y < 1) return 0;
    uint32_t h = hash3(x, y, 99);
    if (h % 520u) return 0;
    uint32_t g = hash3(x, y, 77);
    float base = 0.35f + 0.65f * ((g & 0xFF) / 255.0f);
    float ph = ((g >> 8) & 0xFF) / 255.0f, f = 0.5f + 1.5f * ((g >> 16) & 0xFF) / 255.0f;
    *b = base * (0.3f + 0.7f * (0.5f + 0.5f * sinf(6.2831853f * (t * f * 0.8f + ph))));
    *big = (g >> 24) < 31;   /* ~12% */
    return 1;
}

/* ---------------- setup ---------------- */
static void env_build(void)
{
    static const float stops[6][4] = {
        { 0.00f, 200, 214, 230 }, { 0.30f, 120, 138, 160 }, { 0.47f, 235, 242, 250 },
        { 0.52f, 52, 60, 74 }, { 0.78f, 88, 100, 118 }, { 1.00f, 150, 165, 182 } };
    for (int i = 0; i <= 64; i++) {
        float v = i / 64.0f;
        int s = 0;
        while (s < 4 && v > stops[s + 1][0]) s++;
        float f = (v - stops[s][0]) / (stops[s + 1][0] - stops[s][0]);
        for (int c = 0; c < 3; c++) env_lut[i][c] = stops[s][c + 1] + (stops[s + 1][c + 1] - stops[s][c + 1]) * f;
    }
}

int splash_setup(int scr_w, int scr_h)
{
    splash_release();
    W = scr_w; H = scr_h;
    if (W >= H) {   /* landscape */
        Z = 1.0f; SCX = W / 2.0f; SCY = 90; VPX = W / 2.0f + 8; VPY = 205;
        TEXTY = 196; MISTTOP = 150;
    } else {        /* portrait: logo a bit smaller, more room below */
        Z = 0.76f; SCX = W / 2.0f; SCY = 116; VPX = W / 2.0f + 6; VPY = 236;
        TEXTY = 262; MISTTOP = 212;
    }
    HAZETOP = MISTTOP - 55;
    STARMAXY = MISTTOP + 50;
    SHINEY = SCY;
    dmap = (int8_t *)malloc(MW * MH);
    glow_x = (float *)malloc(W * sizeof(float));
    glow_y = (float *)malloc(H * sizeof(float));
    mist_gw = W / 4 + 2;
    mist_gh = (H - MISTTOP) / 4 + 2;
    mistg = (uint8_t *)malloc(mist_gw * mist_gh);
    env_lut = (float (*)[3])malloc(65 * 3 * sizeof(float));
    star_x = (uint16_t *)malloc(MAXSTARS * 2);
    star_y = (uint16_t *)malloc(MAXSTARS * 2);
    star_big = (uint8_t *)malloc(MAXSTARS);
    if (!dmap || !glow_x || !glow_y || !mistg || !env_lut || !star_x || !star_y || !star_big) { splash_release(); return -1; }
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            float d = logo_sdf(MX0 + x, MY0 + y) * 8.0f;
            dmap[y * MW + x] = (int8_t)clampf(roundf(d), -127, 127);
        }
    float gcx = W - 58, gcy = 24;
    for (int x = 0; x < W; x++) glow_x[x] = expf(-(x - gcx) * (x - gcx) / (2 * 60.0f * 60.0f));
    for (int y = 0; y < H; y++) glow_y[y] = expf(-(y - gcy) * (y - gcy) / (2 * 60.0f * 60.0f));
    env_build();
    nstars = 0;
    for (int y = 1; y < STARMAXY && nstars < MAXSTARS; y++)
        for (int x = 1; x < W - 1 && nstars < MAXSTARS; x++) {
            float b; int big;
            if (star_at(x, y, 0, &b, &big)) { star_x[nstars] = x; star_y[nstars] = y; star_big[nstars] = big; nstars++; }
        }
    splash_mist_prepare(0);
    return 0;
}

void splash_release(void)
{
    free(dmap); dmap = NULL;
    free(glow_x); glow_x = NULL;
    free(glow_y); glow_y = NULL;
    free(mistg); mistg = NULL;
    free(env_lut); env_lut = NULL;
    free(star_x); star_x = NULL;
    free(star_y); star_y = NULL;
    free(star_big); star_big = NULL;
    nstars = 0;
}

int splash_text_y(void) { return TEXTY; }
int splash_mist_top(void) { return MISTTOP; }
float splash_shine_y(void) { return SHINEY; }
int splash_star_count(void) { return nstars; }
void splash_star(int i, int *x, int *y, int *big) { *x = star_x[i]; *y = star_y[i]; *big = star_big[i]; }

static void to_screen(float qx, float qy, float g, float *px, float *py)
{
    float bx = SCX + (qx - LCX) * Z, by = SCY + (qy - LCY) * Z;
    *px = VPX + (bx - VPX) * g;
    *py = VPY + (by - VPY) * g;
}

void splash_logo_rect(float g, int *x0, int *y0, int *x1, int *y1)
{
    float sx[4], sy[4];
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    float scales[2] = { g, g * (1 - DSTEP * DEPTH) };
    for (int s = 0; s < 2; s++) {
        to_screen(MX0, MY0, scales[s], &sx[0], &sy[0]);
        to_screen(MX0 + MW, MY0 + MH, scales[s], &sx[1], &sy[1]);
        for (int i = 0; i < 2; i++) {
            if (sx[i] < minx) minx = sx[i];
            if (sx[i] > maxx) maxx = sx[i];
            if (sy[i] < miny) miny = sy[i];
            if (sy[i] > maxy) maxy = sy[i];
        }
    }
    *x0 = (int)clampf(minx - 4, 0, W - 1); *y0 = (int)clampf(miny - 4, 0, H - 1);
    *x1 = (int)clampf(maxx + 4, 0, W - 1); *y1 = (int)clampf(maxy + 4, 0, H - 1);
}

void splash_mist_prepare(float t)
{
    if (!mistg) return;
    for (int gy = 0; gy < mist_gh; gy++)
        for (int gx = 0; gx < mist_gw; gx++) {
            float x = gx * 4.0f, y = MISTTOP + gy * 4.0f;
            float m1 = fbm(x / 36.0f + t * 0.9f, y / 9.0f + 0.2f * sinf(x / 50.0f + t * 0.8f), 10);
            float m2 = fbm(x / 20.0f - t * 0.6f + 7, y / 7.0f + 3, 20);
            mistg[gy * mist_gw + gx] = (uint8_t)clampf((m1 * 0.6f + m2 * 0.5f) * 200.0f, 0, 255);
        }
}

/* ---------------- render ---------------- */
static const float BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

static inline uint16_t pack565(float r, float g, float b, int x, int y)
{
    float d = ((BAYER[(y & 3) * 4 + (x & 3)] + 0.5f) / 16 - 0.5f) * 0.8f;
    int R = (int)(r * (31 / 255.0f) + d + 0.5f), G = (int)(g * (63 / 255.0f) + d + 0.5f), B = (int)(b * (31 / 255.0f) + d + 0.5f);
    R = R < 0 ? 0 : R > 31 ? 31 : R;
    G = G < 0 ? 0 : G > 63 ? 63 : G;
    B = B < 0 ? 0 : B > 31 ? 31 : B;
    uint16_t c = (uint16_t)((R << 11) | (G << 5) | B);
    return (uint16_t)((c >> 8) | (c << 8));
}

void splash_render(const splash_frame_t *f, int x0, int y, int w, uint16_t *out)
{
    const float g = f->g;
    const int slices = f->slices < 1 ? 1 : (f->slices > DEPTH ? DEPTH : f->slices);
    const float dstep = DSTEP * DEPTH / slices;
    /* front: q = LC + (VP + (p - VP)/g - SC)/Z  ->  qx = A0 + x * B0 */
    const float B0 = 1.0f / (g * Z);
    const float A0 = LCX + (VPX - VPX / g - SCX) / Z;
    const float qy0 = LCY + (VPY + (y - VPY) / g - SCY) / Z;
    /* slice k: same with g * s_k; precompute its row (or -1) and x mapping */
    float Ak[DEPTH + 1], Bk[DEPTH + 1];
    int rowk[DEPTH + 1];
    int any = 0;
    for (int k = 1; k <= slices; k++) {
        float gs = g * (1 - dstep * k);
        Bk[k] = 1.0f / (gs * Z);
        Ak[k] = LCX + (VPX - VPX / gs - SCX) / Z - MX0 + 0.5f;
        float qy = LCY + (VPY + (y - VPY) / gs - SCY) / Z - MY0 + 0.5f;
        rowk[k] = (qy >= 0 && qy < MH) ? (int)qy : -1;
        if (rowk[k] >= 0) any = 1;
    }
    /* row constants */
    const float t_ = (float)y / H;
    const float skyr = 6 + 16 * t_, skyg = 10 + 24 * t_, skyb = 22 + 40 * t_;
    const float gy_ = glow_y[y];
    const float low = clampf((y - HAZETOP) / 120.0f, 0, 1);
    const int in_mist = y >= MISTTOP;
    const float mist_fade = clampf((y - MISTTOP) / 16.0f, 0, 1);
    const float mgy = (y - MISTTOP) / 4.0f;
    const int mgi = in_mist ? (int)mgy : 0;
    const float mga = mgy - mgi;
    const int trow = y - f->text_y0;
    const int has_text = f->text_mask && trow >= 0 && trow < f->text_h;
    const float tg = clampf((y - (TEXTY - 9)) / 18.0f, 0, 1);
    const float tr = 235 * (1 - tg) + 120 * tg, tgc = 242 * (1 - tg) + 140 * tg, tb = 250 * (1 - tg) + 165 * tg;
    const float L0 = -0.55f, L1 = -0.7f, L2 = 0.45f;
    const float Ln = 1.0f / sqrtf(L0 * L0 + L1 * L1 + L2 * L2);
    const float bright = f->bright;

    for (int i = 0; i < w; i++) {
        const int x = x0 + i;
        float r, gg, b;
        /* front face distance (logo units -> screen) */
        const float qx = A0 + x * B0;
        const float d = dist_at(qx, qy0);
        const float ds = d * Z * g;
        const float alpha = clampf(0.5f - ds, 0, 1);
        if (alpha < 1) {
            /* sky + glow */
            float gl = glow_x[x] * gy_;
            r = skyr + 34 * gl; gg = skyg + 44 * gl; b = skyb + 64 * gl;
            /* stars */
            float sb; int big;
            if (star_at(x, y, f->t, &sb, &big)) {
                float sr = 200 * sb, sg = 215 * sb, sbb = 245 * sb;
                if (sr > r) r = sr;
                if (sg > gg) gg = sg;
                if (sbb > b) b = sbb;
            } else {
                static const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int n = 0; n < 4; n++)
                    if (star_at(x + nb[n][0], y + nb[n][1], f->t, &sb, &big) && big && sb > 0.72f) {
                        float sr = 100 * sb, sg = 108 * sb, sbb = 122 * sb;
                        if (sr > r) r = sr;
                        if (sg > gg) gg = sg;
                        if (sbb > b) b = sbb;
                    }
            }
            /* haze + drifting mist */
            float m = 0.05f * low;
            if (in_mist && mistg) {
                float mx = x / 4.0f;
                int mi = (int)mx;
                float ma = mx - mi;
                const uint8_t *p = mistg + mgi * mist_gw + mi;
                float n0 = p[0] + (p[1] - p[0]) * ma, n1 = p[mist_gw] + (p[mist_gw + 1] - p[mist_gw]) * ma;
                float nv = (n0 + (n1 - n0) * mga) * (1 / 200.0f);
                m += clampf((nv - 0.45f) * 1.6f, 0, 1) * (0.06f + 0.45f * low) * mist_fade;
            }
            m = clampf(m, 0, 0.55f);
            r += (150 - r) * m; gg += (165 - gg) * m; b += (190 - b) * m;
            /* dark edge around the letters */
            if (d < 15.0f && ds < 3.0f) {
                float sh = clampf(1 - (ds + 0.5f) / 3.5f, 0, 1) * 0.8f;
                r *= 1 - sh; gg *= 1 - sh; b *= 1 - sh;
            }
            /* depth slices: nearest slice hit, darker the deeper */
            if (any) {
                for (int k = 1; k <= slices; k++) {
                    if (rowk[k] < 0) continue;
                    int ix = (int)(Ak[k] + x * Bk[k]);
                    if (ix < 0 || ix >= MW) continue;
                    if (dmap[rowk[k] * MW + ix] < 4) {
                        float dep = (float)k / slices;
                        float s = powf(1 - dep, 1.6f);
                        r = (96 * s + 6 * dep) * bright;
                        gg = (110 * s + 7 * dep) * bright;
                        b = (132 * s + 10 * dep) * bright;
                        break;
                    }
                }
            }
        } else {
            r = gg = b = 0;
        }
        if (alpha > 0) {
            /* chrome face by height + lit bevel */
            float v = clampf((qy0 - (LCY - HH)) / (2 * HH), 0, 1) * 64;
            int vi = (int)v;
            float va = v - vi;
            if (vi >= 64) { vi = 63; va = 1; }
            float fr = env_lut[vi][0] + (env_lut[vi + 1][0] - env_lut[vi][0]) * va;
            float fg = env_lut[vi][1] + (env_lut[vi + 1][1] - env_lut[vi][1]) * va;
            float fb = env_lut[vi][2] + (env_lut[vi + 1][2] - env_lut[vi][2]) * va;
            float hgt = clampf(-d / 7.0f, 0, 1);
            float edge = 1 - hgt;
            if (edge > 0) {
                float gx = (dist_at(qx + 1, qy0) - dist_at(qx - 1, qy0)) * 0.5f;
                float gyv = (dist_at(qx, qy0 + 1) - dist_at(qx, qy0 - 1)) * 0.5f;
                float slope = edge * 1.6f;
                float nx = -gx * slope, ny = -gyv * slope, nz = 1;
                float lam = clampf((nx * L0 + ny * L1 + nz * L2) * Ln / sqrtf(nx * nx + ny * ny + nz * nz), 0, 1);
                float lp = powf(lam, 1.3f);
                float br = 34 + (240 - 34) * lp, bg = 40 + (246 - 40) * lp, bb = 52 + (255 - 52) * lp;
                float e = edge * 0.9f;
                fr = fr * (1 - e) + br * e; fg = fg * (1 - e) + bg * e; fb = fb * (1 - e) + bb * e;
            }
            if (f->shine > -1000) {
                float u = x + 0.55f * y - 0.55f * SHINEY - f->shine;
                if (u > -33 && u < 33) {
                    float sh = expf(-(u / 11) * (u / 11)) * (0.75f + 0.25f * edge);
                    fr += (255 - fr) * sh; fg += (255 - fg) * sh; fb += (255 - fb) * sh;
                }
            }
            r = r * (1 - alpha) + fr * bright * alpha;
            gg = gg * (1 - alpha) + fg * bright * alpha;
            b = b * (1 - alpha) + fb * bright * alpha;
        }
        /* hackatnight.com */
        if (has_text) {
            const uint8_t *orow = f->outline_mask + trow * f->mask_pitch;
            const uint8_t *trw = f->text_mask + trow * f->mask_pitch;
            if ((orow[x >> 3] >> (7 - (x & 7))) & 1) { r *= 0.2f; gg *= 0.2f; b *= 0.2f; }
            if ((trw[x >> 3] >> (7 - (x & 7))) & 1) { r = tr; gg = tgc; b = tb; }
        }
        out[i] = pack565(r, gg, b, x, y);
    }
}
