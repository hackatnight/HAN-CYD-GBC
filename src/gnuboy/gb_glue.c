/*
 * Glue between the gnuboy core and the CYD front end (main.cpp).
 * Replaces gnuboy.c / emu.c from GBCCAT.
 */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <esp_timer.h>

#include "defs.h"
#include "regs.h"
#include "hw.h"
#include "cpu.h"
#include "mem.h"
#include "lcd.h"
#include "rtc.h"
#include "sys.h"
#include "fb.h"
#include "pcm.h"
#include "sound.h"
#include "gb_glue.h"
void loader_unload();

extern byte *vdest;
void gb_bank_sample(void);
struct pcm pcm;
struct fb fb;

/* ---- stubs the core expects ---- */
void vid_setpal(int i, int r, int g, int b) { (void)i; (void)r; (void)g; (void)b; }
void vid_begin() {}
void vid_end() {}
void doevents() {}
unsigned long sys_micros() { return (unsigned long)esp_timer_get_time(); }
void sys_sleep(int us) { (void)us; }
void *sys_timer() { static uint32_t t; return &t; }
int sys_elapsed(uint32_t *prev)
{
	uint32_t now = (uint32_t)esp_timer_get_time();
	int d = (int)(now - *prev);
	*prev = now;
	return d;
}

int pcm_submit()
{
	if (pcm.buf && pcm.pos > 0)
		gb_audio_out(pcm.buf, pcm.pos);
	pcm.pos = 0;
	return 0;
}

/* ---- setup ---- */
int gb_glue_init(int sample_rate)
{
	memset(&fb, 0, sizeof fb);
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 1;      /* 8-bit palette indices, see lcd.c */
	fb.pitch = 160;
	fb.indexed = 0;
	fb.enabled = 1;
	fb.dirty = 1;

	if (!gb_ring && gb_ring_alloc(GB_RING) != 0) return -1;
	if (!pcm.buf) pcm.buf = (byte *)malloc(512);
	if (!pcm.buf) return -1;
	pcm.len = 512;
	pcm.hz = sample_rate;
	pcm.stereo = 0;
	pcm.pos = 0;
	return 0;
}

void gb_set_framebuffer(uint8_t *buf, int render)
{
	fb.ptr = (byte *)buf;   /* unused: lines go through gb_ring */
	fb.enabled = render;
}

void gb_reset()
{
	hw_reset();
	lcd_reset();
	cpu_reset();
	mbc_reset();
	sound_reset();
	pcm.pos = 0;
}

/* Finish VBlank: run until LY wraps to 0 (start of the next frame).
 * lcd_begin() latches fb.ptr at that point. */
void gb_run_vblank()
{
	if (!(R_LCDC & 0x80))
		cpu_emulate(32832);
	while (R_LY > 0)
		cpu_emulate(cpu.lcdc);
	/* LY == 0 and line 0 not drawn yet: point the scanline writer at this
	 * frame's buffer (also covers the first frame after reset). */
	if (fb.enabled)
	{
		/* a frame the display will draw: start a fresh line handoff */
		gb_lines_need = 0;
		gb_lines_out = 0;
		lcd_snap_reset();
	}
}

/* end of a drawn frame: release the display even if the LCD was off */
void gb_frame_end()
{
	__sync_synchronize();
	gb_lines_out = 144;
}

/* Run visible lines 0..143; returns when VBlank starts (frame complete). */
void gb_run_visible()
{
	cpu_emulate(2280);
	while (R_LY > 0 && R_LY < 144)
	{
		cpu_emulate(cpu.lcdc);
		gb_bank_sample();
	}
	rtc_tick();
	sound_mix();
	pcm_submit();
}

void gb_set_pad(uint8_t pad)
{
	static uint8_t old;
	uint8_t delta = pad ^ old;
	if (!delta) return;
	for (int b = 0; b < 8; b++)
		if (delta & (1 << b)) pad_set(1 << b, pad & (1 << b));
	old = pad;
	pad_refresh();
}

int gb_is_cgb() { return hw.cgb; }
int gb_sram_size() { return mbc.batt ? 8192 * mbc.ramsize : 0; }
int gb_sram_has_battery() { return mbc.batt; }
int gb_sram_dirty() { return ram.sram_dirty; }
volatile unsigned gb_sram_writes = 0;   /* cartridge RAM writes (counted in mem_write) */
void gb_sram_clear_dirty() { ram.sram_dirty = 0; }
int gb_has_rtc() { return rtc.batt; }
void *gb_rtc_ptr() { return &rtc; }
int gb_rtc_size() { return sizeof(rtc); }
const char *gb_rom_title() { return rom.name; }



