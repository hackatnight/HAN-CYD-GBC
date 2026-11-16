#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes_glue.h"
volatile int gb_lines_out, gb_lines_need;
static unsigned char scratch[16768]; void *gb_scratch(size_t *n){ *n=sizeof scratch; return scratch; }
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f,int l,const char *fmt,...){ exit(2);}
int main(int c,char**v){ FILE*fp=fopen(v[1],"rb"); fseek(fp,0,SEEK_END); long n=ftell(fp); rewind(fp); uint8_t*rom=malloc(n); fread(rom,1,n,fp); fclose(fp);
  if (nes_start(rom,n,22050,0)) return 1; nes_ring_alloc(NES_H);
  int bad=0; nes_cheat_poke(0x75,9); bad += nes_cheat_peek(0x75)!=9;
  nes_cheat_poke(0x875,8); bad += nes_cheat_peek(0x75)!=8;   /* mirror */
  if (nes_cheat_space()>0x800){ nes_cheat_poke(0x6010,0x33); bad += nes_cheat_peek(0x800+0x10)!=0x33; }
  for (int i=0;i<300;i++){ nes_cheat_poke(0x75,9); nes_begin_frame(); nes_run_frame(1); }
  printf("NES space %d, pokes %s\n", nes_cheat_space(), bad?"FAILED":"ok"); nes_stop(); return bad; }
