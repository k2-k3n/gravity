#!/usr/bin/env python3
"""Targeted host-only module ABI qualification. No USB or live audio opens."""
import ctypes as C,json,math
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];U=C.c_uint32;P=C.c_void_p;F=C.c_float
class Host(C.Structure):_fields_=[('abi',U),('rate',U),('frames',U),('channels',U),('controls',U),('device',C.c_char_p),('log',P),('size',U),('memory',P),('tools',P)]
class Module(C.Structure):_fields_=[('abi',U),('size',U),('id',C.c_char_p),('name',C.c_char_p),('version',C.c_char_p),('layouts',U),('rates',U),('state',U),('count',U),('params',P),('ext',P),('init',P),('reset',P),('set',P),('process',P)]
lib=C.CDLL(str(ROOT/'dsp/build/org-resona-demo-gravity-resonator-host.dylib'));lib.resona_module_get.restype=C.POINTER(Module);m=lib.resona_module_get().contents
init=C.CFUNCTYPE(None,P,C.POINTER(Host))(m.init);reset=C.CFUNCTYPE(None,P)(m.reset);setp=C.CFUNCTYPE(None,P,U,F)(m.set);process=C.CFUNCTYPE(None,P,C.POINTER(C.POINTER(F)),C.POINTER(C.POINTER(F)),U)(m.process)
host=Host(1,48000,256,1,2,b'resona-box',None,C.sizeof(Host),None,None)
state=C.create_string_buffer(m.state+127);sp=(C.addressof(state)+127)&~127
init(sp,C.byref(host));counts=[1,17,64,128,256]
class StateView(C.Structure):
 _fields_=[('values',U*5),('targets',U*15),('re',F*6),('im',F*6),('c',F*6),('sn',F*6),('radius',F),('hold',F),('mix',F),('level',F),('dc',F)]
view=StateView.from_address(sp)
checked_blocks=0;worst_rotation_norm_squared=0.;worst_pole_radius=0.
def inspect_poles():
 global checked_blocks,worst_rotation_norm_squared,worst_pole_radius
 checked_blocks+=1
 norms=[float(view.c[j])**2+float(view.sn[j])**2 for j in range(6)]
 worst_rotation_norm_squared=max(worst_rotation_norm_squared,max(norms))
 assert max(norms)<=1., ('rotation grew',max(norms))
 assert 0<=view.radius<=1 and 0<=view.hold<=1
 # The hold slew stays between its present and target value throughout a block.
 hmax=1. if view.values[2] else view.hold
 rmax=view.radius+(1-view.radius)*hmax
 pole=max(norms)**.5*rmax
 worst_pole_radius=max(worst_pole_radius,pole)
 assert pole<=1., ('pole grew',pole)

def run(x):
 y=np.zeros(len(x),np.float32);x=np.asarray(x,np.float32);pos=0;k=0
 while pos<len(x):
  n=min(counts[k%len(counts)],len(x)-pos);k+=1
  ai=x[pos:pos+n];ao=y[pos:pos+n];ip=(C.POINTER(F)*1)(ai.ctypes.data_as(C.POINTER(F)));op=(C.POINTER(F)*1)(ao.ctypes.data_as(C.POINTER(F)));process(sp,ip,op,n);inspect_poles();pos+=n
 return y
def rms(y):return float(np.sqrt(np.mean(np.square(y.astype(float)))))
def tone(n):
 t=np.arange(n)/48000;return np.sin(2*np.pi*146.832384*t)*.15+np.sin(2*np.pi*220*t)*.04
excite=tone(24000);silence=np.zeros(96000,np.float32);report={}
setp(sp,1,0);setp(sp,3,100);reset(sp);short=run(np.concatenate([excite,silence]));report['short_tail_rms_last_second']=rms(short[-48000:])
setp(sp,1,100);reset(sp);long=run(np.concatenate([excite,silence]));report['long_tail_rms_last_second']=rms(long[-48000:]);assert report['long_tail_rms_last_second']>report['short_tail_rms_last_second']*100
reset(sp);run(excite);setp(sp,2,1);held=run(silence);report['suspend_rms_first_500ms']=rms(held[:24000]);report['suspend_rms_last_500ms']=rms(held[-24000:]);assert report['suspend_rms_last_500ms']>report['suspend_rms_first_500ms']*.75
# Live guitar remains audible above held resonances: compare same held state, no input vs new note.
setp(sp,2,0);setp(sp,3,0);setp(sp,4,100);reset(sp);dry=run(excite);t=np.arange(len(excite))/48000;expected=excite.copy();dc=0
for i,x in enumerate(excite):dc+=.00065*(x-dc);expected[i]-=dc
report['mix_zero_dry_error']=float(np.max(np.abs(dry-expected)));assert report['mix_zero_dry_error']<1e-5
setp(sp,3,100);setp(sp,0,9);reset(sp);different=run(np.concatenate([excite,silence]));report['different_key_relative_error']=rms(long-different)/rms(long);assert report['different_key_relative_error']>.1
allout=np.concatenate([short,long,held,dry,different]);report['all_finite']=bool(np.all(np.isfinite(allout)));report['peak']=float(np.max(np.abs(allout)));assert report['all_finite'] and report['peak']<=.991
# Regression: all key targets at default FLOAT must decay, not settle at a
# bounded nonlinear limit cycle. Check every actual process() block's poles.
all_key_decay=[]
for key in range(24):
 setp(sp,0,key);setp(sp,1,68);setp(sp,2,0);setp(sp,3,100);setp(sp,4,82);reset(sp)
 freq=440.*2**((48+key%12-69)/12)
 t=np.arange(24000)/48000.;x=np.sin(2*np.pi*freq*t)*.08
 run(x);tail=run(np.zeros(480000,np.float32))
 first=rms(tail[:48000]);last=rms(tail[-48000:]);ratio=last/max(first,1e-20)
 assert ratio<.005, ('key tail did not decay',key,ratio)
 all_key_decay.append({'key':key,'first_second_rms':first,'tenth_second_rms':last,'ratio':ratio})
report['all_24_key_default_decay']=all_key_decay
transitions=[]
for old in [0,1,13]:
 setp(sp,0,old);setp(sp,1,100);setp(sp,2,0);setp(sp,3,100);reset(sp);run(excite)
 setp(sp,0,2);setp(sp,1,68);run(excite)
 setp(sp,2,1);frozen=run(np.zeros(192000,np.float32));hfirst=rms(frozen[24000:48000]);hlast=rms(frozen[-24000:])
 assert hlast<=hfirst*1.01 and hlast>=hfirst*.9, ('freeze grew or faded',old,hfirst,hlast)
 setp(sp,2,0);released=run(np.zeros(480000,np.float32));first=rms(released[:48000]);last=rms(released[-48000:]);ratio=last/max(first,1e-20)
 assert ratio<.005, ('release failed after transition',old,ratio)
 transitions.append({'key_from':old,'key_to':2,'float_from':100,'float_to':68,'held_first_window_rms':hfirst,'held_last_window_rms':hlast,'release_first_second_rms':first,'release_tenth_second_rms':last,'release_ratio':ratio})
report['key_transition_suspend_release']=transitions
report['process_blocks_with_pole_assertions']=checked_blocks
report['worst_rotation_norm_squared']=worst_rotation_norm_squared
report['worst_effective_pole_radius']=worst_pole_radius
report['frame_counts_tested']=counts;report['scope']='Host-only DSP ABI tests, no hardware CPU, analog audio or Windows runtime.'
(ROOT/'verification').mkdir(exist_ok=True);(ROOT/'verification/host-dsp.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
