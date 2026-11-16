#ifndef __LCD_H__
#define __LCD_H__

#include "defs.h"

struct vissprite
{
	short pat;
	short x;
	short v;
	byte pal;
	byte pri;
	//byte pad[2]; //6
};

struct scan
{
	int bg[64];
	int wnd[64];
	byte buf[256];
	un16 pal2[64];
	byte pri[256];
	struct vissprite vs[16];
	int ns, l, x, y, s, t, u, v, wx, wy, wt, wv;
};

struct obj
{
	byte y;
	byte x;
	byte pat;
	byte flags;
};

struct lcd
{
	byte vbank[2][8192];
	union
	{
		byte mem[256];
		struct obj obj[40];
	} oam;
	byte pal[128];
};

extern struct lcd lcd;
extern struct scan scan;


void lcd_begin();
void lcd_refreshline();
void pal_write(int i, byte b);
void pal_write_dmg(int i, int mapnum, byte d);
void vram_write(int a, byte b);
void pal_dirty();
void vram_dirty();
void lcd_reset();
void lcd_set_dmg_palette(const int *p);

/* Rendered lines go through a ring of GB_RING RGB565 lines to the display task:
 * gb_lines_out = lines finished in this frame, gb_lines_need = oldest line the
 * display still needs (the emulator waits rather than overwrite it). */
#define GB_RING 32
#define GB_PAL_POOL 32
extern byte (*gb_ring)[160];
extern byte *gb_ring_pal;
extern un16 (*gb_pal_pool)[64];
extern int gb_ring_lines;
extern volatile int gb_lines_out, gb_lines_need;
extern void (*gb_line_hook)(int line, const un16 *pixels);

void lcd_snap_reset();
//void bg_scan_color();
void updatepatpix();

/* lcdc.c */
void lcdc_trans();
void lcdc_change(byte b);
void stat_write(byte b);
void stat_trigger();


#endif
