"""诊断半符号间隔的 CSS 斜率候选；使用已知 BW/SF，不是盲检或完整 LoRa 判定。"""
import argparse
import json
from pathlib import Path
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--capture',required=True)
args=parser.parse_args()
root=Path(args.capture)
trial=json.loads((root/'trial.json').read_text(encoding='utf-8'))
meta=json.loads((root/'capture/capture.sigmf-meta').read_text(encoding='utf-8'))
bw,sf=trial['case']['bandwidth'],trial['case']['sf']
rate=meta['global']['core:sample_rate'];n=round(rate*2**sf/bw);m=2**sf
raw=np.fromfile(root/'capture/capture.sigmf-data',dtype=np.uint8).reshape(-1,2)
x=(raw[:,0].astype(float)-127.5+1j*(raw[:,1].astype(float)-127.5))/128
if trial['case']['inverted']:x=x.conj()
t=np.arange(n)/rate
ref=np.exp(2j*np.pi*(-bw*t/2+bw*t*t/(2*n/rate)))
frames=(len(x)-n//2)//n
found=[]
for frame in range(0,frames,4):
    peaks=[]
    for phase in (0,n//2):
        start=frame*n+phase
        power=abs(np.fft.fft(x[start:start+n]*ref.conj()))**2
        k=int(power.argmax())
        ratio=10*np.log10(power[k]*n/power.sum())
        narrow=10*np.log10(power[k]/max(power[(k-4)%n],power[(k+4)%n],1e-30))
        peaks.append({'bin':k,'ratio_db':float(ratio),'narrow_contrast_db':float(narrow)})
    a,b=peaks;difference=(b['bin']-a['bin'])%n
    slope=min(abs(difference-m//2),abs(difference-(n-m//2)))<=2
    if slope and min(a['ratio_db'],b['ratio_db'])>=9:
        third_consistent=False
        if (frame+2)*n<=len(x):
            third=abs(np.fft.fft(x[(frame+1)*n:(frame+2)*n]*ref.conj()))**2
            k=int(third.argmax());distance=abs(k-a['bin'])
            third_ratio=10*np.log10(third[k]*n/third.sum())
            third_narrow=10*np.log10(third[k]/max(third[(k-4)%n],third[(k+4)%n],1e-30))
            third_consistent=min(distance,n-distance)<=2 and third_ratio>=9 and third_narrow>=3.5
        found.append({'time_seconds':frame*n/rate,'peaks':peaks,
                      'narrow_gate':min(a['narrow_contrast_db'],b['narrow_contrast_db'])>=3.5,
                      'third_consistent':bool(third_consistent)})
result={'note':__doc__,'bandwidth_hz':bw,'sf':sf,'candidates':found}
(root/'half-phase-prototype.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'candidate_count':len(found),'narrow_count':sum(row['narrow_gate'] for row in found),
                  'third_count':sum(row['narrow_gate'] and row['third_consistent'] for row in found),
                  'narrow_candidates':[row for row in found if row['narrow_gate']]}))
