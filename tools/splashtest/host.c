/* renders the splash like the firmware will and writes PPM frames */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "splash.h"
static uint16_t fb[320 * 320];
static int W, H;
static void dump(const char *fn){ FILE*o=fopen(fn,"wb"); fprintf(o,"P6 %d %d 255\n",W,H);
  for(int i=0;i<W*H;i++){ uint16_t c=fb[i]; c=(c>>8)|(c<<8); unsigned char p[3]={ (c>>11)<<3, ((c>>5)&63)<<2, (c&31)<<3 }; fwrite(p,1,3,o);} fclose(o); }
static void rect(splash_frame_t *f,int x0,int y0,int x1,int y1){ for(int y=y0;y<=y1;y++) splash_render(f,x0,y,x1-x0+1,&fb[y*W+x0]); }
int main(int c,char**v){
  W=atoi(v[1]); H=atoi(v[2]); const char *maskfile=v[3]; const char *prefix=v[4];
  if (splash_setup(W,H)) { puts("setup failed"); return 1; }
  /* text masks: file with frames of (rows*pitch) bytes for n = 0..16 (text) then outline */
  int pitch=(W+7)/8, th=26; FILE*mf=fopen(maskfile,"rb"); static uint8_t masks[34][26*40], om[32][26*40]; if(fread(masks,1,sizeof masks,mf)){} fclose(mf); { char on[256]; snprintf(on,sizeof on,"o%s",maskfile); FILE*q=fopen(on,"rb"); if(fread(om,1,sizeof om,q)){} fclose(q); }
  splash_frame_t f; memset(&f,0,sizeof f); f.mask_pitch=pitch; f.text_h=th; f.text_y0=splash_text_y()-13; f.shine=-2000;
  int frame=0; char fn[256];
  clock_t c0=clock(); double full=0, fly=0, band=0; int nfly=0, nband=0;
  /* first: full background at small scale */
  for (int fr=0; fr<96; fr++) {
    float T=fr/20.0f; f.t=T;
    float u=T/1.1f; if(u>1)u=1; float e=1-(1-u)*(1-u)*(1-u);
    f.g=0.04f+0.96f*e; f.bright=0.25f+0.75f*e;
    int n = T<1.1f?0:((int)((T-1.1f)/0.075f)+1); if(n>15)n=15;
    int cursor = (n<15) || ((int)(T*2.5f)%2==0);
    f.text_mask = T>=1.1f ? masks[n*2 + (cursor?1:0)] : NULL; f.outline_mask = masks[32 + 0]; 
    if (T>=1.1f) f.outline_mask = om[n*2+(cursor?1:0)]; 
    f.shine = (T>2.3f && T<3.2f) ? -40 + (W+80)*(T-2.3f)/0.9f : -2000;
    clock_t a=clock();
    if (fr==0) { f.slices=8; splash_mist_prepare(T); rect(&f,0,0,W-1,H-1); full+= (double)(clock()-a)/CLOCKS_PER_SEC; }
    else if (f.g<1.0f || fr==22) {
      f.slices = f.g<1.0f ? 10 : 30;
      int x0,y0,x1,y1; splash_logo_rect(f.g,&x0,&y0,&x1,&y1);
      if (fr==22) { x0=0;y0=0;x1=W-1;y1=H-1; }
      rect(&f,x0,y0,x1,y1); fly += (double)(clock()-a)/CLOCKS_PER_SEC; nfly++;
    } else {
      f.slices=30;
      if (fr%2==0) { splash_mist_prepare(T); rect(&f,0,splash_mist_top(),W-1,H-1); band+=(double)(clock()-a)/CLOCKS_PER_SEC; nband++; }
      for (int s=0;s<splash_star_count();s++){ int x,y,big; splash_star(s,&x,&y,&big); if (y>=splash_mist_top()) continue;
        if(big) rect(&f,x-1,y-1,x+1,y+1); else rect(&f,x,y,x,y); }
      if (f.shine>-1000) { int x0,y0,x1,y1; splash_logo_rect(1.0f,&x0,&y0,&x1,&y1); rect(&f,x0,y0,x1,y1); }
    }
    snprintf(fn,sizeof fn,"%s%03d.ppm",prefix,fr); dump(fn);
  }
  printf("host ms: first full %.1f, fly frame avg %.1f, mist band avg %.1f, stars %d\n", full*1000, fly*1000/nfly, band*1000/nband, splash_star_count());
  splash_release(); return 0;
}
