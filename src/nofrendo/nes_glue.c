/*
 * H@N-CYD-GBC: glue between the nofrendo NES core (from retro-go) and the board.
 *
 * - The ROM is not copied into RAM: rom_loadmem() points PRG/CHR-ROM straight
 *   at the memory-mapped flash copy.
 * - No frame buffer: each finished line (256 palette indices) goes into a small
 *   ring that the display task (core 0) reads, using the same gb_lines_out /
 *   gb_lines_need counters as the Game Boy core.
 * - Audio: one frame of mono 16-bit samples, converted to 8-bit for gb_audio_out.
 */
#include <stdlib.h>
#include <string.h>
#include "nes_glue.h"
#define NES_GLUE_IMPL   /* this file implements the allocator; no malloc macros here */

#include "nes/nes.h"
#include "palettes.h"

/* shared with the Game Boy core (gnuboy/lcd.c) */
extern volatile int gb_lines_out, gb_lines_need;
void gb_audio_out(const uint8_t *samples, int count);
void *gb_scratch(size_t *size);

/* ---- arena: bump allocator over the Game Boy core's idle video memory ---- */
static uint8_t *arena = NULL;
static size_t arena_size = 0, arena_used = 0;

static int in_arena(const void *p)
{
    return arena && (const uint8_t *)p >= arena && (const uint8_t *)p < arena + arena_size;
}

void *nes_malloc(size_t n)
{
    size_t a = (n + 7) & ~(size_t)7;
    if (arena && a <= arena_size - arena_used) {
        void *p = arena + arena_used;
        arena_used += a;
        return p;
    }
    return malloc(n);
}

