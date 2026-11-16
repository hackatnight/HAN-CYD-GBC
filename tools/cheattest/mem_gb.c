#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gb_glue.h"
int gb_swap_read(int p, uint8_t *b){ memset(b,0xFF,4096); return 0; }
int gb_swap_write(int p, const uint8_t *b){ return 0; }
int gb_rom_read(unsigned o, uint8_t *b, int n){ return -1; }
void gb_audio_out(const uint8_t *s, int n){}
void gb_fatal(const char *f, int l, const char *fmt, ...){ exit(2); }
int main(int c, char **v){
  FILE *f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f); long cap=n<0x8000?0x8000:n; uint8_t*rom=malloc(cap); memset(rom,0xff,cap); fread(rom,1,n,f); fclose(f);
  gb_glue_init(22050); gb_ring_alloc(144); if (rom_load(rom,n) || gb_alloc_wram()) return 1; gb_sram_setup(32,0); gb_reset();
  int bad=0;
  gb_cheat_poke(0xFF,0xC120,5); bad += gb_cheat_peek(0x120)!=5;
  gb_cheat_poke(0xFF,0xE121,6); bad += gb_cheat_peek(0x121)!=6;           /* echo RAM */
  gb_cheat_poke(0xFF,0xFF90,7); bad += gb_cheat_peek(gb_cheat_space()-127+0x10)!=7;
  if (gb_is_cgb()) { gb_cheat_poke(3,0xD010,0x42); bad += gb_cheat_peek(3*4096+0x10)!=0x42; }
  else { gb_cheat_poke(0xFF,0xD010,0x42); bad += gb_cheat_peek(1*4096+0x10)!=0x42; }
  printf("space %d, cgb %d, addr(%d)=%05X\n", gb_cheat_space(), gb_is_cgb(), 3*4096+0x10, gb_cheat_addr(3*4096+0x10));
  /* run frames with a cheat applied every frame: must not crash, value must hold */
  for (int i=0;i<300;i++){ gb_cheat_poke(0xFF,0xC120,5); gb_set_framebuffer(0,1); gb_run_vblank(); gb_run_visible(); gb_frame_end(); gb_lines_need=144; }
  printf("GB pokes %s\n", bad ? "FAILED" : "ok");
  return bad;
}
