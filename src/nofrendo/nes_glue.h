// H@N-CYD-GBC: NES (nofrendo) glue, the NES counterpart of gnuboy/gb_glue.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define NES_W 256          /* visible width */
#define NES_H 224          /* visible lines (NTSC overscan cropped) */

/* Line handoff to the display: palette indices (0..63, +64/+128 flag bits),
   one byte per pixel, nes_ring_lines lines. Uses gb_lines_out / gb_lines_need. */
extern uint8_t *nes_ring;
extern int nes_ring_lines;
int nes_ring_alloc(int lines);   /* 0 = ok (keeps the old ring on failure) */

/* rom: iNES image (stays where it is, e.g. memory-mapped flash) */
int nes_start(const uint8_t *rom, size_t len, int sample_rate, int pal);
void nes_stop(void);                 /* frees all game memory */
void nes_reset_game(void);
void nes_begin_frame(void);          /* call before waking the display for a drawn frame */
void nes_run_frame(int draw);        /* one frame + its audio (gb_audio_out) */
void nes_set_pad(uint8_t pad);       /* H@N pad bits (R L U D A B SEL START) */
int nes_is_pal(void);
int nes_mapper(void);

/* 256-entry RGB565 table (native byte order), valid while a game runs */
extern uint16_t *nes_pal565;
/* fill out (256 entries) with palette n (0..5) */
void nes_build_palette(int n, uint16_t *out);
extern const char *const NES_PALETTE_NAMES[];
#define NES_NUM_PALETTES 6

/* battery-backed PRG-RAM (8 KB), NULL/0 when the cartridge has no battery */
uint8_t *nes_sram_ptr(void);
int nes_sram_size(void);
uint32_t nes_sram_sum(void);         /* cheap checksum to spot changes */

/* save states; path is a stdio path (SD card mounted at /sd).
   0 = ok, -2 = state belongs to another game (nothing changed), -1 = file error */
int nes_state_save(const char *path);
int nes_state_load(const char *path);

/* cheats: search space (work RAM, then 6000-7FFF RAM if present) and pokes */
int nes_cheat_space(void);
int nes_cheat_peek(int i);
unsigned nes_cheat_addr(int i);
void nes_cheat_poke(unsigned addr, int v);

#ifdef __cplusplus
}
#endif
