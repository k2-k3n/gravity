// MIT. Six input-excited damped complex resonators; no synthesizer source.
#include <resona/module.h>
#include <resona/math.h>
namespace {
const resona_param params[]={{"KEY",0,23,2,""},{"FLOAT",0,100,68,"%"},{"SUSPEND",0,1,0,""},{"MIX",0,100,65,"%"},{"LEVEL",0,100,82,"%"}};
struct State {uint32_t values[5],targets[15];float re[6],im[6],c[6],sn[6],radius,hold,mix,level,dc;};
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
 // This module declares 48 kHz only. Use bounded rotations tabulated for that rate.
 static constexpr float rotation[31][2]={
  {0.9998531990f, 0.0171225130f},
  {0.9998352464f, 0.0181405620f},
  {0.9998150955f, 0.0192191269f},
  {0.9997924770f, 0.0203618021f},
  {0.9997670888f, 0.0215723953f},
  {0.9997385917f, 0.0228549396f},
  {0.9997066052f, 0.0242137068f},
  {0.9996707019f, 0.0256532216f},
  {0.9996304023f, 0.0271782764f},
  {0.9995851683f, 0.0287939466f},
  {0.9995343956f, 0.0305056074f},
  {0.9994774061f, 0.0323189511f},
  {0.9994134390f, 0.0342400057f},
  {0.9993416399f, 0.0362751539f},
  {0.9992610502f, 0.0384311541f},
  {0.9991705939f, 0.0407151613f},
  {0.9990690633f, 0.0431347503f},
  {0.9989551033f, 0.0456979393f},
  {0.9988271926f, 0.0484132149f},
  {0.9986836242f, 0.0512895584f},
  {0.9985224823f, 0.0543364737f},
  {0.9983416169f, 0.0575640154f},
  {0.9981386155f, 0.0609828199f},
  {0.9979107704f, 0.0646041357f},
  {0.9976550436f, 0.0684398573f},
  {0.9973680259f, 0.0725025581f},
  {0.9970458922f, 0.0768055261f},
  {0.9966843506f, 0.0813628001f},
  {0.9962785859f, 0.0861892064f},
  {0.9958231959f, 0.0913003976f},
  {0.9953121203f, 0.0967128903f},
 };
 for(unsigned j=0;j<6;j++){
  const float *rot=rotation[key+(major?majorSteps[j]:minorSteps[j])];
  store(&s.targets[j*2],rot[0]);
  store(&s.targets[j*2+1],rot[1]);
 }
 const float tau=.028f+amount*amount*2.5f;
 const float step=1.f/(tau*48000.f);
 store(&s.targets[12],1.f-step*(1.f-.5f*step));
 store(&s.targets[13],load(&s.values[3])*.01f);store(&s.targets[14],load(&s.values[4])*.01f);
}
static void reset(void *p){auto &s=*(State*)p;for(unsigned j=0;j<6;j++){s.re[j]=s.im[j]=0;s.c[j]=load(&s.targets[j*2]);s.sn[j]=load(&s.targets[j*2+1]);}s.radius=load(&s.targets[12]);s.hold=load(&s.values[2]);s.mix=load(&s.targets[13]);s.level=load(&s.targets[14]);s.dc=0;}
static void init(void *p,const resona_host*){auto &s=*(State*)p;for(unsigned i=0;i<5;i++)store(&s.values[i],params[i].def);coefficients(s);reset(p);}
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
  wet=squash(wet*.55f);float y=dry*(1.f-.45f*s.mix)+wet*s.mix;y*=s.level;float a=resona_fabsf(y);if(a>.9f){float over=a-.9f;y=(y<0?-1.f:1.f)*(.9f+.09f*over/(.09f+over));}out[0][i]=y;
 }
}
static void prepare(void*p,uint32_t phase){set(p,0,phase==1?23.f:2.f);set(p,1,100);set(p,2,phase==1?1.f:0.f);set(p,3,100);set(p,4,100);}
const resona_cpu_profile cpu={RESONA_CPU_PROFILE_VERSION,sizeof(resona_cpu_profile),20,prepare};
const resona_module module={RESONA_MODULE_ABI,sizeof(resona_module),"org.resona.demo.gravity.resonator","Gravity Resonator","0.1.3",RESONA_LAYOUT_MONO,RESONA_RATE_48000,sizeof(State),5,params,nullptr,init,reset,set,process,nullptr,nullptr,get,&cpu};
}
extern "C" RESONA_EXPORT const resona_module *resona_module_get(){return &module;}
