/* headless SDL3 stand-in: software renderer to PPM snapshots, fake clock, scripted input */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
static Uint64 now; static Uint64 snap_every=1000, next_snap=0, limit=40000;
static int OW=640, OH=480; static uint32_t *outbuf; static uint32_t drawcol=0xFF000000u;
static int frameno; static unsigned rseed=12345;
static int script=0;            /* 1 = play the game: click title, then drive the ship */
static int clicks_done=0;
static bool keys[SDL_NUM_SCANCODES];
static float mousex=-1, mousey=-1; static SDL_MouseButtonFlags mbtn=0;
struct SDL_Texture { int w,h; uint32_t *px; };
struct SDL_AudioStream { Uint64 start; long total; int rate; int id; };
static int stream_ids, audio_on=1;
static FILE *evlog;
bool SDL_Init(SDL_InitFlags f){
  if(getenv("SNAP_EVERY")) snap_every=atoll(getenv("SNAP_EVERY"));
  if(getenv("MOUSEX")) mousex=(float)atof(getenv("MOUSEX"));
  if(getenv("MOUSEY")) mousey=(float)atof(getenv("MOUSEY"));
  if(getenv("LIMIT_MS")) limit=atoll(getenv("LIMIT_MS"));
  if(getenv("OUT_W")) OW=atoi(getenv("OUT_W"));
  if(getenv("OUT_H")) OH=atoi(getenv("OUT_H"));
  if(getenv("SCRIPT")) script=atoi(getenv("SCRIPT"));
  if(getenv("NOAUDIO")) audio_on=0;
  outbuf=calloc((size_t)OW*OH,4); evlog=stdout; return true; }
bool SDL_InitSubSystem(SDL_InitFlags f){ return audio_on!=0; }
void SDL_Quit(void){ printf("[%llu ms] SDL_Quit\n",(unsigned long long)now); }
const char *SDL_GetError(void){ return "stub"; }
void SDL_Log(const char *fmt,...){ va_list a; va_start(a,fmt); printf("LOG: "); vprintf(fmt,a); printf("\n"); va_end(a); }
bool SDL_CreateWindowAndRenderer(const char*t,int w,int h,SDL_WindowFlags f,SDL_Window**w_,SDL_Renderer**r){ *w_=(SDL_Window*)1; *r=(SDL_Renderer*)1; return true; }
bool SDL_SetWindowMinimumSize(SDL_Window*w,int a,int b){ return true; }
bool SDL_MaximizeWindow(SDL_Window*w){ return true; }
bool SDL_SetWindowFullscreen(SDL_Window*w,bool f){ printf("[%llu ms] fullscreen=%d\n",(unsigned long long)now,f); return true; }
bool SDL_GetRenderOutputSize(SDL_Renderer*r,int*w,int*h){ *w=OW; *h=OH; return true; }
bool SDL_SetRenderDrawColor(SDL_Renderer*r,Uint8 R,Uint8 G,Uint8 B,Uint8 A){ drawcol=0xFF000000u|(R<<16)|(G<<8)|B; return true; }
SDL_Texture *SDL_CreateTexture(SDL_Renderer*r,SDL_PixelFormat f,SDL_TextureAccess a,int w,int h){ SDL_Texture*t=calloc(1,sizeof *t); t->w=w;t->h=h;t->px=calloc((size_t)w*h,4); return t; }
bool SDL_SetTextureScaleMode(SDL_Texture*t,SDL_ScaleMode m){return true;}
bool SDL_UpdateTexture(SDL_Texture*t,const SDL_Rect*r,const void*p,int pitch){ for(int y=0;y<t->h;y++) memcpy(t->px+(size_t)y*t->w,(const char*)p+(size_t)y*pitch,(size_t)t->w*4); return true; }
bool SDL_RenderClear(SDL_Renderer*r){ for(int i=0;i<OW*OH;i++) outbuf[i]=drawcol; return true; }
bool SDL_RenderFillRect(SDL_Renderer*r,const SDL_FRect*d){ for(int y=(int)d->y;y<(int)(d->y+d->h);y++) for(int x=(int)d->x;x<(int)(d->x+d->w);x++) if(x>=0&&y>=0&&x<OW&&y<OH) outbuf[y*OW+x]=drawcol; return true; }
bool SDL_RenderTexture(SDL_Renderer*r,SDL_Texture*t,const SDL_FRect*a,const SDL_FRect*d){
  for(int y=(int)d->y;y<(int)(d->y+d->h);y++) for(int x=(int)d->x;x<(int)(d->x+d->w);x++){ if(x<0||y<0||x>=OW||y>=OH) continue;
    int u=(int)((x-d->x)*t->w/d->w), v=(int)((y-d->y)*t->h/d->h); outbuf[y*OW+x]=t->px[v*t->w+u]; } return true; }
