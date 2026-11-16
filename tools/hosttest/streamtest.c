#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "defs.h"
#include "mem.h"
#include "gb_glue.h"
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ printf("FATAL\n"); exit(2); }
int gb_swap_read(int p, uint8_t *b){ return -1; }
int gb_swap_write(int p, const uint8_t *b){ return 0; }
static uint8_t *full; static unsigned reads;
int gb_rom_read(unsigned off, uint8_t *b, int len){ memcpy(b, full+off, len); reads++; return 0; }
static void on_line(int L, const uint16_t *px){ gb_lines_need = L + 1; }
int main(int argc,char**argv){
  int slots=atoi(argv[1]); const int FLASH=156;
  FILE*f=fopen("stream4mb.gbc","rb"); full=malloc(4<<20); int n=fread(full,1,4<<20,f); fclose(f);
  /* "flash" only holds the first 156 banks; the rest must never be touched directly */
  uint8_t *flash=malloc(FLASH*16384); memcpy(flash, full, FLASH*16384);
  int e0,e1; f=fopen("stream_expect.txt","r"); fscanf(f,"%d %d",&e0,&e1); fclose(f);
  static uint8_t fb[160*144];
  gb_glue_init(22050);
  gb_line_hook = on_line;
  if (rom_load(flash,n) || gb_alloc_wram()) { printf("load failed\n"); return 1; }
  gb_sram_setup(32,0);
  /* layout: argv[2] == "shuffle" puts a random set of banks (always incl. bank 0
   * in slot 0) into flash slots in random order, like a learned layout */
  int nb = gb_rom_banks();
  static uint8_t *bank_map[512]; static uint32_t use[512];
  int layout[512]; for (int i=0;i<nb;i++) layout[i]=i;
  if (argc > 2) {
    srand(99);
    for (int i=nb-1;i>1;i--){ int j=1+rand()%i; int t=layout[i]; layout[i]=layout[j]; layout[j]=t; }
  }
  /* rebuild "flash" from the layout: slot s holds bank layout[s] */
  for (int sl=0; sl<FLASH; sl++) memcpy(flash + sl*16384, full + layout[sl]*16384, 16384);
  for (int sl=0; sl<FLASH; sl++) bank_map[layout[sl]] = flash + sl*16384;
  int got = gb_rom_stream_setup(bank_map, use, nb, slots);
  gb_reset();
  for(int i=0;i<10;i++){ gb_set_framebuffer(fb,1); gb_run_vblank(); gb_run_visible(); gb_frame_end(); }
  int s0=ram.ibank[0][0], s1=ram.ibank[0][1];
  int ok = s0==e0 && s1==e1;
  /* random reads through the MBC + both read paths */
  int bad=0;
  srand(2);
  for(int i=0;i<200000;i++){
    int bank=1+rand()%255, a=0x4000+rand()%0x4000;
    mbc_write(0x2000, bank & 0xFF); mbc_write(0x3000, bank>>8);
    unsigned char want=full[bank*16384 + (a-0x4000)];
    unsigned char v1 = READB(a), v2 = mem_read(a);
    if (v1!=want || v2!=want) bad++;
  }
  unsigned long tot=0; for(int i=0;i<nb;i++) tot+=use[i];
  printf("[%s] usage samples=%lu ", argc>2?"shuffled layout":"first-2.4MB layout", tot);
  printf("slots=%d banks=%d code-from-SD sums %d,%d (want %d,%d) %s; random reads bad=%d faults=%u\n",
         got, gb_rom_banks(), s0, s1, e0, e1, ok?"ok":"BAD", bad, gb_rom_faults);
  gb_free_game_memory();
  return !(ok && bad==0);
}
