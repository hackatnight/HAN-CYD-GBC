#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "gb_glue.h"
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ printf("FATAL\n"); exit(2); }
int main(){
  FILE*f=fopen("banktest.gbc","rb"); static uint8_t rom[4<<20]; int n=fread(rom,1,sizeof rom,f); fclose(f);
  static uint8_t fb[160*144];
  gb_glue_init(22050);
  if (rom_load(rom,n)) { printf("rom_load failed\n"); return 1; }
  if (gb_alloc_wram()) { printf("wram failed\n"); return 1; }
  gb_reset();
  for(int i=0;i<30;i++){ gb_set_framebuffer(fb,1); gb_run_vblank(); gb_run_visible(); }
  int ok=1;
  printf("needKB=%d sram banks=%d cgb=%d\n", gb_game_ram_kb(), gb_sram_banks(), gb_is_cgb());
  for(int b=0;b<4;b++){ uint8_t *p=gb_sram_bank(b); int g=p[0]==0xA0+b && p[0x1FFF]==0xA0+b; ok&=g; printf("cart bank %d: %02X %02X %s\n",b,p[0],p[0x1FFF],g?"ok":"BAD"); }
  for(int b=1;b<8;b++){ int g=ram.ibank[b][0]==0xC0+b; ok&=g; printf("wram bank %d: %02X %s\n",b,ram.ibank[b][0],g?"ok":"BAD"); }
  int g=ram.ibank[0][0]==0x55 && ram.ibank[0][1]==0xA2; ok&=g; printf("wram bank 0: %02X, readback %02X %s\n",ram.ibank[0][0],ram.ibank[0][1],g?"ok":"BAD");
  /* save/load round trip like main.cpp does */
  FILE*s=fopen("bt.sav","wb"); for(int b=0;b<gb_sram_banks();b++) fwrite(gb_sram_bank(b),1,8192,s); fclose(s);
  gb_free_game_memory();
  rom_load(rom,n); gb_alloc_wram();
  s=fopen("bt.sav","rb"); for(int b=0;b<gb_sram_banks();b++) fread(gb_sram_bank(b),1,8192,s); fclose(s);
  for(int b=0;b<4;b++){ int g=gb_sram_bank(b)[0]==0xA0+b; ok&=g; }
  printf("save/load round trip %s\n", ok?"ok":"BAD");
  gb_free_game_memory();
  printf(ok?"ALL OK\n":"FAILED\n"); return !ok;
}