bool SDL_RenderTextureTiled(SDL_Renderer*r,SDL_Texture*t,const SDL_FRect*a,float sc,const SDL_FRect*d){
  for(int y=(int)d->y;y<(int)(d->y+d->h);y++) for(int x=(int)d->x;x<(int)(d->x+d->w);x++){ if(x<0||y<0||x>=OW||y>=OH) continue;
    int u=(int)floorf((x-d->x)/sc)%t->w, v=(int)floorf((y-d->y)/sc)%t->h; outbuf[y*OW+x]=t->px[v*t->w+u]; } return true; }
bool SDL_RenderPresent(SDL_Renderer*r){
  if(now>=next_snap){ char fn[160]; snprintf(fn,sizeof fn,"frames/f_%06llu.ppm",(unsigned long long)now); FILE*f=fopen(fn,"wb");
    if(f){ fprintf(f,"P6\n%d %d\n255\n",OW,OH); for(int i=0;i<OW*OH;i++){ uint32_t c=outbuf[i]; fputc((c>>16)&255,f); fputc((c>>8)&255,f); fputc(c&255,f);} fclose(f);} next_snap+=snap_every; frameno++; }
  if(now>limit){ printf("[%llu ms] time limit reached\n",(unsigned long long)now); exit(0);} return true; }
void SDL_DestroyTexture(SDL_Texture*t){} void SDL_DestroyRenderer(SDL_Renderer*r){} void SDL_DestroyWindow(SDL_Window*w){}
/* scripted input: after 1s skip the intro with a click; at ~26s click "Play Apeiron"; then drive */
static void script_update(void){
  if(!script) return;
  memset(keys,0,sizeof keys); mbtn=0;
  mousex=getenv("MOUSEX")?(float)atof(getenv("MOUSEX")):-1; mousey=getenv("MOUSEY")?(float)atof(getenv("MOUSEY")):-1;
  if(script==2) return;
  if(getenv("NOMENU")) return;
  if(now>26000){ /* in game: sweep left/right and hold fire */
    Uint64 t=(now-26000)/1000;
    if((t/2)%2==0) keys[SDL_SCANCODE_LEFT]=true; else keys[SDL_SCANCODE_RIGHT]=true;
    if(((now-26000)/150)%2==0) keys[SDL_SCANCODE_SPACE]=true;
    if(((now-26000)/3000)%4==3) keys[SDL_SCANCODE_UP]=true;
    if(getenv("SPAWN")) keys[atoi(getenv("SPAWN"))] = (((now-26000)/700)%2==0);
  }
}
bool SDL_PollEvent(SDL_Event*e){
  script_update();
  if(script){
    if(clicks_done==0 && now>800){ clicks_done=1; memset(e,0,sizeof *e); e->type=SDL_EVENT_MOUSE_BUTTON_DOWN; e->button.x=320; e->button.y=240; printf("[%llu ms] click (skip intro)\n",(unsigned long long)now); return true; }
    if(clicks_done==1 && now>25000 && !getenv("NOMENU")){ clicks_done=2; memset(e,0,sizeof *e); e->type=SDL_EVENT_MOUSE_BUTTON_DOWN;
      e->button.x=getenv("CX")?atoi(getenv("CX")):460; e->button.y=getenv("CY")?atoi(getenv("CY")):42;
      printf("[%llu ms] click menu (%g,%g)\n",(unsigned long long)now,e->button.x,e->button.y); return true; }
    if(clicks_done==2 && getenv("CX2") && now>28000){ clicks_done=3; memset(e,0,sizeof *e); e->type=SDL_EVENT_MOUSE_BUTTON_DOWN;
      e->button.x=atoi(getenv("CX2")); e->button.y=getenv("CY2")?atoi(getenv("CY2")):0;
      printf("[%llu ms] click 2 (%g,%g)\n",(unsigned long long)now,e->button.x,e->button.y); return true; }
    if(clicks_done>=2 && script==2 && !getenv("CX2") && now>(Uint64)(30000+(clicks_done-2)*9000)){ clicks_done++; memset(e,0,sizeof *e); e->type=SDL_EVENT_MOUSE_BUTTON_DOWN; e->button.x=320; e->button.y=240; printf("[%llu ms] click page\n",(unsigned long long)now); return true; }
  }
  if(getenv("KEYSCRIPT") && !clicks_done && now>26000){ clicks_done=1; memset(e,0,sizeof *e);
      e->type=SDL_EVENT_TEXT_INPUT; e->text.text=getenv("KEYSCRIPT"); printf("[%llu ms] typed %s\n",(unsigned long long)now,getenv("KEYSCRIPT")); return true; }
  return false; }  /* KEYSCRIPT */
