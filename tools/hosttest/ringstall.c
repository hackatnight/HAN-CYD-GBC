/* The emulator and the on-screen menu are the same task on the board, so if the
 * emulator ever waits forever for the display the whole board stops answering
 * its buttons - which is what a player sees as "it froze and I had to reset it".
 *
 * This holds the display still (gb_lines_need never advances) and checks that
 * the emulator keeps finishing frames anyway, counting the lines it gave up on.
 * Built with a short give-up time so the test runs in a second instead of a
 * minute; the board uses 250 ms.
 *
 *   ringstall <rom>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "lcd.h"
#include "gb_glue.h"

void gb_audio_out(const uint8_t *s, int n) { (void)s; (void)n; }
void gb_fatal(const char *f, int l, const char *fmt, ...) { (void)f; (void)l; (void)fmt; printf("FATAL\n"); exit(2); }
int gb_swap_read(int p, uint8_t *b) { (void)p; (void)b; return -1; }
int gb_swap_write(int p, const uint8_t *b) { (void)p; (void)b; return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n) { (void)o; (void)b; (void)n; return -1; }

static unsigned long yields;
void gb_ring_yield(void) { yields++; }      /* the board yields to the rest of its core here */

static int display_alive = 1;
static void on_line(int L, const uint16_t *px)
{
  (void)px;
  if (display_alive) gb_lines_need = L + 1;   /* a display that keeps up */
}

int main(int argc, char **argv)
{
  FILE *f; long n; uint8_t *rom; int i, ok = 1;
  static uint8_t fb[160 * 144];
  unsigned long t0, ms;
  if (argc < 2) { printf("usage: ringstall <rom>\n"); return 2; }
  f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  rom = (uint8_t *)malloc(n);
  if (fread(rom, 1, n, f) != (size_t)n) return 2;
  fclose(f);

  gb_glue_init(22050);
  gb_ring_alloc(16);
  if (rom_load(rom, n) || gb_alloc_wram()) return 2;
  gb_sram_setup(32, 0);
  gb_reset();
  gb_line_hook = on_line;

  /* get the game drawing, with the display keeping up */
  for (i = 0; i < 120; i++) {
    gb_set_framebuffer(fb, 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
  }
  if (gb_ring_stalls != 0) { printf("stalled %u times while the display was keeping up  BAD\n", gb_ring_stalls); ok = 0; }

  /* now wedge the display and make sure the emulator carries on regardless */
  display_alive = 0;
  yields = 0;
  t0 = sys_micros();
  for (i = 0; i < 3; i++) {
    gb_set_framebuffer(fb, 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
  }
  ms = (sys_micros() - t0) / 1000;

  printf("display wedged: 3 frames in %lu ms, %u lines dropped, %lu yields\n", ms, gb_ring_stalls, yields);
  if (gb_ring_stalls == 0) { printf("the emulator never noticed the wedged display  BAD\n"); ok = 0; }
  if (yields == 0) { printf("the emulator never let the rest of the core run  BAD\n"); ok = 0; }
  /* one give-up per frame, not one per line: 3 frames must cost about 3 waits,
   * so a wedged display slows the picture down, not the game */
  if (ms > 6 * (GB_RING_WAIT_US / 1000)) {
    printf("waited %lu ms for 3 frames, expected about %d  BAD\n", ms, 3 * (GB_RING_WAIT_US / 1000));
    ok = 0;
  }
  printf(ok ? "ALL OK\n" : "FAILED\n");
  return !ok;
}
