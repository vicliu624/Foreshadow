"""使用已知发射参数定位实测短包并输出逐符号证据；不是盲检验收。"""
import argparse
import json
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--capture', required=True, help='rf_diagnostic_capture 输出目录')
args = parser.parse_args()
root = Path(args.capture)
trial = json.loads((root/'trial.json').read_text(encoding='utf-8'))
meta = json.loads((root/'capture/capture.sigmf-meta').read_text(encoding='utf-8'))
case = trial['case']
rate = meta['global']['core:sample_rate']
center = meta['captures'][0]['core:frequency']
bw, sf = case['bandwidth'], case['sf']
n = round(rate*2**sf/bw)
raw = np.fromfile(root/'capture/capture.sigmf-data', dtype=np.uint8).reshape(-1,2)
x = ((raw[:,0].astype(float)-127.5)+1j*(raw[:,1].astype(float)-127.5))/128
if case['inverted']:
    x = x.conj()
offset = (case['frequency']-center)*(-1 if case['inverted'] else 1)
t = np.arange(n)/rate
ref = np.exp(2j*np.pi*(-bw*t/2+bw*t*t/(2*n/rate)))
frequency = np.fft.fftfreq(n, 1/rate)
indices = np.flatnonzero(abs(frequency-offset)<=bw/2)
best = None
for phase in (0,n//2):
    frames = min((len(x)-phase)//n, int(1.5*rate/n))
    power = (abs(np.fft.fft(x[phase:phase+frames*n].reshape(-1,n)*ref.conj(),axis=1))**2).astype(np.float32)
    cumulative = np.vstack([np.zeros((1,n)), np.cumsum(power,axis=0,dtype=float)])
    accumulated = cumulative[6:]-cumulative[:-6]
    restricted = accumulated[:,indices]
    local_peak = np.argmax(restricted,axis=1)
    contrast = restricted[np.arange(len(restricted)),local_peak]/np.maximum(np.median(restricted,axis=1),1e-30)
    start = int(np.argmax(contrast))
    candidate = (float(contrast[start]),phase,start,int(indices[local_peak[start]]),power)
    if best is None or candidate[0]>best[0]:
        best = candidate
contrast,phase,start,target,power = best
rows = []
extent = 2**sf//2
deltas = [4+(i+1)*(extent-4)//32 for i in range(32)]
for frame in range(max(0,start-2),min(len(power),start+15)):
    p = power[frame]
    sample=phase+frame*n
    down=abs(np.fft.fft(x[sample:sample+n]*ref))**2
    down_index=int(np.argmax(down));known_down=int(indices[np.argmax(down[indices])])
    dominant = int(np.argmax(p))
    neighbors = [p[(target+d)%n] for d in deltas]+[p[(target-d)%n] for d in deltas]
    floor = np.partition(neighbors,48)[48]
    rows.append({'sample':phase+frame*n,'time_seconds':(phase+frame*n)/rate,
                 'dominant_offset_hz':float(frequency[dominant]),
                 'dominant_peak_to_mean_db':float(10*np.log10(max(float(p[dominant])*n/float(p.sum()),1e-30))),
                 'target_peak_to_mean_db':float(10*np.log10(max(float(p[target])*n/float(p.sum()),1e-30))),
                 'target_prominence_db':float(10*np.log10(max(float(p[target])/max(float(floor),1e-30),1e-30))),
                 'down_dominant_offset_hz':float(frequency[down_index]),
                 'down_known_band_offset_hz':float(frequency[known_down]),
                 'down_known_band_peak_to_mean_db':float(10*np.log10(max(float(down[known_down])*n/float(down.sum()),1e-30)))})
result = {'note':'发射参数辅助定位，不代表盲检或解码成功','case':case,'rate_hz':rate,'symbol_samples':n,
          'phase_samples':phase,'six_symbol_contrast_db':10*np.log10(contrast),'target_offset_hz':float(frequency[target]),'frames':rows}
(root/'assisted-frame-trace.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({key:result[key] for key in ('symbol_samples','six_symbol_contrast_db','target_offset_hz')}))
print(json.dumps(rows))