Uint64 SDL_GetTicks(void){ return now; } void SDL_Delay(Uint32 ms){ now+=ms?ms:1; }
const bool *SDL_GetKeyboardState(int *n){ if(n)*n=SDL_NUM_SCANCODES; return keys; }
SDL_MouseButtonFlags SDL_GetMouseState(float*x,float*y){ if(x)*x=mousex; if(y)*y=mousey; return mbtn; }
SDL_Window *SDL_GetKeyboardFocus(void){ return (SDL_Window*)1; }
bool SDL_StartTextInput(SDL_Window*w){ return true; } bool SDL_StopTextInput(SDL_Window*w){ return true; }
Sint32 SDL_rand(Sint32 n){ rseed=rseed*1103515245u+12345u; return n>0 ? (Sint32)((rseed>>16)%(unsigned)n) : 0; }
SDL_Surface *SDL_LoadBMP(const char*path){
  FILE*f=fopen(path,"rb"); if(!f) return NULL; Uint8 h[54]; if(fread(h,1,54,f)!=54){fclose(f);return NULL;}
  uint32_t off=*(uint32_t*)(h+10), hs=*(uint32_t*)(h+14); int w=*(int32_t*)(h+18), ht=*(int32_t*)(h+22); uint16_t bpp=*(uint16_t*)(h+28); uint32_t clr=*(uint32_t*)(h+46);
  if(bpp!=8||hs!=40){ fclose(f); return NULL; } if(clr==0) clr=256;
  SDL_Surface*s=calloc(1,sizeof *s); s->format=SDL_PIXELFORMAT_INDEX8; s->w=w; s->h=ht<0?-ht:ht; s->pitch=w; s->pixels=malloc((size_t)w*s->h);
  s->pal=calloc(1,sizeof(SDL_Palette)); s->pal->ncolors=clr; s->pal->colors=calloc(256,sizeof(SDL_Color));
  fseek(f,14+hs,SEEK_SET); for(uint32_t i=0;i<clr;i++){ Uint8 q[4]; fread(q,1,4,f); s->pal->colors[i]=(SDL_Color){q[2],q[1],q[0],255}; }
  int rowb=(w+3)&~3; Uint8*row=malloc(rowb); fseek(f,off,SEEK_SET);
  for(int y=0;y<s->h;y++){ fread(row,1,rowb,f); int dy= ht>0 ? s->h-1-y : y; memcpy((Uint8*)s->pixels+(size_t)dy*w,row,w);} free(row); fclose(f); return s; }
SDL_Palette *SDL_GetSurfacePalette(SDL_Surface*s){ return s->pal; }
void SDL_DestroySurface(SDL_Surface*s){ free(s->pixels); free(s->pal->colors); free(s->pal); free(s); }
bool SDL_LoadWAV(const char*path,SDL_AudioSpec*sp,Uint8**buf,Uint32*len){
  FILE*f=fopen(path,"rb"); if(!f) return false; Uint8 h[12]; if(fread(h,1,12,f)!=12){fclose(f);return false;}
  for(;;){ Uint8 ch[8]; if(fread(ch,1,8,f)!=8){ fclose(f); return false; } uint32_t sz=*(uint32_t*)(ch+4);
    if(!memcmp(ch,"fmt ",4)){ Uint8 b[16]; fread(b,1,16,f); sp->channels=*(uint16_t*)(b+2); sp->freq=*(uint32_t*)(b+4); sp->format=SDL_AUDIO_U8; fseek(f,sz-16,SEEK_CUR);}
    else if(!memcmp(ch,"data",4)){ *buf=malloc(sz); fread(*buf,1,sz,f); *len=sz; fclose(f); return true; } else fseek(f,sz,SEEK_CUR); } }
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID d,const SDL_AudioSpec*s,void*cb,void*u){ if(!audio_on) return NULL; SDL_AudioStream*a=calloc(1,sizeof *a); a->rate=s->freq*s->channels; a->id=++stream_ids; return a; }
bool SDL_PutAudioStreamData(SDL_AudioStream*a,const void*p,int n){ if(a)a->total=n; return true; }
bool SDL_FlushAudioStream(SDL_AudioStream*a){return true;}
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream*a){ if(a)a->start=now; return true; }
int SDL_GetAudioStreamQueued(SDL_AudioStream*a){ if(!a) return 0; long el=(long)((now-a->start)*a->rate/1000); long q=a->total-el; return q>0?(int)q:0; }
void SDL_DestroyAudioStream(SDL_AudioStream*a){ free(a); }

