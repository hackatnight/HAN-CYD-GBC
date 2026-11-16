// Runs a Game Boy ROM in the firmware's gnuboy core on a PC, pressing buttons from a
// script and saving every Nth frame as a PPM, for checking the demo ROM and making GIFs.
//   gbrun ROM FRAMES EVERY OUTDIR [FRAME:PAD ...]
// PAD is the firmware's pad byte in hex (01 right, 02 left, 04 up, 08 down, 10 A, 20 B,
// 40 select, 80 start); it is held from that frame until the next script entry.
// Build: gcc -O2 -w -I../hosttest/stub -I../../src/gnuboy ../../src/gnuboy/*.c gbrun.c -lm
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "gb_glue.h"
static long samples, audio_sum;
int gb_swap_read(int p, uint8_t *b) { return -1; }
int gb_swap_write(int p, const uint8_t *b) { return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n) { return -1; }
void gb_audio_out(const uint8_t *s, int n) {
  samples += n;
  for (int i = 0; i < n; i++) audio_sum += s[i] > 128 ? s[i] - 128 : 128 - s[i];
}
void gb_fatal(const char *f, int l, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); fprintf(stderr, "FATAL %s:%d ", f, l); vfprintf(stderr, fmt, ap); va_end(ap); exit(2);
}
static const int pal[4] = { 0xd5f3ef, 0x7ab6a3, 0x3b6137, 0x161c04 };
static uint16_t frame565[160 * 144];
static void on_line(int L, const uint16_t *px) { memcpy(&frame565[L * 160], px, 320); gb_lines_need = L + 1; }
int main(int argc, char **argv) {
  if (argc < 5) { fprintf(stderr, "usage: gbrun ROM FRAMES EVERY OUTDIR [FRAME:PAD ...]\n"); return 1; }
  FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  long cap = 4 * 1024 * 1024; uint8_t *rom = malloc(cap); memset(rom, 0xff, cap); fread(rom, 1, n, f); fclose(f);
  int frames = atoi(argv[2]), every = atoi(argv[3]);
  static uint8_t fb[2][160 * 144];
  gb_glue_init(22050);
  gb_line_hook = on_line;
  if (rom_load(rom, n)) return 1;
  if (gb_alloc_wram()) return 3;
  gb_sram_setup(32, 0);
  gb_reset();
  if (!gb_is_cgb()) lcd_set_dmg_palette(pal);
  int back = 0, saved = 0; uint8_t pad = 0;
  for (int i = 0; i < frames; i++) {
    for (int a = 5; a < argc; a++) {
      int fr; unsigned p;
      if (sscanf(argv[a], "%d:%x", &fr, &p) == 2 && fr == i) pad = (uint8_t)p;
    }
    gb_set_pad(pad);
    gb_set_framebuffer(fb[back], 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
    back ^= 1;
    if (every > 0 && i % every == 0) {
      char path[512]; snprintf(path, sizeof path, "%s/f%05d.ppm", argv[4], i);
      FILE *o = fopen(path, "wb"); fprintf(o, "P6\n160 144\n255\n");
      for (int k = 0; k < 160 * 144; k++) {
        uint16_t c = frame565[k];
        unsigned char rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
        fwrite(rgb, 1, 3, o);
      }
      fclose(o); saved++;
    }
  }
  printf("title='%s' cgb=%d frames=%d saved=%d audio_activity=%.1f\n", gb_rom_title(), gb_is_cgb(), frames, saved,
         samples ? (double)audio_sum / samples : 0.0);
  return 0;
}
