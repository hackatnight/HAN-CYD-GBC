/* Checks the indexed line ring: a slow, jittery display thread rebuilds each drawn frame
 * from gb_ring + gb_pal_pool; it must match the RGB565 lines the core produced. */
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
static uint16_t shown[160*144], want[160*144];
static long bad=0, checked=0, skipped=0;
static volatile int hooked=0;
static void hook(int L, const uint16_t *px){ memcpy(&want[L*160], px, 320); hooked++; }
static void *display(void *arg){
  unsigned seed=7;
  while(!stop){
    if(!frameReady){ usleep(20); continue; }
    frameReady=0;
    for(int L=0;L<144;L++){
      while(gb_lines_out<=L) { }
      gb_ring_line_rgb(L, &shown[L*160]);
      gb_lines_need=L+1;
      if(rand_r(&seed)%5==0) usleep(rand_r(&seed)%200);
    }
    __sync_synchronize();
    if (hooked != 144) { skipped++; hooked = 0; dispBusy = 0; continue; }
    hooked = 0;
    { int fb=0, fl=-1, nl=0; for(int L=0;L<144;L++){ int lb=0; for(int x=0;x<160;x++) if(shown[L*160+x]!=want[L*160+x]) lb++; if(lb){ if(fl<0) fl=L; nl++; fb+=lb; } } if(fb && bad < 200000) printf("frame %ld: %d lines bad, first %d\n", checked, nl, fl); bad+=fb; }
    checked++;
    dispBusy=0;
  }
  return 0;
}
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); static uint8_t rom[4<<20]; int n=fread(rom,1,sizeof rom,f); fclose(f);
  int frames=atoi(argv[2]), ring=atoi(argv[3]);
  gb_glue_init(22050);
  gb_ring_alloc(ring);
  if (rom_load(rom,n) || gb_alloc_wram()) return 1;
  gb_sram_setup(32,0);
  gb_reset();
  gb_line_hook=hook;
  pthread_t t; pthread_create(&t,0,display,0);
  for(int i=0;i<frames;i++){
    int press = (i%400)==60;            /* A now and then: walk through the scenes */
    gb_set_pad(press?0x10:0);
    int render=!dispBusy;
    gb_set_framebuffer(0,render);
    gb_run_vblank();
    if(render){ dispBusy=1; __sync_synchronize(); frameReady=1; }
    gb_run_visible();
    if(render) gb_frame_end();
    while(render && dispBusy) usleep(10);   /* keep want[] stable until the frame is checked */
  }
  stop=1; pthread_join(t,0);
  printf("%s ring %d: %ld frames checked (%ld with the LCD off skipped), %ld wrong pixels\n", argv[1], ring, checked, skipped, bad);
  return bad != 0;
}