SDL_Keymod SDL_GetModState(void){ static int n=0; if(getenv("CAPSAT")&&now>(Uint64)atoll(getenv("CAPSAT"))&&now<(Uint64)atoll(getenv("CAPSAT"))+3000){ (void)n; return SDL_KMOD_CAPS; } return 0; }
bool SDL_HideCursor(void){ return true; }
bool SDL_ShowCursor(void){ return true; }

bool SDL_SetWindowRelativeMouseMode(SDL_Window *w, bool on){ (void)w; (void)on; return true; }
void SDL_WarpMouseInWindow(SDL_Window *w, float x, float y){ (void)w; (void)x; (void)y; }
bool SDL_GetWindowRelativeMouseMode(SDL_Window *w){ (void)w; return false; }
unsigned int SDL_GetRelativeMouseState(float *x, float *y){
  static float px=-1, py=-1;
  float dx=0, dy=0;
  if(mousex>=0 && px>=0){ dx=mousex-px; dy=mousey-py; }
  px=mousex; py=mousey;
  if(x)*x=dx; if(y)*y=dy; return 0;
}

void SDL_free(void *p){ free(p); }

SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID d, const SDL_AudioSpec *sp){ (void)d;(void)sp; return 0; }
SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *a, const SDL_AudioSpec *b){ (void)a;(void)b; return 0; }
bool SDL_BindAudioStream(SDL_AudioDeviceID d, SDL_AudioStream *s){ (void)d;(void)s; return true; }
bool SDL_ClearAudioStream(SDL_AudioStream *s){ (void)s; return true; }
bool SDL_SetAudioStreamFormat(SDL_AudioStream *s, const SDL_AudioSpec *a, const SDL_AudioSpec *b){ (void)s;(void)a;(void)b; return true; }
bool SDL_RaiseWindow(SDL_Window *w){ (void)w; return true; }

SDL_JoystickID *SDL_GetGamepads(int *n){ if(n)*n=0; return 0; }
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID i){ (void)i; return 0; }
void SDL_CloseGamepad(SDL_Gamepad *g){ (void)g; }
bool SDL_GamepadConnected(SDL_Gamepad *g){ (void)g; return false; }
const char *SDL_GetGamepadName(SDL_Gamepad *g){ (void)g; return "none"; }
short SDL_GetGamepadAxis(SDL_Gamepad *g, SDL_GamepadAxis a){ (void)g;(void)a; return 0; }
bool SDL_GetGamepadButton(SDL_Gamepad *g, SDL_GamepadButton b){ (void)g;(void)b; return false; }

bool SDL_SetHint(const char *a, const char *b){ (void)a;(void)b; return true; }
bool SDL_ShowWindow(SDL_Window *w){ (void)w; return true; }
bool SDL_SetWindowFocusable(SDL_Window *w, bool b){ (void)w;(void)b; return true; }
bool SDL_FlashWindow(SDL_Window *w, SDL_FlashOperation o){ (void)w;(void)o; return true; }
bool SDL_RestoreWindow(SDL_Window *w){(void)w;return true;}
bool SDL_SetRenderVSync(SDL_Renderer *r,int v){(void)r;(void)v;return true;}
bool SDL_SyncWindow(SDL_Window *w){(void)w;return true;}
SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w){(void)w;return 1;}
SDL_DisplayID SDL_GetPrimaryDisplay(void){return 1;}
bool SDL_GetDisplayUsableBounds(SDL_DisplayID d,SDL_Rect *r){(void)d;if(r){r->x=0;r->y=0;r->w=1920;r->h=1080;}return true;}
bool SDL_SetWindowPosition(SDL_Window *w,int x,int y){(void)w;(void)x;(void)y;return true;}
bool SDL_SetWindowSize(SDL_Window *w,int x,int y){(void)w;(void)x;(void)y;return true;}
bool SDL_GetWindowSize(SDL_Window *w,int *x,int *y){(void)w;if(x)*x=640;if(y)*y=480;return true;}
void SDL_PumpEvents(void){}
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *w){(void)w;return SDL_WINDOW_INPUT_FOCUS|SDL_WINDOW_MOUSE_FOCUS;}
