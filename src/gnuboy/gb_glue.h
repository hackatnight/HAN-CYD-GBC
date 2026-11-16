#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* implemented in main.cpp */
void gb_audio_out(const uint8_t *samples, int count);
/* implemented in gb_glue.c */
int gb_glue_init(int sample_rate);
void gb_set_framebuffer(uint8_t *buf, int render);
/* 8-bit frame buffer helpers: per-line palettes (RGB565, native order) */
void gb_frame_end();
/* line ring (lcd.c) */
#define GB_RING 32
#ifndef GB_PAL_LOG
#define GB_PAL_LOG 1024
typedef struct { unsigned char line, index; unsigned short color; } gb_pal_change_t;
extern uint8_t (*gb_ring)[160];      /* lines of 1-byte color numbers */
extern gb_pal_change_t *gb_pal_log;
void gb_ring_line_rgb(int line, uint16_t *out);   /* display side: that line as RGB565 */
#endif
extern int gb_ring_lines;
/* (re)allocate the line ring; 0 = ok (keeps the old ring on failure) */
int gb_ring_alloc(int lines);
void gb_ring_free(void);
/* idle Game Boy video memory, lent to the NES core (see nes_glue.c) */
#include <stddef.h>
void *gb_scratch(size_t *size);
extern volatile int gb_lines_out, gb_lines_need;
extern void (*gb_line_hook)(int line, const uint16_t *pixels);
void gb_reset();
void gb_run_vblank();
void gb_run_visible();
void gb_set_pad(uint8_t pad);
int gb_is_cgb();
int gb_alloc_wram();
void gb_free_game_memory();
int gb_sram_size();
int gb_game_ram_kb();
int gb_wram_kb();
/* cartridge RAM paging (implemented in gb_glue.c) */
int gb_sram_has_battery();
int gb_sram_pages();
int gb_sram_slots();
int gb_sram_setup(int max_slots, int from_file);
void gb_sram_release(void);
int gb_sram_flush(void);
extern unsigned gb_sram_faults;
/* ROM streaming: banks not in flash are read from the ROM file on demand */
int gb_rom_stream_setup(uint8_t **bank_map, uint32_t *use, int total_banks, int max_slots);
extern uint8_t **gb_bank_map;   /* per bank: flash copy or NULL (streamed from SD) */
extern uint32_t *gb_bank_use;   /* per bank usage score (sampled while running) */
/* pick the banks for the slots flash slots from usage (see gb_glue.c) */
int gb_plan_layout(const uint32_t *use, int nb, int slots, const short *cur, short *out);
void gb_rom_stream_release(void);
int gb_rom_stream_slots();
extern unsigned gb_rom_faults;
int gb_rom_banks();
/* implemented in main.cpp: read len bytes at offset of the ROM file; 0 = ok */
int gb_rom_read(unsigned offset, uint8_t *buf, int len);
/* page I/O, implemented in main.cpp: 4 KB page p at file offset p*4096; 0 = ok */
int gb_swap_read(int page, uint8_t *buf);
int gb_swap_write(int page, const uint8_t *buf);
int gb_sram_dirty();
extern volatile unsigned gb_sram_writes;
void gb_sram_clear_dirty();
int gb_has_rtc();
void *gb_rtc_ptr();
int gb_rtc_size();
const char *gb_rom_title();
/* save states: io returns 0 on success (see gb_glue.c) */
typedef int (*gb_io_fn)(void *ctx, void *buf, int len);
int gb_state_save(gb_io_fn write, void *ctx);
int gb_state_load(gb_io_fn read, void *ctx);
long gb_state_size(void);
void gb_sram_reload(void);
/* cheats (gb_glue.c) */
int gb_cheat_space(void);
int gb_cheat_peek(int i);
unsigned gb_cheat_addr(int i);
void gb_cheat_poke(int bank, unsigned addr, int v);
/* gnuboy */
int rom_load(const uint8_t *data, int len);
void loader_unload();
void lcd_set_dmg_palette(const int *p);
#ifdef __cplusplus
}
#endif
