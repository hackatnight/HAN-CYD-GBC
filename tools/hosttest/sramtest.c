/* Counts how often cartridge RAM has to be swapped to the SD card for a given number of
 * resident 4 KB pages. A game whose save RAM does not all fit thrashes badly here: with
 * Donkey Kong Country at 1 of its 2 pages resident this reports thousands of card
 * operations, and 2 with both resident.
 *
 *   sramtest ROM FLASHBANKS ROMSLOTS SRAMSLOTS FRAMES
 * Build: gcc -O2 -w -Istub -I../../src/gnuboy ../../src/gnuboy/*.c sramtest.c -lm      */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gb_glue.h"
static uint8_t *full; static unsigned romreads, swapio;
static uint8_t swapfile[128*1024];
int gb_swap_read(int p,uint8_t*b){ memcpy(b,swapfile+(size_t)p*4096,4096); swapio++; return 0; }
int gb_swap_write(int p,const uint8_t*b){ memcpy(swapfile+(size_t)p*4096,b,4096); swapio++; return 0; }
int gb_rom_read(unsigned o,uint8_t*b,int n){ memcpy(b,full+o,n); romreads++; return 0; }
void gb_audio_out(const uint8_t*s,int n){}
void gb_fatal(const char*f,int l,const char*fmt,...){printf("FATAL\n");exit(2);}
static void on_line(int L,const uint16_t*p){ gb_lines_need=L+1; }
int main(int c,char**v){
  int FLASH=atoi(v[2]), romslots=atoi(v[3]), sramslots=atoi(v[4]), frames=atoi(v[5]);
  FILE*f=fopen(v[1],"rb"); full=malloc(4<<20); memset(full,0xff,4<<20);
  int n=fread(full,1,4<<20,f); fclose(f);
  memset(swapfile,0xff,sizeof swapfile);
  uint8_t*flash=malloc((size_t)FLASH*16384); memcpy(flash,full,(size_t)FLASH*16384);
  gb_glue_init(22050); gb_ring_alloc(32); gb_line_hook=on_line;
  if(rom_load(flash,n)||gb_alloc_wram())return 1;
  gb_sram_setup(sramslots,1);
  int nb=gb_rom_banks(); static uint8_t*bm[512]; static uint32_t use[512];
  for(int s=0;s<FLASH&&s<nb;s++) bm[s]=flash+(size_t)s*16384;
  gb_rom_stream_setup(bm,use,nb,romslots);
  gb_reset();
  for(int i=0;i<frames;i++){
    int t=i%120;
    gb_set_pad(t<5?0x80:(t<10?0:(t<15?0x10:(t<20?0:(t<25?0x40:0)))));
    gb_set_framebuffer(0,1); gb_run_vblank(); gb_run_visible(); gb_frame_end();
  }
  printf("  sram pages=%d resident=%d : %u card ops for save RAM, %u for ROM  (%d frames)\n",
         gb_sram_pages(), gb_sram_slots(), swapio, romreads, frames);
  return 0; }
