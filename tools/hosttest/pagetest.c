#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "gb_glue.h"
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ printf("FATAL\n"); exit(2); }
int gb_rom_read(unsigned o, uint8_t *b, int n){ return -1; }
static FILE *sw;
int gb_swap_read(int p, uint8_t *b){ fseek(sw,(long)p*4096,SEEK_SET); return fread(b,1,4096,sw)==4096?0:-1; }
int gb_swap_write(int p, const uint8_t *b){ fseek(sw,(long)p*4096,SEEK_SET); int r=fwrite(b,1,4096,sw)==4096?0:-1; fflush(sw); return r; }

int main(int argc,char**argv){
  int slots=atoi(argv[1]);
  FILE*f=fopen("banktest.gbc","rb"); static uint8_t rom[1<<20]; int n=fread(rom,1,sizeof rom,f); fclose(f);
  sw=fopen("swap.bin","w+b"); static uint8_t ff[32768]; memset(ff,0xFF,sizeof ff); fwrite(ff,1,sizeof ff,sw); fflush(sw);
  static uint8_t fb[160*144];
  gb_glue_init(22050);
  if (rom_load(rom,n) || gb_alloc_wram()) return 1;
  if (gb_sram_setup(slots,1)!=slots && slots<=8) { printf("setup failed\n"); return 1; }
  gb_reset();
  for(int i=0;i<30;i++){ gb_set_framebuffer(fb,1); gb_run_vblank(); gb_run_visible(); }
  /* the test ROM wrote A0+b to both ends of each of its 4 banks */
  static uint8_t ref[32768]; memset(ref,0xFF,sizeof ref);
  for(int b=0;b<4;b++){ ref[b*8192]=0xA0+b; ref[b*8192+0x1FFF]=0xA0+b; }
  int bad=0;
  /* random reads/writes through the MBC, as the game would */
  srand(1);
  mem_write(0x0000,0x0A);
  for(int i=0;i<200000;i++){
    int bank=rand()%4, off=rand()%8192, w=rand()%3==0;
    mem_write(0x4000,bank);
    if(w){ uint8_t v=rand(); mem_write(0xA000+off,v); ref[bank*8192+off]=v; }
    else if(mem_read(0xA000+off)!=ref[bank*8192+off]) bad++;
  }
  gb_sram_flush();
  /* file must now equal the reference */
  static uint8_t disk[32768]; fseek(sw,0,SEEK_SET); fread(disk,1,32768,sw);
  int diskbad=memcmp(disk,ref,32768)!=0;
  printf("slots=%d/%d faults=%u read mismatches=%d file %s\n", gb_sram_slots(), gb_sram_pages(), gb_sram_faults, bad, diskbad?"MISMATCH":"matches");
  gb_free_game_memory();
  return bad||diskbad;
}
