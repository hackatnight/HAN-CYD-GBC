/* save-state round trip: run A frames, save, run B frames (record), load, run B frames (record), compare */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "gb_glue.h"
static uint8_t swapfile[32][4096]; static int swapok[32];
int gb_swap_read(int p, uint8_t *b){ if(!swapok[p]) return -1; memcpy(b,swapfile[p],4096); return 0; }
int gb_swap_write(int p, const uint8_t *b){ memcpy(swapfile[p],b,4096); swapok[p]=1; return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n){ return -1; }
static unsigned long long ahash; static int arec;
void gb_audio_out(const uint8_t *s, int n){ if(arec) for(int i=0;i<n;i++) ahash = ahash*1099511628211ULL ^ s[i]; }
void gb_fatal(const char *f, int l, const char *fmt, ...){ va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); exit(2); }
static unsigned long long vhash; static int vrec;
static void on_line(int L, const uint16_t *px){ if(vrec) for(int i=0;i<160;i++) vhash = vhash*1099511628211ULL ^ px[i]; gb_lines_need=L+1; }
static uint8_t *st; static int stlen, stpos, stcap;
static int wr(void *c, void *b, int n){ if(stlen+n>stcap){stcap=(stlen+n)*2; st=realloc(st,stcap);} memcpy(st+stlen,b,n); stlen+=n; return 0; }
static int rd(void *c, void *b, int n){ if(stpos+n>stlen) return -1; memcpy(b,st+stpos,n); stpos+=n; return 0; }
static int pad_at(int f){ return ((f*7919)>>5)&0xFF; }   /* pseudo-random input */
static void run(int from, int n){ for(int i=from;i<from+n;i++){ gb_set_pad(pad_at(i)); gb_set_framebuffer(0,1); gb_run_vblank(); gb_run_visible(); gb_frame_end(); } }
int main(int argc,char**argv){
  FILE *f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  long cap = n < 0x8000 ? 0x8000 : n; uint8_t *rom=malloc(cap); memset(rom,0xff,cap); fread(rom,1,n,f); fclose(f);
  int A=atoi(argv[2]), B=atoi(argv[3]), slots=argc>4?atoi(argv[4]):32;
  gb_glue_init(22050); gb_ring_alloc(144); gb_line_hook = on_line;
  if (rom_load(rom,n)) return 1;
  if (gb_alloc_wram()) return 3;
  int pages=gb_sram_pages(); for(int p=0;p<pages;p++){ memset(swapfile[p],0xFF,4096); swapok[p]=1; }
  gb_sram_setup(slots, 1); gb_reset();
  run(0,A);
  if (gb_state_save(wr,0)) { puts("save failed"); return 4; }
  vrec=arec=1; vhash=ahash=1469598103934665603ULL; run(A,B); unsigned long long v1=vhash,a1=ahash;
  /* scramble a little to prove loading restores it */
  run(A+B, 37);
  stpos=0; int r=gb_state_load(rd,0); if(r){ printf("load failed %d\n",r); return 5; }
  vhash=ahash=1469598103934665603ULL; run(A,B); unsigned long long v2=vhash,a2=ahash;
  if (gb_state_size()!=stlen) printf("SIZE MISMATCH %ld vs %d\n", gb_state_size(), stlen);
  printf("%-28s cgb=%d sram pages=%d (resident %d) state=%d bytes  video %s  audio %s\n", gb_rom_title(), gb_is_cgb(), pages, gb_sram_slots(), stlen,
    v1==v2?"SAME":"DIFF", a1==a2?"SAME":"DIFF");
  return (v1==v2 && a1==a2) ? 0 : 6;
}
