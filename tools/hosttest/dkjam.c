/* Host probe: run a game and print the hardware state each frame, to find
 * where a game stops drawing. Everything is resident here - the whole ROM is
 * in RAM and no cartridge RAM is paged - so anything this reproduces is a bug
 * in the emulator core, not in the streaming or paging code.
 *
 *   dkjam <rom> <frames> <first> <last>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "regs.h"
#include "hw.h"
#include "cpu.h"
#include "lcd.h"
#include "gb_glue.h"

void gb_audio_out(const uint8_t *s, int n) { (void)s; (void)n; }
void gb_fatal(const char *f, int l, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  fprintf(stderr, "FATAL %s:%d ", f, l); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
  va_end(ap); exit(2);
}
int gb_swap_read(int p, uint8_t *b) { (void)p; (void)b; return -1; }
int gb_swap_write(int p, const uint8_t *b) { (void)p; (void)b; return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n) { (void)o; (void)b; (void)n; return -1; }
static uint16_t frame565[160 * 144];
static int shot;
static void on_line(int L, const uint16_t *px) { memcpy(&frame565[L * 160], px, 320); gb_lines_need = L + 1; }
static void dump_ppm(const char *path)
{
  FILE *o = fopen(path, "wb"); int i;
  if (!o) return;
  fprintf(o, "P6\n160 144\n255\n");
  for (i = 0; i < 160 * 144; i++) {
    uint16_t c = frame565[i];
    unsigned char rgb[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3) };
    fwrite(rgb, 1, 3, o);
  }
  fclose(o);
}

/* -DGB_PROBE routes these from cpu.c and mem.c */
static unsigned ints[5], hram80_w, hram80_last = 0x100;
void gb_probe_int(int n) { if (n >= 0 && n < 5) ints[n]++; }
void gb_probe_hram(int a, int b) { if (a == 0xFF80) { hram80_w++; hram80_last = b; } }

int main(int argc, char **argv)
{
  FILE *f; long n; uint8_t *rom; int frames, i, first, last, idle;
  static uint8_t fb[160 * 144];
  f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  rom = (uint8_t *)malloc(n);
  if (fread(rom, 1, n, f) != (size_t)n) return 1;
  fclose(f);
  frames = atoi(argv[2]); first = atoi(argv[3]); last = atoi(argv[4]);
  idle = argc > 5 && argv[5][0] == 'i';
  shot = argc > 5;

  gb_glue_init(22050);
  gb_line_hook = on_line;
  if (rom_load(rom, n) || gb_alloc_wram()) return 1;
  gb_sram_setup(32, 0);
  gb_reset();

  for (i = 0; i < frames; i++)
  {
    static const uint8_t menu[] = { PAD_START, PAD_A, PAD_DOWN, PAD_A, PAD_START, PAD_A };
    static const uint8_t play[] = { PAD_A, PAD_RIGHT, PAD_A, PAD_UP, PAD_B, PAD_LEFT,
                                    PAD_A, PAD_DOWN, PAD_RIGHT | PAD_A, PAD_A };
    int step = i / 10;
    uint8_t pad = (i < 1200) ? menu[step % (int)sizeof menu]
                             : (idle ? 0 : play[step % (int)sizeof play]);
    gb_set_pad((i % 10 < 6) ? pad : 0);
    gb_set_framebuffer(fb, 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
    if (shot && i % 800 == 0) { char pp[64]; snprintf(pp, sizeof pp, "/tmp/idle.%05d.ppm", i); dump_ppm(pp); }
    if (i >= first && i <= last)
      printf("f%-6d LCDC=%02X STAT=%02X LY=%02X IE=%02X IF=%02X bank=%-3d "
             "pc=%04X sp=%04X halt=%d ime=%d speed=%d\n",
             i, R_LCDC, R_STAT, R_LY, R_IE, R_IF, mbc.rombank,
             cpu.pc.w[0], cpu.sp.w[0], cpu.halt, cpu.ime, cpu.speed);
    if (i >= first && i <= last) {
      printf("        vbl=%u stat=%u tim=%u ser=%u joy=%u | FF80 writes=%u last=%02X now=%02X\n",
             ints[0], ints[1], ints[2], ints[3], ints[4], hram80_w, hram80_last, ram.hi[0x80]);
      memset(ints, 0, sizeof ints); hram80_w = 0;
      {
        int k; printf("        vec40:"); for (k = 0; k < 6; k++) printf(" %02X", ram.ibank[0][0x32D + k]);
        printf("   vec48:"); for (k = 0; k < 10; k++) printf(" %02X", ram.ibank[0][0x330 + k]);
        unsigned v0 = 0, v1 = 0, nz0 = 0;
        int j;
        for (j = 0; j < 8192; j++) { v0 = v0 * 31 + lcd.vbank[0][j]; if (lcd.vbank[0][j]) nz0++; }
        for (j = 0; j < 8192; j++) v1 = v1 * 31 + lcd.vbank[1][j];
        printf("   HDMA5=%02X SVBK=%02X VBK=%02X SCX=%02X SCY=%02X BGP0=", REG(0x55), REG(0x70), REG(0x4F), REG(0x43), REG(0x42));
        for (j = 0; j < 64; j += 2) printf("%04X%s", lcd.pal[j] | (lcd.pal[j+1] << 8), (j % 8 == 6) ? "/" : " ");
        printf(" vram0=%08X(nz %u) vram1=%08X\n", v0, nz0, v1);
      }
    }
  }
  return 0;
}
