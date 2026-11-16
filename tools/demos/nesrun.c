// Runs an NES ROM in the firmware's nofrendo core on a PC, pressing buttons from a script
// and saving every Nth frame as a PPM (256x224), for checking the demo ROM.
//   nesrun ROM FRAMES EVERY OUTDIR [FRAME:PAD ...]
// PAD is the firmware's pad byte in hex (01 right, 02 left, 04 up, 08 down, 10 A, 20 B,
// 40 select, 80 start), held from that frame until the next script entry.
// Build: gcc -O2 -w -I../../src/nofrendo -I../../src/nofrendo/nes -I../hosttest/stub \
//          $(ls ../../src/nofrendo/*.c ../../src/nofrendo/nes/*.c ../../src/nofrendo/mappers/*.c) nesrun.c -lm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nes_glue.h"
volatile int gb_lines_out, gb_lines_need;
static unsigned char scratch[16768];
void *gb_scratch(size_t *n) { *n = sizeof scratch; return scratch; }
static long audio_n, audio_sum;
void gb_audio_out(const uint8_t *s, int n) { for (int i = 0; i < n; i++) audio_sum += abs((int)s[i] - 128); audio_n += n; }
void gb_fatal(const char *f, int l, const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); exit(2); }
int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "usage: nesrun ROM FRAMES EVERY OUTDIR [FRAME:PAD ...]\n"); return 1; }
  FILE *fp = fopen(argv[1], "rb"); if (!fp) return 1;
  fseek(fp, 0, SEEK_END); long n = ftell(fp); rewind(fp);
  uint8_t *rom = malloc(n); fread(rom, 1, n, fp); fclose(fp);
  int frames = atoi(argv[2]), every = atoi(argv[3]);
  if (nes_ring_alloc(16)) { puts("ring"); return 1; }
  int r = nes_start(rom, n, 22050, 0);
  if (r) { printf("start %d\n", r); return 1; }
  if (nes_ring_alloc(NES_H)) { puts("ring"); return 1; }   /* whole frame, after the game starts */
  uint16_t *pal = nes_pal565;
  nes_build_palette(0, pal);
  uint8_t pad = 0; int saved = 0;
  for (int f = 0; f < frames; f++) {
    for (int a = 5; a < argc; a++) {
      int fr; unsigned p;
      if (sscanf(argv[a], "%d:%x", &fr, &p) == 2 && fr == f) pad = (uint8_t)p;
    }
    nes_set_pad(pad);
    int draw = every > 0 && f % every == 0;
    if (draw) nes_begin_frame();
    nes_run_frame(draw);
    if (draw) {
      char path[512]; snprintf(path, sizeof path, "%s/f%05d.ppm", argv[4], f);
      FILE *o = fopen(path, "wb"); fprintf(o, "P6 %d %d 255\n", NES_W, NES_H);
      for (int i = 0; i < NES_W * NES_H; i++) {
        uint16_t c = pal[nes_ring[i]];
        fputc((c >> 11) << 3, o); fputc(((c >> 5) & 63) << 2, o); fputc((c & 31) << 3, o);
      }
      fclose(o); saved++;
    }
  }
  printf("mapper %d frames %d saved %d audio %.2f\n", nes_mapper(), frames, saved, audio_n ? (double)audio_sum / audio_n : 0.0);
  nes_stop();
  return 0;
}
