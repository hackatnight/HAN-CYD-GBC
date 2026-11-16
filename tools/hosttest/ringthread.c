#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include "gb_glue.h"
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ printf("FATAL\n"); exit(2); }
int gb_swap_read(int p, uint8_t *b){ return -1; }
int gb_swap_write(int p, const uint8_t *b){ return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n){ return -1; }
static volatile int frameReady=0, dispBusy=0, stop=0;
static uint16_t shown[160*144];
static void *display(void *arg){
  unsigned seed=7;
  while(!stop){
    if(!frameReady){ usleep(50); continue; }
    frameReady=0;
    for(int L=0;L<144;L++){
      while(gb_lines_out<=L) { }                 /* wait for the line */
      { int r=L % gb_ring_lines; for(int x=0;x<160;x++) shown[L*160+x]=gb_pal_pool[gb_ring_pal[r]][gb_ring[r][x]]; }
      gb_lines_need=L+1;                          /* release it */
      if(rand_r(&seed)%7==0) usleep(rand_r(&seed)%300);   /* slow, jittery display */
    }
    __sync_synchronize(); dispBusy=0;
  }
  return 0;
}
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); static uint8_t rom[4<<20]; int n=fread(rom,1,sizeof rom,f); fclose(f);
  int frames=atoi(argv[2]);
  gb_glue_init(22050);
  gb_ring_alloc(48);
  if (rom_load(rom,n) || gb_alloc_wram()) return 1;
  gb_sram_setup(32,0);
  gb_reset();
  static const int pal[4]={0xd5f3ef,0x7ab6a3,0x3b6137,0x161c04};
  if(!gb_is_cgb()) lcd_set_dmg_palette(pal);
  pthread_t t; pthread_create(&t,0,display,0);
  int drawn=0;
  for(int i=0;i<frames;i++){
    int render=!dispBusy;
    gb_set_framebuffer(0,render);
    gb_run_vblank();
    if(render){ dispBusy=1; __sync_synchronize(); frameReady=1; drawn++; }
    gb_run_visible();
    if(render) gb_frame_end();
  }
  while(dispBusy) usleep(100);
  stop=1; pthread_join(t,0);
  FILE*o=fopen(argv[3],"wb"); fprintf(o,"P6\n160 144\n255\n");
  for(int i=0;i<160*144;i++){ uint16_t c=shown[i]; unsigned char rgb[3]={(c>>11)<<3,((c>>5)&63)<<2,(c&31)<<3}; fwrite(rgb,1,3,o);} fclose(o);
  printf("%s: frames=%d drawn=%d\n", argv[1], frames, drawn);
  return 0;
}
