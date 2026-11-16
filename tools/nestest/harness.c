#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nes_glue.h"
#include "nes/nes.h"
volatile int gb_lines_out, gb_lines_need;
static unsigned char scratch[16768]; void *gb_scratch(size_t *n){ *n=sizeof scratch; return scratch; }
static long audio_n; static long audio_sum;
void gb_audio_out(const uint8_t *s, int n){ for(int i=0;i<n;i++) audio_sum+=abs((int)s[i]-128); audio_n+=n; }
void gb_fatal(const char *f,int l,const char *fmt,...){ va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap); exit(2);}
static uint8_t frame[NES_H][NES_W];
int main(int argc,char**argv){
  FILE*fp=fopen(argv[1],"rb"); fseek(fp,0,SEEK_END); long n=ftell(fp); rewind(fp);
  uint8_t*rom=malloc(n); fread(rom,1,n,fp); fclose(fp);
  int frames=argc>2?atoi(argv[2]):600;
  if(nes_ring_alloc(16)){puts("ring");return 1;}
  int r=nes_start(rom,n,22050,0); if(r){printf("start %d\n",r);return 1;}
  printf("mapper %d pal %d sram %d\n",nes_mapper(),nes_is_pal(),nes_sram_size());
  for(int f=0;f<frames;f++){
    int draw = f==frames-1;
    if(draw) nes_begin_frame();
    if(draw){ /* consume lines like the display would: ring 16 lines, emulate via hook */ }
    nes_set_pad(argc>3 && f>60 && (f/30)%2 ? 0x80:0);
    /* act as display: run frame in steps isn't possible, so use big ring when drawing */
    if(draw){ nes_ring_alloc(NES_H); }
    nes_run_frame(draw);
    if(draw) memcpy(frame,nes_ring,sizeof frame);
  }
  /* blargg status at $6000, text at $6004 */
  uint8_t st=mem_getbyte(0x6000); char txt[256]={0};
  if(mem_getbyte(0x6001)==0xDE&&mem_getbyte(0x6002)==0xB0){ for(int i=0;i<255;i++){ txt[i]=mem_getbyte(0x6004+i); if(!txt[i])break; } }
  printf("status %02X audio avg %.2f samples %ld\ntext: %s\n",st,audio_n?(double)audio_sum/audio_n:0,audio_n,txt);
  uint16_t *pal=nes_pal565; nes_build_palette(0,pal);
  FILE*o=fopen(argc>4?argv[4]:"out.ppm","wb"); fprintf(o,"P6 %d %d 255\n",NES_W,NES_H);
  for(int y=0;y<NES_H;y++)for(int x=0;x<NES_W;x++){uint16_t c=pal[frame[y][x]]; fputc((c>>11)<<3,o);fputc(((c>>5)&63)<<2,o);fputc((c&31)<<3,o);} fclose(o);
  nes_stop(); return 0;
}