/* Work RAM: 32 KB on GBC, 8 KB on DMG. Call after rom_load(). 0 = ok */
int gb_alloc_wram()
{
	int i, banks = hw.cgb ? 8 : 2;
	for (i = 0; i < 8; i++)
	{
		if (ram.ibank[i]) free(ram.ibank[i]);
		ram.ibank[i] = 0;
	}
	gb_svbk_mask = hw.cgb ? 7 : 0;
	for (i = 0; i < banks; i++)
	{
		ram.ibank[i] = (byte *)calloc(1, 4096);
		if (!ram.ibank[i]) return -1;
	}
	return 0;
}

void gb_free_game_memory()
{
	int i;
	gb_rom_stream_release();
	loader_unload();   /* frees cart RAM */
	for (i = 0; i < 8; i++)
	{
		if (ram.ibank[i]) free(ram.ibank[i]);
		ram.ibank[i] = 0;
	}
}

/* KB of RAM a game needs: work RAM + cartridge RAM. Call after rom_load(). */
int gb_game_ram_kb() { return (hw.cgb ? 32 : 8) + 8 * mbc.ramsize; }
int gb_wram_kb() { return hw.cgb ? 32 : 8; }
int gb_rom_banks() { return mbc.romsize; }

/* ---------------- cartridge RAM paging ----------------
 * nslots 4 KB buffers hold the resident pages; with nslots == ram.spages
 * everything is resident and the SD card is only touched on save.        */
static byte *slot_buf[32];
static int slot_page[32];
static int nslots, hand;
static int sram_errors;
unsigned gb_sram_faults;

int gb_sram_pages() { return ram.spages; }
int gb_sram_slots() { return nslots; }

void gb_sram_release(void)
{
	int i;
	for (i = 0; i < 32; i++)
	{
		if (i < nslots && slot_buf[i]) free(slot_buf[i]);
		slot_buf[i] = 0;
		slot_page[i] = -1;
		ram.spage[i] = 0;
	}
	nslots = hand = 0;
	ram.sref = ram.sdirty = 0;
}

/* Allocate up to max_slots page buffers (at least 1) and preload pages 0..n-1.
 * from_file: read initial contents with gb_swap_read(), else fill with 0xFF.
 * Returns the number of slots, or 0 on failure. */
int gb_sram_setup(int max_slots, int from_file)
{
	int i, want = ram.spages;
	gb_sram_release();
	if (want <= 0) return 0;
	if (max_slots < want) want = max_slots;
	if (want < 1) want = 1;
	for (i = 0; i < want; i++)
	{
		slot_buf[i] = (byte *)malloc(4096);
		if (!slot_buf[i]) break;
	}
	nslots = i;
	if (nslots < 1) return 0;
	for (i = 0; i < nslots; i++)
	{
		if (!from_file || gb_swap_read(i, slot_buf[i]) != 0)
			memset(slot_buf[i], 0xFF, 4096);
		slot_page[i] = i;
		ram.spage[i] = slot_buf[i];
	}
	ram.sref = ram.sdirty = 0;
	sram_errors = 0;
	gb_sram_faults = 0;
	return nslots;
}

byte *gb_sram_fault(int p)
{
	int s, old, guard = 0;
	if (p < 0 || p >= ram.spages || nslots < 1) p = 0;
	if (ram.spage[p]) return ram.spage[p];
	/* clock: skip recently referenced pages once */
	for (;;)
	{
		s = hand;
		hand = (hand + 1) % nslots;
		old = slot_page[s];
		if (old >= 0 && (ram.sref & (1u << old)) && guard++ < nslots)
		{
			ram.sref &= ~(1u << old);
			continue;
		}
		break;
	}
	if (old >= 0)
	{
		if (ram.sdirty & (1u << old))
		{
			if (gb_swap_write(old, slot_buf[s]) != 0) sram_errors++;
			ram.sdirty &= ~(1u << old);
		}
		ram.spage[old] = 0;
	}
	if (gb_swap_read(p, slot_buf[s]) != 0)
	{
		memset(slot_buf[s], 0xFF, 4096);
		sram_errors++;
	}
	slot_page[s] = p;
	ram.spage[p] = slot_buf[s];
	gb_sram_faults++;
	return slot_buf[s];
}

