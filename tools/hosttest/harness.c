#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "gb_glue.h"
static long samples;
static unsigned long audio_sum;
int gb_swap_read(int p, uint8_t *b){ return -1; }
int gb_swap_write(int p, const uint8_t *b){ return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n){ return -1; }
void gb_audio_out(const uint8_t *s, int n){ samples += n; for(int i=0;i<n;i++) audio_sum += (s[i]>128? s[i]-128 : 128-s[i]); }
void gb_fatal(const char *f, int l, const char *fmt, ...){ va_list ap; va_start(ap,fmt); fprintf(stderr,"FATAL %s:%d ",f,l); vfprintf(stderr,fmt,ap); va_end(ap); exit(2); }
static const int pal[4]={0xd5f3ef,0x7ab6a3,0x3b6137,0x161c04};
static uint16_t frame565[160*144];
static void on_line(int L, const uint16_t *px){ memcpy(&frame565[L*160], px, 320); gb_lines_need=L+1; }
static void dump(const uint8_t *fb_unused, const char *out){
  FILE *o=fopen(out,"wb"); fprintf(o,"P6\n160 144\n255\n");
  for(int i=0;i<160*144;i++){ uint16_t c=frame565[i]; unsigned char rgb[3]={ (c>>11)<<3, ((c>>5)&63)<<2, (c&31)<<3 }; fwrite(rgb,1,3,o);} fclose(o);
}
int main(int argc,char**argv){
  FILE *f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  long cap = n < 0x8000 ? 0x8000 : n; if (cap < 4*1024*1024) cap = 4*1024*1024;
  uint8_t *rom=malloc(cap); memset(rom,0xff,cap); fread(rom,1,n,f); fclose(f);
  int frames=atoi(argv[2]);
  static uint8_t fb[2][160*144];
  gb_glue_init(22050);
  gb_line_hook = on_line;
  if (rom_load(rom,n)) return 1;
  if (gb_alloc_wram()) return 3;
  gb_sram_setup(32, 0);
  gb_reset();
  if (!gb_is_cgb()) lcd_set_dmg_palette(pal);
  int back=0;
  for(int i=0;i<frames;i++){
    uint8_t pad = 0;
    if (argc>4 && i>=atoi(argv[4]) && i < atoi(argv[4])+5) pad = 0x80; /* press START */
    gb_set_pad(pad);
    gb_set_framebuffer(fb[back], 1);
    gb_run_vblank(); gb_run_visible(); gb_frame_end();
    back^=1;
  }
  dump(fb[back^1], argv[3]);
  printf("title='%s' cgb=%d frames=%d samples=%ld (%.1f/frame) audio_activity=%lu sram=%d\n", gb_rom_title(), gb_is_cgb(), frames, samples, (double)samples/frames, audio_sum, gb_sram_size());
  return 0;
}
