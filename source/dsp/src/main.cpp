// MIT. Six input-excited damped complex resonators; no synthesizer source.
#include <resona/module.h>
#include <resona/math.h>
namespace {
const resona_param params[]={{"KEY",0,23,2,""},{"FLOAT",0,100,68,"%"},{"SUSPEND",0,1,0,""},{"MIX",0,100,65,"%"},{"LEVEL",0,100,82,"%"}};
struct State {uint32_t values[5],targets[15];float sr;float re[6],im[6],c[6],sn[6],radius,hold,mix,level,dc;};
static void store(uint32_t *p,float f){uint32_t v;__builtin_memcpy(&v,&f,4);__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static float load(const uint32_t *p){uint32_t v=__atomic_load_n(p,__ATOMIC_ACQUIRE);float f;__builtin_memcpy(&f,&v,4);return f;}
static float clamp(float f,float a,float b){return f<a?a:(f>b?b:f);}
// Normalize each rotation before publication. The inward float margin keeps the
// represented vector inside the unit disk even after rounding on either host.
static void normalize(float &c,float &sn){
 float n=c*c+sn*sn;float inv=1.5f-.5f*n;
 inv*=1.5f-.5f*n*inv*inv; // two Newton inverse-square-root steps, n is near 1
 const float inward=.99999988f;c*=inv*inward;sn*=inv*inward;
}
static float slew(float current,float target,float alpha,float snap){float d=target-current;return resona_fabsf(d)<=snap?target:current+alpha*d;}
static void coefficients(State &s){
 int key=(int)load(&s.values[0]);float amount=load(&s.values[1])*.01f;bool major=key>=12;key%=12;
 static const int minorSteps[6]={0,3,7,12,15,19},majorSteps[6]={0,4,7,12,16,19};
 for(unsigned j=0;j<6;j++) {int note=48+key+(major?majorSteps[j]:minorSteps[j]);float f=440.f*resona_exp2f((note-69)/12.f);float w=6.28318530718f*f/s.sr;float c=resona_cosf(w),sn=resona_sinf(w);normalize(c,sn);store(&s.targets[j*2],c);store(&s.targets[j*2+1],sn);}
 float tau=.028f+amount*amount*2.5f;
 store(&s.targets[12],resona_expf(-1.f/(tau*s.sr)));
 store(&s.targets[13],load(&s.values[3])*.01f);store(&s.targets[14],load(&s.values[4])*.01f);
}
static void reset(void *p){auto &s=*(State*)p;for(unsigned j=0;j<6;j++){s.re[j]=s.im[j]=0;s.c[j]=load(&s.targets[j*2]);s.sn[j]=load(&s.targets[j*2+1]);}s.radius=load(&s.targets[12]);s.hold=load(&s.values[2]);s.mix=load(&s.targets[13]);s.level=load(&s.targets[14]);s.dc=0;}
static void init(void *p,const resona_host*h){auto &s=*(State*)p;s.sr=(float)h->sample_rate;for(unsigned i=0;i<5;i++)store(&s.values[i],params[i].def);coefficients(s);reset(p);}
static void set(void*p,uint32_t n,float f){if(n>=5)return;auto&s=*(State*)p;store(&s.values[n],clamp(f,params[n].min,params[n].max));coefficients(s);}
static float get(const void*p,uint32_t n){return n<5?load(&((const State*)p)->values[n]):0;}
static float squash(float x){return x/(1.f+resona_fabsf(x));}
static void process(void*p,const float*const*in,float*const*out,uint32_t frames){
 auto&s=*(State*)p;float targetC[6],targetS[6];for(unsigned j=0;j<6;j++){targetC[j]=load(&s.targets[j*2]);targetS[j]=load(&s.targets[j*2+1]);}
 float targetHold=load(&s.values[2]),targetR=load(&s.targets[12]),targetMix=load(&s.targets[13]),targetLevel=load(&s.targets[14]);
 // Smooth the angle once per block, then normalize it. Independent per-sample
 // c/s slews can stall at different float ULPs and make a pole grow after a key
 // change. Every sample in this block instead uses one bounded rotation.
 float alpha=(float)frames/(1250.f+(float)frames);
 for(unsigned j=0;j<6;j++){
  float cross=s.c[j]*targetS[j]-s.sn[j]*targetC[j];
  if(resona_fabsf(cross)<.00001f){s.c[j]=targetC[j];s.sn[j]=targetS[j];}
  else{s.c[j]+=alpha*(targetC[j]-s.c[j]);s.sn[j]+=alpha*(targetS[j]-s.sn[j]);normalize(s.c[j],s.sn[j]);}
 }
 // Damping changes are tiny, so landing exactly is preferable to a float32
 // slew that can permanently stop before the requested radius.
 s.radius=clamp(slew(s.radius,targetR,alpha,.00004f),0.f,1.f);
 for(uint32_t i=0;i<frames;i++){
  float x=in[0][i];s.dc+=.00065f*(x-s.dc);float dry=x-s.dc;
  s.hold=slew(s.hold,targetHold,.0015f,.00003f);s.mix=slew(s.mix,targetMix,.001f,.00004f);s.level=slew(s.level,targetLevel,.001f,.00004f);
  float r=s.radius+(1.f-s.radius)*s.hold;float excite=clamp(dry,-3.f,3.f)*.025f*(1.f-s.hold);float wet=0;
  for(unsigned j=0;j<6;j++){
   float old=s.re[j],ri=s.im[j];float re=(old*s.c[j]-ri*s.sn[j])*r+excite;float im=(old*s.sn[j]+ri*s.c[j])*r;
   // Bounding each resonator pair also bounds accidental sustained full-scale excitation.
   float peak=resona_fabsf(re)>resona_fabsf(im)?resona_fabsf(re):resona_fabsf(im);if(peak>2.5f){float g=2.5f/peak;re*=g;im*=g;}
   s.re[j]=resona_fabsf(re)<1e-20f?0:re;s.im[j]=resona_fabsf(im)<1e-20f?0:im;wet+=re;
  }
  // Below MIX 65 the dry path gains up to +8.6 dB (x2.7 at MIX 0) so the App's DRY toggle
  // (MIX 0) is level-matched to FLOAT (MIX 65) instead of dropping ~9 dB (gated loudness, guitar DI).
  wet=squash(wet*.55f);float make=1.f+1.7f*clamp(1.f-s.mix*1.53846f,0.f,1.f);float y=dry*(1.f-.45f*s.mix)*make+wet*s.mix;y*=s.level;float a=resona_fabsf(y);if(a>.9f){float over=a-.9f;y=(y<0?-1.f:1.f)*(.9f+.09f*over/(.09f+over));}out[0][i]=y;
 }
}
static void prepare(void*p,uint32_t phase){set(p,0,phase==1?23.f:2.f);set(p,1,100);set(p,2,phase==1?1.f:0.f);set(p,3,100);set(p,4,100);}
const resona_cpu_profile cpu={RESONA_CPU_PROFILE_VERSION,sizeof(resona_cpu_profile),20,prepare};
const resona_module module={RESONA_MODULE_ABI,sizeof(resona_module),"org.resona.demo.gravity.resonator","Gravity Resonator","0.1.1",RESONA_LAYOUT_MONO,RESONA_RATE_48000,sizeof(State),5,params,nullptr,init,reset,set,process,nullptr,nullptr,get,&cpu};
}
extern "C" RESONA_EXPORT const resona_module *resona_module_get(){return &module;}