/* Write every dirty resident page to the save file. Returns 0 on success. */
int gb_sram_flush(void)
{
	int s, err = 0;
	for (s = 0; s < nslots; s++)
	{
		int p = slot_page[s];
		if (p >= 0 && (ram.sdirty & (1u << p)))
		{
			if (gb_swap_write(p, slot_buf[s]) != 0) err++;
			else ram.sdirty &= ~(1u << p);
		}
	}
	if (!err) ram.sram_dirty = 0;
	return err || sram_errors;
}

/* ---------------- ROM streaming ----------------
 * For ROMs bigger than the flash partition the app copies a chosen set of
 * banks to flash (gb_bank_map[bank] -> flash copy). Every other bank is read
 * from the ROM file on the SD card in 4 KB pages into a small pool (clock
 * replacement). gb_bank_use[] records how busy each bank is so the app can
 * put the busiest banks in flash next time.                              */
byte **gb_bank_map;
uint32_t *gb_bank_use;
static byte **rs_buf;
static int *rs_page;          /* slot -> page (bank*4 + k), -1 = free */
static short *rs_slot;        /* page -> slot, -1 = not resident */
static unsigned char *rs_ref;
static int rs_nslots, rs_npages, rs_hand;
unsigned gb_rom_faults;

void gb_rom_stream_release(void)
{
	int i;
	if (rs_buf) for (i = 0; i < rs_nslots; i++) free(rs_buf[i]);
	free(rs_buf); free(rs_page); free(rs_slot); free(rs_ref);
	rs_buf = 0; rs_page = 0; rs_slot = 0; rs_ref = 0;
	rs_nslots = rs_npages = rs_hand = 0;
	gb_bank_map = 0;   /* owned by the app */
	gb_bank_use = 0;   /* owned by the app */
}

/* bank_map: per bank, flash pointer or NULL (streamed); use: per-bank usage
 * counters (may be NULL). Allocates up to max_slots 4 KB pages. */
int gb_rom_stream_setup(byte **bank_map, uint32_t *use, int total_banks, int max_slots)
{
	int i;
	gb_rom_stream_release();
	rs_npages = total_banks * 4;
	rs_slot = (short *)malloc(rs_npages * sizeof(short));
	rs_ref = (unsigned char *)calloc(rs_npages, 1);
	rs_buf = (byte **)calloc(max_slots > 0 ? max_slots : 1, sizeof(byte *));
	rs_page = (int *)malloc((max_slots > 0 ? max_slots : 1) * sizeof(int));
	if (!rs_slot || !rs_ref || !rs_buf || !rs_page) { gb_rom_stream_release(); return 0; }
	for (i = 0; i < rs_npages; i++) rs_slot[i] = -1;
	for (i = 0; i < max_slots; i++)
	{
		rs_buf[i] = (byte *)malloc(4096);
		if (!rs_buf[i]) break;
		rs_page[i] = -1;
	}
	rs_nslots = i;
	gb_bank_map = bank_map;
	gb_bank_use = use;
	gb_rom_faults = 0;
	return rs_nslots;
}

int gb_rom_stream_slots() { return rs_nslots; }

/* sample which bank is mapped (called a few hundred times per frame) */
void gb_bank_sample(void)
{
	if (gb_bank_use && mbc.romsize)
		gb_bank_use[mbc.rombank & (mbc.romsize - 1)]++;
}

byte *gb_rom_page(int bank, int k)
{
	int g = bank * 4 + k;
	if (g < 0 || g >= rs_npages || rs_slot[g] < 0) return 0;
	rs_ref[g] = 1;
	return rs_buf[rs_slot[g]];
}

