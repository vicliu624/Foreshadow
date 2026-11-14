"""已知 BW/SF/起点的辅助转换模型诊断；不是盲检或载荷解调。"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input', required=True)
parser.add_argument('--start', type=int, required=True)
parser.add_argument('--offset', type=float, required=True)
parser.add_argument('--bw', type=float, required=True)
parser.add_argument('--sf', type=int, required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--symbols', type=int, choices=(4, 5), default=4)
args = parser.parse_args()
meta = Path(args.input)
description = json.loads(meta.read_text(encoding='utf-8'))
rate = description['global']['core:sample_rate']
raw = np.fromfile(meta.with_suffix('.sigmf-data'), dtype=np.uint8).reshape(-1, 2)
x = (raw[:, 0].astype(float)-127.5+1j*(raw[:, 1].astype(float)-127.5))/128
n = round(rate*2**args.sf/args.bw)
m = 2**args.sf
if n*m > 2000000:
    raise ValueError('辅助原型只枚举有限大小的参考矩阵')
position = (np.arange(n)[None, :]/n+np.arange(m)[:, None]/m) % 1
templates = np.exp(-2j*np.pi*args.bw*n/rate*(-position/2+position**2/2))
window = args.symbols*n
time = np.arange(window)/rate
samples = x[args.start:args.start+window]*np.exp(-2j*np.pi*args.offset*time)
if len(samples) != window:
    raise ValueError('观察数据不足')
phase = np.arange(window) % n/n
down_reference = np.exp(2j*np.pi*args.bw*n/rate*(-phase/2+phase**2/2))
down = samples*down_reference
learn = 3*n//2
amplitude = np.mean(down[:learn])
noise = max(float(np.mean(abs(down[:learn]-amplitude)**2)), 1e-30)
models = []
for eighths in (15, 16, 17, 18, 19, 20, 24):
    length = eighths*n//8
    score = float(2*np.real(amplitude.conjugate()*np.sum(down[:length]))-abs(amplitude)**2*length)
    details = []
    for first in range(length, window, n):
        count = min(n, window-first)
        projections = abs(templates[:, :count]@samples[first:first+count])
        fitted = float(2*abs(amplitude)*projections.max()-abs(amplitude)**2*count)
        penalty = noise*(np.log(m)+.5*np.log(count))
        score += fitted-penalty
        details.append({'samples': count, 'fit': fitted, 'penalty': penalty})
    models.append({'down_symbols': eighths/8, 'score': score, 'parts': details})
result = {'note': __doc__, 'input_sha256': hashlib.sha256(raw.tobytes()).hexdigest(),
          'start_sample': args.start, 'frequency_offset_hz': args.offset,
          'bandwidth_hz': args.bw, 'sf': args.sf,
          'common_samples': window, 'noise_estimate': noise,
          'models': models, 'winner': max(models, key=lambda row: row['score'])['down_symbols']}
Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=True))
