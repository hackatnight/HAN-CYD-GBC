#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstdlib>
struct { uint8_t rotation = 2, calValid = 0, calSwap = 0, touchFlipX = 0, touchFlipY = 0;
         float calAx = 1, calBx = 0, calAy = 1, calBy = 0; } cfg;
#include "touch.inc"
// A simulated touch layer: panel pixel -> raw reading, in one of 8 wirings.
struct Wiring { bool swap, fx, fy; float scale, off; };
// BIG=1: the 3.5" board. The touch layer covers the whole 320x480 screen and the firmware
// uses the 240x320 window in its middle (40 px in from the sides, 80 from the ends).
#ifndef BIG
#define BIG 0
#endif
static void raw(const Wiring &w, float px, float py, float &rx, float &ry, float noise) {
#if BIG
  px += 40; py += 80;
  float u = w.fx ? 319 - px : px, v = w.fy ? 479 - py : py;
  float a = w.off + u * w.scale, b = w.off + v * w.scale * (TOUCH_RESISTIVE ? 0.62f : 1.0f);  // 4096 over 480 px
#else
  float u = w.fx ? 239 - px : px, v = w.fy ? 319 - py : py;
  float a = w.off + u * w.scale, b = w.off + v * w.scale * 0.92f;   // y a bit less sensitive, like real panels
#endif
  a += (rand() % 1000 / 500.0f - 1) * noise; b += (rand() % 1000 / 500.0f - 1) * noise;
  if (w.swap) { rx = b; ry = a; } else { rx = a; ry = b; }
}
int main() {
  srand(7);
  const bool R = TOUCH_RESISTIVE;
  const float scale = R ? (BIG ? 11.2f : 14.5f) : 1.0f, off = R ? 250 : 0, noise = R ? 12 : 1;   // resistive: 0..4095 readings with jitter
  int fails = 0, cases = 0; float worst = 0;
  for (int wi = 0; wi < 8; wi++) {
    Wiring w = { bool(wi & 4), bool(wi & 2), bool(wi & 1), scale, off };
    for (int calRot = 0; calRot < 4; calRot++) {
      // calibrate in this orientation, like the Calibrate touch screen does
      cfg.rotation = calRot; cfg.calValid = 0;
      int W = calRot & 1 ? 320 : 240, H = calRot & 1 ? 240 : 320;
      const int TX[4] = { 30, W - 30, 30, W - 30 }, TY[4] = { 50, 50, H - 30, H - 30 };
      float rx[4], ry[4], PX[4], PY[4];
      for (int i = 0; i < 4; i++) {
        screenToPanel(TX[i], TY[i], PX[i], PY[i]);
        float sx = 0, sy = 0;   // the firmware averages ~25 readings per tap
        for (int k = 0; k < 25; k++) { float a, b; raw(w, PX[i], PY[i], a, b, noise); sx += a; sy += b; }
        rx[i] = sx / 25; ry[i] = sy / 25;
      }
      CalResult c;
      cases++;
      if (!calSolve(rx, ry, PX, PY, c)) { printf("  REJECTED: wiring %d, calibrated in rotation %d\n", wi, calRot); fails++; continue; }
      cfg.calValid = 1; cfg.calSwap = c.swap; cfg.calAx = c.ax; cfg.calBx = c.bx; cfg.calAy = c.ay; cfg.calBy = c.by;
      // then use it in every orientation: random taps must land where the finger was
      for (int useRot = 0; useRot < 4; useRot++) {
        cfg.rotation = useRot;
        int UW = useRot & 1 ? 320 : 240, UH = useRot & 1 ? 240 : 320;
        for (int t = 0; t < 300; t++) {
          float tx = rand() % UW, ty = rand() % UH, px, py, a, b, qx, qy, sx, sy;
          screenToPanel(tx, ty, px, py);
          raw(w, px, py, a, b, noise);
          rawToPanel(a, b, qx, qy);
          panelToScreen(qx, qy, sx, sy);
          float e = hypotf(sx - tx, sy - ty);
          if (e > worst) worst = e;
          if (e > (R ? 6 : 3)) { fails++; if (fails < 5) printf("  off by %.1f px: wiring %d cal %d use %d\n", e, wi, calRot, useRot); }
        }
      }
    }
  }
  // a bad calibration (one tap far from its cross) must be refused, not saved
  int refused = 0;
  for (int t = 0; t < 200; t++) {
    cfg.rotation = t % 4; int W = t % 2 ? 320 : 240, H = t % 2 ? 240 : 320;
    const int TX[4] = { 30, W - 30, 30, W - 30 }, TY[4] = { 50, 50, H - 30, H - 30 };
    float rx[4], ry[4], PX[4], PY[4]; Wiring w = { bool(t & 4), bool(t & 8), bool(t & 16), scale, off };
    for (int i = 0; i < 4; i++) { screenToPanel(TX[i], TY[i], PX[i], PY[i]); raw(w, PX[i], PY[i], rx[i], ry[i], noise); }
    int bad = t % 4; float bx, by; screenToPanel(W / 2, H / 2, bx, by); raw(w, bx, by, rx[bad], ry[bad], noise);   // tapped the middle instead
    CalResult c; if (!calSolve(rx, ry, PX, PY, c)) refused++;
  }
  printf("%s touch: a tap on the wrong spot is refused in %d of 200 tries\n", R ? "resistive" : "capacitive", refused);
  if (refused < 200) fails++;
  printf("%s touch: %d calibrations (8 wirings x 4 orientations), each checked with 1200 taps in all 4 orientations:"
         " %s, worst error %.1f px\n", R ? "resistive" : "capacitive", cases, fails ? "FAILED" : "all OK", worst);
  return fails ? 1 : 0;
}