byte *gb_rom_fault(int bank, int k)
{
	int g = bank * 4 + k, s, old, guard = 0;
	if (g < 0 || g >= rs_npages || rs_nslots < 1)
		return rom.bank[0];   /* can't happen (bank is masked); any readable 4 KB will do */
	if (rs_slot[g] >= 0) { rs_ref[g] = 1; return rs_buf[rs_slot[g]]; }
	for (;;)
	{
		s = rs_hand;
		rs_hand = (rs_hand + 1) % rs_nslots;
		old = rs_page[s];
		if (old >= 0 && rs_ref[old] && guard++ < 2 * rs_nslots) { rs_ref[old] = 0; continue; }
		break;
	}
	if (old >= 0) rs_slot[old] = -1;
	if (gb_rom_read((unsigned)bank * 16384u + (unsigned)k * 4096u, rs_buf[s], 4096) != 0)
		memset(rs_buf[s], 0xFF, 4096);
	rs_page[s] = g;
	rs_slot[g] = (short)s;
	rs_ref[g] = 1;
	gb_rom_faults++;
	/* (no extra usage weight here: it would make streamed banks look busier
	 * than equally used flash banks and the layout would never settle) */
	mem_updatemap();   /* map the new page, drop the evicted one */
	return rs_buf[s];
}

/* ---------------- learned flash layout ----------------
 * Choose which banks live in the slots flash slots: bank 0 always in slot 0,
 * the rest by usage. Banks that stay keep their slot; a bank only replaces a
 * resident one when it's clearly busier (avoids rewriting flash for nothing).
 * cur: current layout (slot -> bank) or NULL. Returns slots that changed.   */
static const uint32_t *plan_use;
static int cmp_use_desc(const void *a, const void *b)
{
	uint32_t ua = plan_use[*(const short *)a], ub = plan_use[*(const short *)b];
	if (ua != ub) return ua < ub ? 1 : -1;
	return *(const short *)a - *(const short *)b;
}

int gb_plan_layout(const uint32_t *use, int nb, int slots, const short *cur, short *out)
{
	short *order, *add, *evict;
	unsigned char *want, *have;
	int i, n, na = 0, ne = 0, changes = 0;
	if (slots > nb) slots = nb;
	order = (short *)malloc(nb * sizeof(short));
	add = (short *)malloc(nb * sizeof(short));
	evict = (short *)malloc(nb * sizeof(short));
	want = (unsigned char *)calloc(nb, 1);
	have = (unsigned char *)calloc(nb, 1);
	if (!order || !add || !evict || !want || !have) { changes = -1; goto done; }

	plan_use = use;
	for (i = 1, n = 0; i < nb; i++) order[n++] = (short)i;
	qsort(order, n, sizeof(short), cmp_use_desc);
	want[0] = 1;
	for (i = 0; i < slots - 1 && i < n; i++) want[order[i]] = 1;

	if (!cur)
	{
		/* first layout: bank 0 then the wanted banks in bank order */
		int s = 0;
		for (i = 0; i < nb && s < slots; i++) if (want[i]) out[s++] = (short)i;
		changes = slots;
		goto done;
	}
	for (i = 0; i < slots; i++) { out[i] = cur[i]; have[cur[i]] = 1; }
	/* candidates: wanted but not in flash (busiest first) */
	for (i = 0; i < n; i++) if (want[order[i]] && !have[order[i]]) add[na++] = order[i];
	/* victims: in flash but not wanted (least busy first) */
	for (i = n - 1; i >= 0; i--) if (have[order[i]] && !want[order[i]]) evict[ne++] = order[i];
	for (i = 0; i < na && i < ne; i++)
	{
		uint32_t ua = use[add[i]], ue = use[evict[i]];
		int s;
		if (ua <= ue + ue / 4 + 16) break;           /* not worth a flash rewrite */
		for (s = 1; s < slots; s++) if (out[s] == evict[i]) break;
		if (s >= slots) continue;
		out[s] = add[i];
		changes++;
	}
done:
	free(order); free(add); free(evict); free(want); free(have);
	return changes;
}

/* Line ring between the emulator and the display. Bigger = the emulator waits
 * less for the display (smoother); smaller = more RAM for big ROMs. */
/* The Game Boy video memory block (~17 KB, static) is idle while another core
   runs; lcd_reset() clears it whenever a Game Boy game starts. */
void *gb_scratch(size_t *size)
{
	*size = sizeof lcd;
	return &lcd;
}

/* free the line ring between games (it can be 45 KB); gb_glue_init() makes a new one */
void gb_ring_free(void)
{
	free(gb_ring);
	gb_ring = 0;
	gb_ring_lines = 0;
}

