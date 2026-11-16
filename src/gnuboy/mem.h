#ifndef __MEM_H__
#define __MEM_H__
#include <stdint.h>


#include "defs.h"



#define MBC_NONE 0
#define MBC_MBC1 1
#define MBC_MBC2 2
#define MBC_MBC3 3
#define MBC_MBC5 5
#define MBC_RUMBLE 15
#define MBC_HUC1 0xC1
#define MBC_HUC3 0xC3

struct mbc
{
	int type;
	int model;
	int rombank;
	int rambank;
	int romsize;
	int ramsize;
	int enableram;
	int batt;
	byte *rmap[0x10], *wmap[0x10];
};

struct rom
{
	byte (* bank)[16384];
	char name[20];
	int length;
};

struct ram
{
	byte hi[256];
	/* Banked RAM is allocated one bank at a time, so a game never needs a large
	 * contiguous block (heap is fragmented once Bluetooth is running). */
	byte *ibank[8];        /* work RAM: 8 x 4 KB on GBC, 2 x 4 KB on DMG (gb_alloc_wram) */
	/* Cartridge RAM in 4 KB pages (2 per 8 KB bank). A page that isn't resident
	 * (NULL) is paged in from the .sav file on the SD card by gb_sram_fault(). */
	byte *spage[32];
	int spages;            /* mbc.ramsize * 2 */
	uint32_t sref;         /* page referenced bits (clock replacement) */
	uint32_t sdirty;       /* page dirty bits */
	byte loaded;
	byte sram_dirty;
};


extern struct mbc mbc;
extern struct rom rom;
extern struct ram ram;
extern int gb_svbk_mask;
byte *gb_sram_fault(int page);
/* Big ROMs: gb_bank_map[bank] points at the bank's copy in flash, or is NULL
 * when that bank is paged in from the SD card in 4 KB pages (see gb_glue.c).
 * gb_bank_map == NULL means the whole ROM is in flash at rom.bank. */
extern byte **gb_bank_map;
byte *gb_rom_page(int bank, int k);    /* resident page or NULL */
byte *gb_rom_fault(int bank, int k);   /* page it in */


void mem_updatemap();
void ioreg_write(byte r, byte b);
void mbc_write(int a, byte b);
void mem_write(int a, byte b);
byte mem_read(int a);
void mbc_reset();


#define READB(a) ( mbc.rmap[(a)>>12] \
? mbc.rmap[(a)>>12][(a)] \
: mem_read((a)) )
#define READW(a) ( READB((a)) | ((word)READB((a)+1)<<8) )

#define WRITEB(a, b) ( mbc.wmap[(a)>>12] \
? ( mbc.wmap[(a)>>12][(a)] = (b) ) \
: ( mem_write((a), (b)), (b) ) )
#define WRITEW(a, w) ( WRITEB((a), (w)&0xFF), WRITEB((a)+1, (w)>>8) )


#endif
