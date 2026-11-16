/* Host tool: record which 4 KB ROM page a real game reads, in order.
 *
 * Built with -DGB_TRACE_ROM, which makes mem.c route every read above 0x4000
 * through gb_rom_trace() instead of a mapped pointer, so nothing is missed.
 * Consecutive reads of the same page are one entry: a page that is already
 * resident costs nothing in any layout, so only the hops matter.
 *
 *   romtrace <rom> <frames> <out.trace>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "gb_glue.h"
#include "hw.h"

void gb_audio_out(const uint8_t *s, int n) { (void)s; (void)n; }
void gb_fatal(const char *f, int l, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  fprintf(stderr, "FATAL %s:%d ", f, l); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
  va_end(ap); exit(2);
}
int gb_swap_read(int p, uint8_t *b) { (void)p; (void)b; return -1; }
int gb_swap_write(int p, const uint8_t *b) { (void)p; (void)b; return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n) { (void)o; (void)b; (void)n; return -1; }

static FILE *out;
static int last = -1;
static unsigned long hops, reads;

void gb_rom_trace(int bank, int k)
{
  int page = (bank << 2) | k;
  reads++;
  if (page == last) return;
  last = page;
  hops++;
  fputc(page & 0xFF, out);
  fputc((page >> 8) & 0xFF, out);
}

static uint16_t frame565[160 * 144];
static void on_line(int L, const uint16_t *px) { memcpy(&frame565[L * 160], px, 320); gb_lines_need = L + 1; }
static void dump_ppm(const char *path)
{
  FILE *o = fopen(path, "wb");
  int i;
  if (!o) return;
  fprintf(o, "P6\n160 144\n255\n");
  for (i = 0; i < 160 * 144; i++) {
    uint16_t c = frame565[i];
    unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3) };
    fwrite(rgb, 1, 3, o);
  }
  fclose(o);
}

int main(int argc, char **argv)
{
  FILE *f;
  long n;
  uint8_t *rom;
  int frames, i, menu_frames, shot_every;
  static uint8_t fb[160 * 144];

  if (argc < 4) { fprintf(stderr, "usage: romtrace <rom> <frames> <out.trace>\n"); return 1; }
  f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  rom = (uint8_t *)malloc(n);
  if (fread(rom, 1, n, f) != (size_t)n) { fprintf(stderr, "short read\n"); return 1; }
  fclose(f);
  frames = atoi(argv[2]);
  menu_frames = frames / 6 < 1200 ? 1200 : frames / 6;
  shot_every = argc > 5 ? atoi(argv[5]) : 0;
  out = fopen(argv[3], "wb");
  if (!out) { perror(argv[3]); return 1; }

  gb_glue_init(22050);
  gb_line_hook = on_line;
  if (rom_load(rom, n)) { fprintf(stderr, "rom_load failed\n"); return 1; }
  if (gb_alloc_wram()) { fprintf(stderr, "wram failed\n"); return 1; }
  gb_sram_setup(32, 0);
  gb_reset();

  for (i = 0; i < frames; i++)
  {
    /* Get through logos and menus with START and A, then stop pressing START
     * so the game is not paused for the rest of the run, and drive it with A
     * and the D-pad. Buttons are held for 10 frames at a time; a game ignores
     * a press that is gone before it polls. */
    {
      static const uint8_t menu[] = { PAD_START, PAD_A, PAD_DOWN, PAD_A, PAD_START, PAD_A };
      static const uint8_t play[] = { PAD_A, PAD_RIGHT, PAD_A, PAD_UP, PAD_B, PAD_LEFT,
                                      PAD_A, PAD_DOWN, PAD_RIGHT | PAD_A, PAD_A };
      int step = i / 10;
      uint8_t pad = (i < menu_frames) ? menu[step % (int)sizeof menu]
                                      : play[step % (int)sizeof play];
      gb_set_pad((i % 10 < 6) ? pad : 0);
    }
    gb_set_framebuffer(fb, 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
    if (shot_every && i % shot_every == 0) {
      char path[256];
      snprintf(path, sizeof path, "%s.%05d.ppm", argv[4], i);
      dump_ppm(path);
    }
  }
  fclose(out);
  if (argc > 4) dump_ppm(argv[4]);
  fprintf(stderr, "%s: %d frames, %lu reads, %lu page hops\n", argv[1], frames, reads, hops);
  return 0;
}
