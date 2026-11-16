#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "gb_glue.h"
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ exit(2); }
int gb_swap_read(int p, uint8_t *b){ return -1; }
int gb_swap_write(int p, const uint8_t *b){ return 0; }
static uint8_t *full;
int gb_rom_read(unsigned off, uint8_t *b, int len){ memcpy(b, full+off, len); return 0; }
/* a "game": 90% of accesses go to a few busy banks spread across the ROM,
 * some of them past 2.4 MB; 10% go anywhere */
static int busy[] = { 3, 17, 40, 77, 160, 171, 190, 201, 222, 240, 251, 255 };
static unsigned run(short *layout, int nb, int FLASH, uint8_t *flash, uint32_t *use, int slots, int steps){
  static uint8_t *bank_map[512]; memset(bank_map,0,sizeof bank_map);
  for (int sl=0; sl<FLASH; sl++){ memcpy(flash+sl*16384, full+layout[sl]*16384, 16384); bank_map[layout[sl]] = flash+sl*16384; }
  gb_rom_stream_setup(bank_map, use, nb, slots);
  srand(5); unsigned bad=0;
  for (int i=0;i<steps;i++){
    int bank = (rand()%10) ? busy[rand()%12] : 1+rand()%(nb-1);
    int a = 0x4000 + rand()%0x4000;
    mbc_write(0x2000, bank & 0xFF); mbc_write(0x3000, bank>>8);
    gb_bank_sample();
    if (READB(a) != full[bank*16384 + a-0x4000]) bad++;
  }
  unsigned f = gb_rom_faults;
  gb_rom_stream_release();
  return bad ? 0xFFFFFFFF : f;
}
void gb_bank_sample(void);
int main(){
  const int FLASH=156;
  FILE*f=fopen("stream4mb.gbc","rb"); full=malloc(4<<20); int n=fread(full,1,4<<20,f); fclose(f);
  uint8_t *flash=malloc(FLASH*16384); memcpy(flash, full, FLASH*16384);
  gb_glue_init(22050);
  if (rom_load(flash,n) || gb_alloc_wram()) return 1;
  gb_sram_setup(32,0);
  int nb = gb_rom_banks();
  static uint32_t use[512]; static short lay0[512], lay1[512], lay2[512];
  for (int i=0;i<FLASH;i++) lay0[i]=i;                              /* first launch: first 2.4 MB */
  unsigned f0 = run(lay0, nb, FLASH, flash, use, 8, 100000);
  int ch1 = gb_plan_layout(use, nb, FLASH, lay0, lay1);             /* learned after session 1 */
  for (int i=0;i<nb;i++) use[i]/=2;                                 /* decay, as the app does */
  unsigned f1 = run(lay1, nb, FLASH, flash, use, 8, 100000);
  int ch2 = gb_plan_layout(use, nb, FLASH, lay1, lay2);             /* stable afterwards? */
  int bank0ok = lay1[0]==0;
  int dup=0; { static char seen[512]; for(int i=0;i<FLASH;i++){ if(seen[lay1[i]]) dup++; seen[lay1[i]]=1; } }
  printf("session 1 (first 2.4 MB in flash): %u SD reads\\n", f0);
  printf("relayout changed %d slots; bank 0 in slot 0: %s; duplicate banks: %d\\n", ch1, bank0ok?"yes":"NO", dup);
  printf("session 2 (learned layout):        %u SD reads (all reads correct: %s)\\n", f1, f1!=0xFFFFFFFF?"yes":"NO");
  printf("next relayout would change %d slots (should be ~0: stable)\\n", ch2);
  return 0;
}
