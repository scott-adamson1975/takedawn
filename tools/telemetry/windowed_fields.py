"""Windowed field search on a BO3_TELEMETRY recording (docs/wheel-force-feedback-proposal.md, "Findings").

Differences the car's position over 4-frame windows (a single frame is noisy: ~10% of
frames show no movement), then correlates every 4-byte slot of the car dump against
the car's local forward / right / up velocity, its yaw rate, and prints the pitch,
yaw, roll slots' correlation. Needs numpy.

  py -3 tools/telemetry/windowed_fields.py            (run1 in the current directory)
  run from the folder holding run1.csv and run1.bin; edit the file names below for another run.
"""
import numpy as np, csv
n=0x2900; W=4
raw=open('run1.bin','rb').read()[12:]
F=len(raw)//(8+n)
a=np.frombuffer(raw,dtype=np.uint8).reshape(F,8+n)[:,8:]
fl=np.ascontiguousarray(a).view('<f4').reshape(F,n//4).astype(np.float64)
fl[~np.isfinite(fl)]=0
r=list(csv.DictReader(open('run1.csv')))
g=lambda k:np.array([float(x[k]) for x in r])
pos=np.stack([g('px'),g('py'),g('pz')],1); t=g('race_time')
R=np.stack([g('rx'),g('ry'),g('rz')],1);U=np.stack([g('ux'),g('uy'),g('uz')],1);A=np.stack([g('ax'),g('ay'),g('az')],1)
i=np.arange(0,F-W); j=i+W
dt=t[j]-t[i]
vw=(pos[j]-pos[i])/dt[:,None]
spd=np.linalg.norm(vw,axis=1)
ok=(spd<110)&(dt>0.05)
mid=(i+j)//2
def loc(M): return np.einsum('ij,ij->i',vw,M[mid])
comps={'v_right':loc(R),'v_up':loc(U),'v_fwd':loc(A)}
# yaw rate: change of 'at' heading about world y
def yaw(M): return np.unwrap(np.arctan2(M[:,0],M[:,2]))
ya=yaw(A); comps['yaw_rate']=(ya[j]-ya[i])/dt
sm=lambda c:np.array([c[:,None]]) 
for name,v in comps.items():
    res=[]
    for s in range(n//4):
        c=fl[:,s]
        x=(c[i]+c[j]+c[mid])/3
        if x[ok].std()<1e-9 or np.abs(x).max()>1e5: continue
        cc=np.corrcoef(x[ok],v[ok])[0,1]
        if np.isfinite(cc): res.append((abs(cc),cc,hex(s*4),np.polyfit(x[ok],v[ok],1)[0]))
    res.sort(reverse=True)
    print(name,'  (std %.2f)'%v[ok].std())
    for x in res[:5]: print('   %+.3f %s scale %.3f'%(x[1],x[2],x[3]))
print('---- angular rates about the car axes (rad/s) vs 0x25D0/D4/D8 x60')
def rate(P,Q):   # rotation of axis P toward Q's direction over window
    return np.einsum('ij,ij->i',P[j]-P[i],Q[mid])/dt
w_right=rate(A,U)   # pitch: forward axis moves toward up
w_up=rate(R,A)      # yaw
w_fwd=rate(U,R)     # roll
for nm,v in (('pitch(about right)',w_right),('yaw(about up)',w_up),('roll(about fwd)',w_fwd)):
    print(nm,'std %.2f'%v[ok].std(),end='  ')
    for s in (0x25D0,0x25D4,0x25D8):
        x=(fl[i,s//4]+fl[j,s//4]+fl[mid,s//4])/3*60
        print(hex(s),'%+.2f'%np.corrcoef(x[ok],v[ok])[0,1],end='  ')
    print()
print('---- wheel slots vs mean, in corners (|yaw|>0.5 rad/s)')
cor=ok&(np.abs(w_up)>0.5)
for s in (0x578,0x57C,0x580,0x584):
    x=(fl[i,s//4]+fl[j,s//4]+fl[mid,s//4])/3
    print(hex(s),'mean %.1f'%x[ok].mean(),'corner/straight ratio to 0x57C: n/a', 'corr fwd %.3f'%np.corrcoef(x[ok],comps['v_fwd'][ok])[0,1])
x=[ (fl[mid,s//4]) for s in (0x578,0x57C,0x580,0x584)]
print('corner mean',[round(float(v[cor].mean()),1) for v in x],'straight mean',[round(float(v[ok&~cor].mean()),1) for v in x])
print('slot 0x64 vs 0x57c ratio on straights', float(np.median(fl[mid,0x57C//4][ok&~cor]/fl[mid,0x64//4][ok&~cor])))
