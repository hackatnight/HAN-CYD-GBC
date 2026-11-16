/* Checks that the emulator never gets stuck waiting for the display.
 *
 * Lines go to the display through a ring, and the colors a game changes partway down the
 * screen go through a separate log (see lcd.c). A game that rewrites many colors on every
 * line can fill that log; this runs a ROM against a deliberately slow, jittery display
 * thread and fails if a frame takes absurdly long, which is what a stall looks like.
 *
 *   palstress ROM FRAMES RING
 * Build: gcc -O2 -w -pthread -Istub -I../../src/gnuboy ../../src/gnuboy/*.c palstress.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "gb_glue.h"

int gb_swap_read(int p, uint8_t *b) { return -1; }
int gb_swap_write(int p, const uint8_t *b) { return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n) { return -1; }
void gb_audio_out(const uint8_t *s, int n) {}
void gb_fatal(const char *f, int l, const char *fmt, ...) { printf("FATAL\n"); exit(2); }

static volatile int ready = 0, busy = 0, stop = 0;
static uint16_t line[160];

static void *display(void *a)
{
  unsigned seed = 3;
  while (!stop) {
    if (!ready) { usleep(20); continue; }
    ready = 0;
    for (int L = 0; L < 144; L++) {
      while (gb_lines_out <= L && !stop) { }
      gb_ring_line_rgb(L, line);
      gb_lines_need = L + 1;
      if (rand_r(&seed) % 6 == 0) usleep(rand_r(&seed) % 400);   /* an uneven display */
    }
    __sync_synchronize();
    busy = 0;
  }
  return 0;
}

int main(int argc, char **argv)
{
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  static uint8_t rom[4 << 20];
  memset(rom, 0xff, sizeof rom);
  int n = fread(rom, 1, sizeof rom, f);
  fclose(f);
  int frames = argc > 2 ? atoi(argv[2]) : 900;
  int ring = argc > 3 ? atoi(argv[3]) : 144;
  gb_glue_init(22050);
  gb_ring_alloc(ring);
  if (rom_load(rom, n) || gb_alloc_wram()) return 1;
  gb_sram_setup(32, 0);
  gb_reset();
  pthread_t t;
  pthread_create(&t, 0, display, 0);
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (int i = 0; i < frames; i++) {
    gb_set_pad((i % 120) < 4 ? 0x80 : ((i % 37) < 3 ? 0x10 : 0));
    int r = !busy;
    gb_set_framebuffer(0, r);
    gb_run_vblank();
    if (r) { busy = 1; __sync_synchronize(); ready = 1; }
    gb_run_visible();
    if (r) gb_frame_end();
  }
  clock_gettime(CLOCK_MONOTONIC, &b);
  stop = 1; ready = 0; busy = 0;
  pthread_join(t, 0);
  double secs = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
  /* the display sleeps on purpose, so this is generous: a stall is orders of magnitude worse */
  /* The log should be big enough that no color change ever has to wait for a later
   * line; anything else means a game's raster colors can land a line or two late. */
  int bad = gb_pal_dropped != 0 || secs > frames * 0.05;
  printf("%-44s ring %3d: %5.2f s, %u late color changes  %s\n",
         strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1],
         ring, secs, gb_pal_dropped, bad ? "FAIL" : "ok");
  return bad;
}
