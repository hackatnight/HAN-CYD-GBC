#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nes_glue.h"
#include "nes/nes.h"
volatile int gb_lines_out, gb_lines_need;
static unsigned char scratch[16768]; void *gb_scratch(size_t *n){ *n=sizeof scratch; return scratch; }
static unsigned long long ah; static int arec;
void gb_audio_out(const uint8_t *s, int n){ if(arec) for(int i=0;i<n;i++) ah=ah*1099511628211ULL^s[i]; }
void gb_fatal(const char *f,int l,const char *fmt,...){ exit(2);}
static unsigned long long vh;
static void run(int from,int n,int rec){ for(int f=from;f<from+n;f++){ nes_set_pad(((f*7919)>>5)&0xF3); nes_begin_frame(); nes_run_frame(1);
  if(rec) for(int i=0;i<NES_H*NES_W;i++) vh=vh*1099511628211ULL^nes_ring[i]; } }
int main(int c,char**v){ FILE*fp=fopen(v[1],"rb"); fseek(fp,0,SEEK_END); long n=ftell(fp); rewind(fp); uint8_t*rom=malloc(n); fread(rom,1,n,fp); fclose(fp);
  int A=atoi(v[2]),B=atoi(v[3]);
  if(nes_start(rom,n,22050,0)) return 1; nes_ring_alloc(NES_H);
  run(0,A,0); if(state_save("st.tmp")) {puts("save fail");return 2;}
  vh=ah=1; arec=1; run(A,B,1); unsigned long long v1=vh,a1=ah;
  run(A+B,41,0); if(state_load("st.tmp")){puts("load fail");return 3;}
  vh=ah=1; run(A,B,1);
  printf("%-24s mapper %3d  video %s  audio %s\n", strrchr(v[1],'/')+1, nes_mapper(), v1==vh?"SAME":"DIFF", a1==ah?"SAME":"DIFF");
  if(c>4){ nes_stop(); FILE*q=fopen(v[4],"rb"); fseek(q,0,SEEK_END); long m=ftell(q); rewind(q); uint8_t*r2=malloc(m); fread(r2,1,m,q); fclose(q); nes_start(r2,m,22050,0); printf("load into other game: %d\n", nes_state_load("st.tmp")); }
  nes_stop(); return 0; }
