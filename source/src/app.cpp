// MIT. Gravity: code-first Resona Box UI and control; audio lives in the DSP pack.
// The Web, Mobile and MIDI controllers (controller/, CCs on channel 1, see README.md) reach every setting the knobs
// reach, plus the resonator's MIX and LEVEL, and four hold pads: SUSPEND (like holding the big knob), SOAR (FLOAT
// 100), SINK (FLOAT 0) and FLIP (the other of minor/major).
#include <resona/platform.h>
#include <resona/startup_helpers.h>
#include "splash_data.h"
#include "demo_capture.h"
#include <resona/ui.h>
#include <resona/math.h>
#include "art.h"
#include "art_words.h"
#include "own_audio.h"
#include "trip_guard.h"
#include "controller_link.h"
namespace {
const resona_platform *P;
const char kPackId[]="org.resona.demo.gravity.resonator"; // dsp/resona.project.json
own_audio::Graph g_audio;
const char *const noteNames[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
// v1 was {magic,key,amount}; v2 appends the DRY toggle, v3 MIX and LEVEL. Older records still restore what they have.
struct Saved {uint32_t magic;int key,amount,dry,mix,level;};
constexpr uint32_t kMagicV1=0x47525631,kMagicV2=0x47525632,kMagicV3=0x47525633;
// The controllers' MIDI map: CC on channel 1 (controller/shared/profile.js says the same).
enum {CC_NOTE=20,CC_MODE,CC_FLOAT,CC_MIX,CC_LEVEL,CC_DRY,CC_SUSPEND=30,CC_SOAR,CC_SINK,CC_FLIP};
struct State {int key,amount,held,dry,ready,stage,error,entry,pos,count;uint32_t ms,changed,nextPoll;int savePending;uint8_t in,out;trip_guard::Guard trip;
 int note,major,mix,level;                  // key = major*12 + note; the controllers set note and mode apart
 int padSuspend,padSoar,padSink,padFlip;    // pads held on a controller
 ctl::Link link;};
static int clamp(int n,int lo,int hi){return n<lo?lo:(n>hi?hi:n);}
static ctl::Bind kBind[10];
static void bind(State*s){const ctl::Bind b[10]={{CC_NOTE,ctl::STEPS,0,11,&s->note},{CC_MODE,ctl::STEPS,0,1,&s->major},{CC_FLOAT,ctl::RANGE,0,100,&s->amount},
 {CC_MIX,ctl::RANGE,0,100,&s->mix},{CC_LEVEL,ctl::RANGE,0,100,&s->level},{CC_DRY,ctl::STEPS,0,1,&s->dry},
 {CC_SUSPEND,ctl::TRIGGER,0,1,nullptr},{CC_SOAR,ctl::TRIGGER,0,1,nullptr},{CC_SINK,ctl::TRIGGER,0,1,nullptr},{CC_FLIP,ctl::TRIGGER,0,1,nullptr}};
 for(int i=0;i<10;++i)kBind[i]=b[i];}
static void split(State*s){s->note=s->key%12;s->major=s->key/12;}
// The pads override the settings while held: SOAR wins over SINK.
static int keyNow(const State*s){return s->padFlip?(s->key+12)%24:s->key;}
static int amountNow(const State*s){return s->padSoar?100:(s->padSink?0:s->amount);}
static int heldNow(const State*s){return s->held||s->padSuspend;}
static bool ok(State*s,int code){if(code>=0)return true;s->error=code;return false;}
static void param(State*s,unsigned n,float v){if(s->ready&&!ok(s,P->effect_param_set(g_audio.h[0],n,v)))s->ready=0;}
static float mixNow(const State*s){return s->dry?0.f:(float)s->mix;}
static void push(State*s){param(s,0,(float)keyNow(s));param(s,1,(float)amountNow(s));param(s,2,(float)heldNow(s));param(s,3,mixNow(s));param(s,4,(float)s->level);}
// The OS bypassed the resonator (CPU time guard or fault): drop the graph and rebuild it after a growing wait.
static void trip(State*s){s->ready=0;s->stage=-1;s->error=RESONA_ERR_BUDGET;s->nextPoll=s->ms+trip_guard::tripped(s->trip,s->ms);own_audio::stop(P,g_audio);}
void setup(State*s){
 if(s->ms<s->nextPoll)return;s->nextPoll=s->ms+1000;
 if(s->ready&&trip_guard::faulted(P,g_audio.h[0])){trip(s);return;}
 // The App owns audio: Guitar -> Gravity Resonator -> Output (own_audio.h); retried until installed.
 int rc=own_audio::start(P,g_audio,kPackId,"0.1.3");if(rc<0){s->ready=0;s->error=rc;return;}
 resona_effect_info fi={};rc=P->effect_info(g_audio.h[0],&fi);
 if(rc!=RESONA_OK||fi.param_count!=5){s->ready=0;s->error=rc<0?rc:RESONA_ERR_STATE;return;}
 if(!s->ready){rc=P->effect_enable(g_audio.h[0],1);if(rc<0){s->error=rc;return;}s->error=0;s->ready=1;push(s);}
 else s->error=0; // audio is up: a stale error must not stay on screen
}
static uint16_t rgb(unsigned r,unsigned g,unsigned b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
static uint16_t blend(uint16_t dst,uint16_t src,unsigned a){unsigned ia=255-a;unsigned r=(((dst>>11)&31)*ia+((src>>11)&31)*a+127)/255;unsigned g=(((dst>>5)&63)*ia+((src>>5)&63)*a+127)/255;unsigned b=((dst&31)*ia+(src&31)*a+127)/255;return (uint16_t)((r<<11)|(g<<5)|b);}
static void text(uint16_t*px,uint32_t stride,int x,int y,const char*t,int size,uint16_t col){
 const char*chars=size==42?chars42:(size==13?chars13:chars9);const uint32_t*off=size==42?glyph_offsets42:(size==13?glyph_offsets13:glyph_offsets9);const uint8_t*width=size==42?glyph_widths42:(size==13?glyph_widths13:glyph_widths9);const uint8_t*alpha=size==42?glyph_alpha42:(size==13?glyph_alpha13:glyph_alpha9);int h=size+4;
 for(int n=0;t[n]&&n<32;n++){int g=0;while(chars[g]&&chars[g]!=t[n])g++;if(!chars[g])g=0;int w=width[g];for(int j=0;j<h;j++)for(int i=0;i<w;i++){int X=x+i,Y=y+j;if(X>=0&&X<320&&Y>=0&&Y<172){unsigned a=alpha[off[g]+j*w+i];if(a)px[Y*stride+X]=blend(px[Y*stride+X],col,a);}}x+=w;}
}
static void paper(uint16_t*px,uint32_t stride,int x,int y,int w,int h){for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(i>=0&&i<320&&j>=0&&j<172)px[j*stride+i]=art_pixel(j*320+(i%6));}
static void draw(State*s){uint16_t*px=nullptr;uint32_t stride=0;resona_ui*ui=P->screen_begin(&px,&stride);if(!ui||!px)return;
 const int key=keyNow(s),amount=amountNow(s);
 int bob=(int)(resona_sinf(s->ms*.0012f)*(1.f+amount*.025f)-(heldNow(s)?2.f:0.f));
 art_copy(px,stride);for(int y=21;y<143;++y){const int yy=clamp(y-bob,20,142);auto*d=reinterpret_cast<uint32_t*>(px+y*stride);const uint32_t*src=art_words+yy*160;for(int x=65;x<160;++x)d[x]=src[x];}
 paper(px,stride,10,36,119,48);paper(px,stride,9,101,79,59);
 uint16_t ink=rgb(28,28,23);char val[12];text(px,stride,13,34,noteNames[key%12],42,ink);text(px,stride,58,62,key<12?"MINOR":"MAJOR",13,ink);
 P->format(val,sizeof val,"%d",amount);text(px,stride,11,101,val,42,ink);text(px,stride,13,145,s->dry?"DRY":"FLOAT",13,ink);
 if(heldNow(s)){ui->rect(ui->ctx,219,145,93,15,ink);text(px,stride,226,147,"SUSPENDED",9,rgb(245,213,136));}
 if(!s->ready||s->error){paper(px,stride,6,161,308,11);char b[48];P->format(b,sizeof b,s->error==RESONA_ERR_PACK_ID?"INSTALL GRAVITY RESONATOR":s->error==RESONA_ERR_BUDGET?"CPU LIMIT":"RESONATOR ERR %d",s->error);text(px,stride,8,161,b,9,rgb(143,65,27));}
 demo_capture::remember(px,stride);P->screen_present(0,0,0,0);
}
static void changed(State*s){s->changed=s->ms;s->savePending=1;}
static void input(State*s,int n,int kind,int delta){
 if(kind==RESONA_EVENT_TURN){if(n==0){int base=s->key/12*12;s->key=base+((s->key%12+delta)%12+12)%12;param(s,0,(float)keyNow(s));}else{s->amount=clamp(s->amount+delta*2,0,100);param(s,1,(float)amountNow(s));}changed(s);}
 else if(kind==RESONA_EVENT_CLICK){
  if(n==0){s->key=(s->key+12)%24;param(s,0,(float)keyNow(s));}else{s->dry^=1;param(s,3,mixNow(s));}
  changed(s);
 }
 else if(kind==RESONA_EVENT_LONG){if(n==0){s->key=2;s->amount=68;s->dry=0;if(!s->mix)s->mix=65;push(s);changed(s);if(!s->ready){s->stage=0;s->error=0;trip_guard::clear(s->trip);s->nextPoll=s->ms;}}else{s->held=!s->held;param(s,2,(float)heldNow(s));}}
 split(s);
}
static int init(void*p,const resona_platform*platform){P=platform;
    const int startup = resona_startup_splash_apply(P, kSplash, sizeof kSplash);
    if (startup < 0 && startup != RESONA_ERR_UNSUPPORTED)
        P->log("Startup animation could not be saved");
    auto*s=(State*)p;s->key=2;s->amount=68;s->mix=65;s->level=82;s->stage=0;Saved v={};int r=P->app_store_read(0,&v,sizeof v);if(r==RESONA_OK&&(v.magic==kMagicV1||v.magic==kMagicV2||v.magic==kMagicV3)&&v.key>=0&&v.key<24&&v.amount>=0&&v.amount<=100){s->key=v.key;s->amount=v.amount;if(v.magic!=kMagicV1&&(v.dry==0||v.dry==1))s->dry=v.dry;if(v.magic==kMagicV3){s->mix=clamp(v.mix,0,100);s->level=clamp(v.level,0,100);}}
 split(s);bind(s);P->input_config(RESONA_INPUT_SELECTOR,0);P->input_config(RESONA_INPUT_CUSTOMIZE,0);return 0;}
static void event(void*p,const resona_app_event*ev){ if(demo_capture::event(P,ev))return;auto*s=(State*)p;
 ctl::Msg m;
 if(ctl::receive(P,s->link,kBind,10,ev,m)){if(!m.changed)return;
  if(m.cc==CC_SUSPEND)s->padSuspend=m.down;else if(m.cc==CC_SOAR)s->padSoar=m.down;else if(m.cc==CC_SINK)s->padSink=m.down;else if(m.cc==CC_FLIP)s->padFlip=m.down;
  else{if(m.cc==CC_NOTE||m.cc==CC_MODE)s->key=s->major*12+s->note;changed(s);}
  push(s);return;}
 if(ev->kind==RESONA_APP_EV_INPUT)input(s,(int)ev->u.input.input,(int)ev->u.input.kind,ev->u.input.value);
#ifdef RESONA_DEMO_BENCH
 // Bench-only: MIDI channel 16 CC20–21 and CC24–27 emulate encoder gestures.
 else if(ev->kind==RESONA_APP_EV_MIDI&&ev->u.midi.len>=3){const uint8_t*m=ev->u.midi.data;if(m[0]==0xbf){int cc=m[1],v=m[2];if(cc==20||cc==21)input(s,cc-20,RESONA_EVENT_TURN,v-64);else if(v>0&&cc>=24&&cc<=27)input(s,(cc-24)&1,cc<26?RESONA_EVENT_CLICK:RESONA_EVENT_LONG,0);}}
#endif
 else if(ev->kind==RESONA_APP_EV_CHANGED&&ev->u.changed.what==RESONA_CHANGED_MODELS&&!s->ready&&s->error!=RESONA_ERR_BUDGET){s->stage=0;s->error=0;}
 else if(ev->kind==RESONA_APP_EV_AUDIO){if(ev->u.audio.tripped_role>=0||(s->ready&&trip_guard::faulted(P,g_audio.h[0])))trip(s);}
}
static void frame(void*p,uint32_t ms){ if(demo_capture::paused(P))return;auto*s=(State*)p;s->ms+=ms;setup(s);ctl::sync(P,s->link,kBind,10,s->ms);P->meters(&s->in,&s->out,nullptr);
 // A failed write is retried on the next change; it is not an audio error, so it never reaches s->error.
 if(s->savePending&&s->ms-s->changed>1200){Saved v={kMagicV3,s->key,s->amount,s->dry,s->mix,s->level};s->savePending=0;(void)P->app_store_write(0,&v,sizeof v);}
 if(s->ms%80<ms)draw(s);}
const resona_app app={RESONA_PLATFORM_VERSION,sizeof(resona_app),RESONA_APP_ID,RESONA_APP_NAME,RESONA_APP_VERSION,sizeof(State),1024,init,event,frame};
}
extern "C" const resona_app*resona_app_get(){return &app;}
