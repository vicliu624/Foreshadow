"""弱候选时频细化诊断：已知 BW/SF，未知载波与同步字；不计入盲检验收。"""
import argparse
import json
from pathlib import Path
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--capture',required=True)
args=parser.parse_args();root=Path(args.capture)
trial=json.loads((root/'trial.json').read_text(encoding='utf-8'))
meta=json.loads((root/'capture/capture.sigmf-meta').read_text(encoding='utf-8'))
seeds=json.loads((root/'half-phase-prototype.json').read_text(encoding='utf-8'))['candidates']
rate=meta['global']['core:sample_rate'];receiver=meta['captures'][0]['core:frequency']
bw,sf=trial['case']['bandwidth'],trial['case']['sf'];n=round(rate*2**sf/bw);m=2**sf;bin_hz=rate/n
raw=np.fromfile(root/'capture/capture.sigmf-data',dtype=np.uint8).reshape(-1,2)
x=(raw[:,0].astype(float)-127.5+1j*(raw[:,1].astype(float)-127.5))/128
inverted=trial['case']['inverted']
if inverted:x=x.conj()
t=np.arange(n)/rate;reference=np.exp(2j*np.pi*(-bw*t/2+bw*t*t/(2*n/rate)))
frequencies=np.fft.fftfreq(n,1/rate)
up_templates=None
if m*n<=2000000:
    position=(np.arange(n)[None,:]/n+np.arange(m)[:,None]/m)%1
    up_templates=np.exp(2j*np.pi*(-bw*(n/rate)*position/2+bw*(n/rate)*position**2/2)).conj()

def projection(start,frequency,down=False,length=None,reference_offset=0):
    length=n if length is None else length
    if start<0 or start+length>len(x):return 0j,0.0,0.0
    samples=x[start:start+length]
    ref=reference[reference_offset:reference_offset+length]
    value=np.sum(samples*(ref if down else ref.conj())*np.exp(-2j*np.pi*frequency*np.arange(length)/rate))
    power=float(np.sum(abs(samples)**2))
    return value,float(abs(value)**2/max(power,1e-30)),power

def db(power):return float(10*np.log10(max(power,1e-30)))

def down_peak(start,up_bin):
    if start<0 or start+n>len(x):return None
    power=abs(np.fft.fft(x[start:start+n]*reference))**2
    indices=(up_bin+np.arange(-m,m+1))%n
    indices=indices[abs((frequencies[indices]+frequencies[up_bin])/2)<=500000+2*bin_hz]
    k=int(indices[np.argmax(power[indices])])
    return k,db(power[k]*n/power.sum())

def transition_scores(start,frequency):
    if up_templates is None or start<0 or start+4*n>len(x):return None
    scale=max(float(np.mean(abs(x[start:start+4*n])**2)),1e-30)
    scores=[]
    for fraction in (2,2.125,2.25,2.375,2.5,3):
        length=round(fraction*n);index=np.arange(length)
        down=x[start:start+length]*reference[index%n]*np.exp(-2j*np.pi*frequency*index/rate)
        down_energy=abs(np.sum(down))**2/length
        samples=x[start+length:start+length+n]*np.exp(-2j*np.pi*frequency*np.arange(n)/rate)
        powers=abs(up_templates@samples)**2/n
        scores.append({'down_symbols':fraction,'score':float((down_energy+float(powers.max()))/scale),
                       'up_symbol_hypothesis':int(powers.argmax())})
    return scores