int gb_ring_alloc(int lines)
{
	un16 (*r)[160];
	if (gb_ring && lines == gb_ring_lines) return 0;
	free(gb_ring);
	gb_ring = 0;
	r = (un16 (*)[160])malloc((size_t)lines * 160 * sizeof(un16));
	if (!r)
	{
		lines = GB_RING;
		r = (un16 (*)[160])malloc((size_t)lines * 160 * sizeof(un16));
		if (!r) return -1;
	}
	gb_ring = r;
	gb_ring_lines = lines;
	return 0;
}


/* ---------------- save states ----------------
 * One file with everything the emulated Game Boy holds: CPU, I/O registers, video
 * memory, sound, clock, memory-bank controller, work RAM banks and all cartridge RAM
 * pages (resident ones from memory, the rest from the swap/.sav file). Pointers are
 * not stored; mem_updatemap() rebuilds them after loading. Taken between frames. */
#define ST_MAGIC "HANGBST1"
struct st_hdr
{
	char magic[8];
	uint32_t cgb, banks, spages, rombanks;
	uint32_t gchk;
	uint32_t sz_cpu, sz_hw, sz_lcd, sz_snd, sz_rtc;
};

static void st_fill(struct st_hdr *h)
{
	memset(h, 0, sizeof *h);
	memcpy(h->magic, ST_MAGIC, 8);
	h->cgb = hw.cgb;
	h->banks = hw.cgb ? 8 : 2;
	h->spages = ram.spages;
	h->rombanks = mbc.romsize;
	h->gchk = (rom.bank[0][0x14E] << 8) | rom.bank[0][0x14F] | (rom.bank[0][0x14D] << 16);
	h->sz_cpu = sizeof cpu;
	h->sz_hw = sizeof hw;
	h->sz_lcd = sizeof lcd;
	h->sz_snd = sizeof snd;
	h->sz_rtc = sizeof rtc;
}

int gb_state_save(gb_io_fn write, void *ctx)
{
	struct st_hdr h;
	int m[8], i, p;
	byte *tmp = 0;
	st_fill(&h);
	m[0] = mbc.type; m[1] = mbc.model; m[2] = mbc.rombank; m[3] = mbc.rambank;
	m[4] = mbc.romsize; m[5] = mbc.ramsize; m[6] = mbc.enableram; m[7] = mbc.batt;
	if (write(ctx, &h, sizeof h) || write(ctx, &cpu, sizeof cpu) || write(ctx, &hw, sizeof hw) ||
	    write(ctx, ram.hi, sizeof ram.hi) || write(ctx, &lcd, sizeof lcd) || write(ctx, &snd, sizeof snd) ||
	    write(ctx, &rtc, sizeof rtc) || write(ctx, m, sizeof m))
		return -1;
	for (i = 0; i < (int)h.banks; i++)
		if (write(ctx, ram.ibank[i], 4096)) return -1;
	for (p = 0; p < ram.spages; p++)
	{
		byte *src = ram.spage[p];
		if (!src)
		{
			/* not resident: read it from the swap/.sav file (dirty pages are always resident) */
			if (!tmp && !(tmp = (byte *)malloc(4096))) return -1;
			if (gb_swap_read(p, tmp) != 0) memset(tmp, 0xFF, 4096);
			src = tmp;
		}
		if (write(ctx, src, 4096)) { free(tmp); return -1; }
	}
	free(tmp);
	return 0;
}

/* 0 = loaded; -2 = state is for another game/version (nothing changed);
   -1 = read error part-way (the game must be reset) */
int gb_state_load(gb_io_fn read, void *ctx)
{
	struct st_hdr h, cur;
	int m[8], i, p, rate;
	byte *tmp = 0;
	st_fill(&cur);
	if (read(ctx, &h, sizeof h) || memcmp(&h, &cur, sizeof h) != 0) return -2;
	rate = snd.rate;   /* host sample rate, not part of the game */
	if (read(ctx, &cpu, sizeof cpu) || read(ctx, &hw, sizeof hw) || read(ctx, ram.hi, sizeof ram.hi) ||
	    read(ctx, &lcd, sizeof lcd) || read(ctx, &snd, sizeof snd) || read(ctx, &rtc, sizeof rtc) ||
	    read(ctx, m, sizeof m))
		return -1;
	snd.rate = rate;
	mbc.type = m[0]; mbc.model = m[1]; mbc.rombank = m[2]; mbc.rambank = m[3];
	mbc.romsize = m[4]; mbc.ramsize = m[5]; mbc.enableram = m[6]; mbc.batt = m[7];
	for (i = 0; i < (int)h.banks; i++)
		if (read(ctx, ram.ibank[i], 4096)) return -1;
	for (p = 0; p < ram.spages; p++)
	{
		if (ram.spage[p])
		{
			if (read(ctx, ram.spage[p], 4096)) return -1;
			ram.sdirty |= 1u << p;   /* written to the .sav/swap file on the next flush */
		}
		else
		{
			if (!tmp && !(tmp = (byte *)malloc(4096))) return -1;
			if (read(ctx, tmp, 4096) || gb_swap_write(p, tmp) != 0) { free(tmp); return -1; }
		}
	}
	free(tmp);
	if (ram.spages) ram.sram_dirty = 1;
	mem_updatemap();
	pal_dirty();
	vram_dirty();
	return 0;
}

