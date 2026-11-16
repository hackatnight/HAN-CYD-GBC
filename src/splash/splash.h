// H@N-CYD-GBC: code-drawn boot splash (no stored image). See splash.c.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float g;              // fly-in scale (0..1], 1 = landed
  float bright;         // 0..1, the logo comes out of the dark while flying in
  int slices;           // depth slices to draw (fewer while moving)
  float t;              // seconds since the splash started (stars, mist)
  float shine;          // shine band position in pixels, < -1000 = off
  // "hackatnight.com" masks, 1 bit per pixel, rows text_y0 .. text_y0 + text_h - 1
  const uint8_t *text_mask, *outline_mask;
  int text_y0, text_h, mask_pitch;
} splash_frame_t;

int splash_setup(int scr_w, int scr_h);   // ~36 KB while the splash runs; 0 = ok
void splash_release(void);
// screen layout values for the caller
int splash_text_y(void);                   // center row of the web address
int splash_mist_top(void);                 // first row of the drifting mist band
float splash_shine_y(void);                // reference row for the shine band
void splash_logo_rect(float g, int *x0, int *y0, int *x1, int *y1);
int splash_star_count(void);
void splash_star(int i, int *x, int *y, int *big);
// compute the mist for time t (call before rendering rows in the mist band)
void splash_mist_prepare(float t);
// render w pixels of row y starting at x0, byte-swapped RGB565 for the panel
void splash_render(const splash_frame_t *f, int x0, int y, int w, uint16_t *out);

#ifdef __cplusplus
}
#endif