def evaluate(down_start,frequency,seed_seconds):
    pre_start=down_start-8*n
    pre=[db(projection(pre_start+i*n,frequency)[1]) for i in range(6)]
    projected=[projection(pre_start+i*n,frequency) for i in range(6)]
    corrected=np.array([p[0] for p in projected])*np.exp(-2j*np.pi*frequency*n/rate*np.arange(6))
    coherence=float(abs(np.sum(corrected))**2/max(6*np.sum(abs(corrected)**2),1e-30))
    integrated=db(abs(np.sum(corrected))**2/max(sum(p[2] for p in projected),1e-30))
    sync=[]
    for i in (2,1):
        sync.append(max(db(projection(down_start-i*n,frequency+shift*bin_hz)[1])
                        for shift in range(-m//2,m//2+1,8)))
    downs=[db(projection(down_start+i*n,frequency,True)[1]) for i in (0,1)]
    pre_down=np.mean([projection(pre_start+i*n,frequency,True)[1] for i in range(6)])
    down_gain=db(np.mean([10**(v/10) for v in downs])/max(pre_down,1e-30))
    quarter=db(projection(down_start+2*n,frequency,True,n//4)[1])
    after=db(projection(down_start+2*n+n//4,frequency,True,n//4,n//4)[1])
    endpoint=None
    if down_start+3*n<=len(x):
        first_down=x[down_start:down_start+2*n]*np.tile(reference,2)*np.exp(-2j*np.pi*frequency*np.arange(2*n)/rate)
        amplitude=np.mean(first_down)
        third=x[down_start+2*n:down_start+3*n]*reference*np.exp(-2j*np.pi*frequency*np.arange(2*n,3*n)/rate)
        likelihood=2*np.real(amplitude.conjugate()*np.cumsum(third))-abs(amplitude)**2*np.arange(1,n+1)
        end=int(np.argmax(likelihood[n//8:n//2]))+n//8
        endpoint={'fraction':float((end+1)/n),'quarter_ll':float(likelihood[n//4-1]),'half_ll':float(likelihood[n//2-1])}
    valid=min(pre)>=11 and min(sync)>=11 and min(downs)>=9 and quarter>=7 and after<quarter-5
    return {'seed_seconds':seed_seconds,'down_start_sample':down_start,
                    'center_frequency_hz':receiver+(-frequency if inverted else frequency),
                    'preamble_db':pre,'sync_db':sync,'down_db':downs,'quarter_db':quarter,
                    'after_db':after,'preamble_coherence':coherence,'preamble_integrated_db':integrated,
                    'endpoint':endpoint,'down_direction_gain_db':down_gain,
                    'transition_scores':transition_scores(down_start,frequency),'full_structure':valid}

results=[]
for seed in seeds:
    if not seed['narrow_gate'] or not seed['third_consistent']:continue
    base=round(seed['time_seconds']*rate)
    for phase in (0,1):
        up=seed['peaks'][phase]['bin'];first=base+phase*n//2
        for step in range(2,11):
            current=first+step*n;previous=current-n
            a,b=down_peak(previous,up),down_peak(current,up)
            if a is None or b is None or min(a[1],b[1])<9:continue
            distance=abs(a[0]-b[0])
            if min(distance,n-distance)>2:continue
            coarse=(frequencies[up]+frequencies[b[0]])/2
            down_start=previous-round((frequencies[up]-frequencies[b[0]])*n/(2*bw))
            pre_start=down_start-8*n
            if pre_start<0:continue
            z=np.array([projection(pre_start+i*n,coarse)[0] for i in range(6)])
            fraction=np.angle(np.sum(z[1:]*z[:-1].conj()))/(2*np.pi)
            refined_bin=np.floor(coarse/bin_hz-fraction+.5)+fraction
            for correction in (-1,0,1):
                frequency=(refined_bin+correction)*bin_hz
                if abs(frequency)>500000:continue
                for adjustment in range(-int(np.ceil(rate/bw)),int(np.ceil(rate/bw))+1):
                    aligned=down_start+adjustment
                    if aligned-8*n<0 or aligned+3*n>len(x):continue
                    candidate=evaluate(aligned,frequency,seed['time_seconds'])
                    candidate['timing_adjustment_samples']=adjustment
                    results.append(candidate)

(root/'refined-candidates.json').write_text(json.dumps({'note':__doc__,'candidates':results},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'candidates':len(results),'matches':[r for r in results if r['full_structure']],
                  'best':sorted(results,key=lambda r:min(r['preamble_db']),reverse=True)[:2]}))