/* size of a state file for the running game (to reject truncated files up front) */
long gb_state_size(void)
{
	return (long)sizeof(struct st_hdr) + sizeof cpu + sizeof hw + sizeof ram.hi + sizeof lcd + sizeof snd +
	       sizeof rtc + 8 * sizeof(int) + (long)(hw.cgb ? 8 : 2) * 4096 + (long)ram.spages * 4096;
}

/* drop unsaved cartridge RAM changes: re-read the resident pages from the file */
void gb_sram_reload(void)
{
	int s;
	for (s = 0; s < nslots; s++)
	{
		int p = slot_page[s];
		if (p >= 0 && gb_swap_read(p, slot_buf[s]) != 0) memset(slot_buf[s], 0xFF, 4096);
	}
	ram.sdirty = 0;
	ram.sram_dirty = 0;
}

/* ---------------- cheats: game memory by address ----------------
 * The finder searches work RAM (2 or 8 banks of 4 KB) and high RAM (FF80-FFFE);
 * cheats can also write cartridge RAM (A000-BFFF). */
int gb_cheat_space(void) { return (hw.cgb ? 8 : 2) * 4096 + 127; }

int gb_cheat_peek(int i)
{
	int banks = hw.cgb ? 8 : 2;
	if (i < banks * 4096) return ram.ibank[i >> 12] ? ram.ibank[i >> 12][i & 4095] : 0;
	return ram.hi[0x80 + (i - banks * 4096)];
}

/* (bank << 16) | address of search index i; bank 0 = C000-CFFF, 1-7 = D000-DFFF */
unsigned gb_cheat_addr(int i)
{
	int banks = hw.cgb ? 8 : 2;
	if (i < banks * 4096)
	{
		int b = i >> 12;
		return ((unsigned)b << 16) | (b ? 0xD000u : 0xC000u) | (unsigned)(i & 4095);
	}
	return 0xFF80u + (unsigned)(i - banks * 4096);
}

/* bank: 1-7 = that work RAM bank for D000-DFFF, anything else = the bank in use */
void gb_cheat_poke(int bank, unsigned addr, int v)
{
	int banks = hw.cgb ? 8 : 2;
	if (addr >= 0xE000 && addr < 0xFE00) addr -= 0x2000;   /* echo RAM */
	if (addr >= 0xC000 && addr < 0xD000)
	{
		if (ram.ibank[0]) ram.ibank[0][addr & 0xFFF] = (byte)v;
	}
	else if (addr >= 0xD000 && addr < 0xE000)
	{
		int b = (bank >= 1 && bank <= 7) ? bank : (R_SVBK & gb_svbk_mask);
		if (b == 0 || b >= banks) b = 1;
		if (ram.ibank[b]) ram.ibank[b][addr & 0xFFF] = (byte)v;
	}
	else if (addr >= 0xFF80 && addr < 0xFFFF)
	{
		ram.hi[addr & 0xFF] = (byte)v;
	}
	else if (addr >= 0xA000 && addr < 0xC000 && ram.spages > 0)
	{
		int p = ((mbc.rambank < mbc.ramsize ? mbc.rambank : 0) << 1) | ((addr >> 12) & 1);
		if (p < ram.spages)
		{
			byte *pg = ram.spage[p] ? ram.spage[p] : gb_sram_fault(p);
			if (pg[addr & 0xFFF] != (byte)v)
			{
				pg[addr & 0xFFF] = (byte)v;
				ram.sdirty |= 1u << p;
				ram.sram_dirty = 1;
			}
		}
	}
}