void *nes_calloc(size_t n, size_t size)
{
    void *p = nes_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *nes_realloc(void *p, size_t n)
{
    if (!p) return nes_malloc(n);
    if (!in_arena(p)) return realloc(p, n);
    void *q = malloc(n);   /* grow out of the arena (rare: MMC3 variants) */
    if (q) {
        size_t avail = (size_t)(arena + arena_size - (uint8_t *)p);
        memcpy(q, p, n < avail ? n : avail);
    }
    return q;
}

void nes_free(void *p)
{
    if (p && !in_arena(p)) free(p);   /* arena memory is dropped all at once in nes_stop() */
}

uint8_t *nes_ring = NULL;
int nes_ring_lines = 0;

static rom_t *cart = NULL;
static int started = 0;
#define ABUF 512
static uint8_t *abuf = NULL;            /* audio conversion buffer (arena) */
static uint8_t *linebuf = NULL;         /* one rendered line (arena) */
uint16_t *nes_pal565 = NULL;            /* palette index -> RGB565 (arena) */

const char *const NES_PALETTE_NAMES[] = { "Nofrendo", "Composite", "NES Classic", "NTSC", "PVM", "Smooth" };

int nes_ring_alloc(int lines)
{
    if (nes_ring && lines == nes_ring_lines) return 0;
    free(nes_ring);
    nes_ring = NULL;
    nes_ring_lines = 0;
    uint8_t *r = (uint8_t *)malloc((size_t)lines * NES_W);
    if (!r) return -1;
    memset(r, 0, (size_t)lines * NES_W);
    nes_ring = r;
    nes_ring_lines = lines;
    return 0;
}

/* called by nes_emulate() for every visible line of a drawn frame */
void nes_line_out(int line, const uint8 *pixels)
{
    /* wait until the display has copied the line this slot held before */
    while (line - gb_lines_need >= nes_ring_lines) { }
    memcpy(nes_ring + (line % nes_ring_lines) * NES_W, pixels, NES_W);
    __sync_synchronize();
    gb_lines_out = line + 1;
}

/* before a drawn frame (and before the display task is woken): fresh handoff */
void nes_begin_frame(void)
{
    gb_lines_need = 0;
    gb_lines_out = 0;
    __sync_synchronize();
}

void nes_frame_done(void)
{
    __sync_synchronize();
    gb_lines_out = NES_H;
}

void nes_build_palette(int n, uint16_t *out)
{
    if (n < 0 || n >= NES_NUM_PALETTES) n = 0;
    const uint8_t *p = nes_palettes[n];
    for (int i = 0; i < 64; i++) {
        const uint8_t *rgb = &p[i * 3];
        uint16_t c = (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
        out[i] = out[i + 64] = out[i + 128] = c;   /* the flag bits (sprite priority) share colors */
    }
    for (int i = 0; i < 64; i++) {
        const uint8_t *rgb = &gui_pal[(i & 7) * 3];
        out[192 + i] = (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
    }
}

int nes_start(const uint8_t *rom, size_t len, int sample_rate, int pal)
{
    nes_stop();
    arena = (uint8_t *)gb_scratch(&arena_size);
    arena_used = 0;
    memset(arena, 0, arena_size);
    nes_t *nes = nes_init(pal ? SYS_NES_PAL : SYS_DETECT, sample_rate, false, NULL);
    if (!nes) return -1;
    /* rom_loadmem only reads the image (PRG/CHR point into it) */
    cart = rom_loadmem((uint8 *)rom, len);
    if (!cart) { nes_shutdown(); return -2; }
    if (pal) cart->system = SYS_NES_PAL;
    if (cart->type != ROM_TYPE_INES) { nes_shutdown(); cart = NULL; return -3; }
    int r = nes_insertcart(cart);   /* calls nes_shutdown() itself on failure */
    if (r < 0) { cart = NULL; return r - 10; }
    abuf = (uint8_t *)nes_malloc(ABUF);
    linebuf = (uint8_t *)nes_malloc(NES_SCREEN_PITCH);
    nes_pal565 = (uint16_t *)nes_malloc(256 * sizeof(uint16_t));
    if (!abuf || !linebuf || !nes_pal565) { nes_shutdown(); cart = NULL; return -1; }
    input_connect(0, NES_JOYPAD);
    /* line 0 of the first drawn frame resets the handoff; until then the display is free */
    gb_lines_out = NES_H;
    started = 1;
    return 0;
}

void nes_stop(void)
{
    if (started) nes_shutdown();
    started = 0;
    cart = NULL;
    if (abuf && !in_arena(abuf)) free(abuf);
    if (linebuf && !in_arena(linebuf)) free(linebuf);
    if (nes_pal565 && !in_arena(nes_pal565)) free(nes_pal565);
    abuf = linebuf = NULL;
    nes_pal565 = NULL;
    arena = NULL;
    arena_size = arena_used = 0;
    free(nes_ring);
    nes_ring = NULL;
    nes_ring_lines = 0;
}

void nes_reset_game(void)
{
    if (started) nes_reset(true);
}

int nes_is_pal(void) { return started && nes_getptr()->system == SYS_NES_PAL; }
int nes_mapper(void) { return cart ? cart->mapper_number : -1; }

void nes_run_frame(int draw)
{
    nes_t *nes = nes_getptr();
    /* nes_emulate() renders only when vidbuf is set: point it at a one-line buffer */
    nes->vidbuf = draw ? linebuf : NULL;
    nes_emulate(draw != 0);
    if (!draw) {   /* skipped frame: keep the display released */
        gb_lines_out = NES_H;
    }
    apu_t *apu = nes->apu;
    const short *s = apu->buffer;
    int n = apu->samples_per_frame;
    while (n > 0) {
        int k = n < ABUF ? n : ABUF;
        for (int i = 0; i < k; i++) abuf[i] = (uint8_t)((s[i] >> 8) + 128);
        gb_audio_out(abuf, k);
        s += k;
        n -= k;
    }
}

void nes_set_pad(uint8_t pad)
{
    /* H@N bits: 0x01 R, 0x02 L, 0x04 U, 0x08 D, 0x10 A, 0x20 B, 0x40 SEL, 0x80 START */
    int st = 0;
    if (pad & 0x01) st |= NES_PAD_RIGHT;
    if (pad & 0x02) st |= NES_PAD_LEFT;
    if (pad & 0x04) st |= NES_PAD_UP;
    if (pad & 0x08) st |= NES_PAD_DOWN;
    if (pad & 0x10) st |= NES_PAD_A;
    if (pad & 0x20) st |= NES_PAD_B;
    if (pad & 0x40) st |= NES_PAD_SELECT;
    if (pad & 0x80) st |= NES_PAD_START;
    input_update(0, st);
}

uint8_t *nes_sram_ptr(void)
{
    return (cart && cart->battery && cart->prg_ram_banks > 0) ? cart->prg_ram : NULL;
}

int nes_sram_size(void)
{
    return nes_sram_ptr() ? cart->prg_ram_banks * ROM_PRG_BANK_SIZE : 0;
}

uint32_t nes_sram_sum(void)
{
    const uint8_t *p = nes_sram_ptr();
    int n = nes_sram_size();
    uint32_t a = 1, b = 0;   /* Adler-32 style */
    for (int i = 0; i < n; i++) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

/* save states (nes/state.c, SNSS format + H@N blocks). path is a stdio path
   (on the board the SD card is mounted at /sd). 0 = ok, -2 = another game's state,
   -1 = file error (on load the game may be half restored: reset it). */
int nes_state_save(const char *path)
{
    return started ? state_save(path) : -1;
}

int nes_state_load(const char *path)
{
    return started ? state_load(path) : -1;
}

/* ---------------- cheats: game memory by address ----------------
 * Searchable: work RAM 0000-07FF, then battery/work RAM 6000-7FFF when the cart has it. */
int nes_cheat_space(void)
{
    if (!started) return 0;
    return 0x800 + ((cart && cart->prg_ram && cart->prg_ram_banks > 0) ? 0x2000 : 0);
}

int nes_cheat_peek(int i)
{
    if (!started) return 0;
    if (i < 0x800) return nes_getptr()->mem->ram[i];
    return cart->prg_ram[i - 0x800];
}

unsigned nes_cheat_addr(int i) { return i < 0x800 ? (unsigned)i : 0x6000u + (unsigned)(i - 0x800); }

void nes_cheat_poke(unsigned addr, int v)
{
    if (!started) return;
    if (addr < 0x2000) nes_getptr()->mem->ram[addr & 0x7FF] = (uint8)v;
    else if (addr >= 0x6000 && addr < 0x8000 && cart && cart->prg_ram && cart->prg_ram_banks > 0)
        cart->prg_ram[addr - 0x6000] = (uint8)v;
}
