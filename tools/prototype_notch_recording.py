"""离线因果窄带抑制原型；保留原始文件，输出不得冒充原始射频录制。"""
import argparse
import json
from pathlib import Path
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input',required=True)
parser.add_argument('--output',required=True)
parser.add_argument('--learn-seconds',type=float,default=.1)
parser.add_argument('--half-width-hz',type=float,default=2500)
parser.add_argument('--max-notches',type=int,default=4)
args=parser.parse_args()
meta_path=Path(args.input);meta=json.loads(meta_path.read_text(encoding='utf-8'))
rate=meta['global']['core:sample_rate'];center=meta['captures'][0]['core:frequency']
raw=np.fromfile(meta_path.with_suffix('.sigmf-data'),dtype=np.uint8).reshape(-1,2)
x=(raw[:,0].astype(float)-127.5+1j*(raw[:,1].astype(float)-127.5))/128
learn=min(len(x),round(rate*args.learn_seconds));size=8192
if learn<size*4:raise ValueError('学习片段不足四个 PSD 窗')
blocks=x[:learn//size*size].reshape(-1,size)
spectra=abs(np.fft.fft(blocks*np.hanning(size),axis=1))**2
power=spectra.mean(axis=0);frequencies=np.fft.fftfreq(size,1/rate)
selected=[]
neighbors=np.r_[np.arange(-80,-20),np.arange(21,81)]
for k in np.argsort(power)[::-1]:
    frequency=float(frequencies[k])
    if any(abs(frequency-p['offset_hz'])<4000 for p in selected):continue
    local=power[(k+neighbors)%size]
    prominence=10*np.log10(power[k]/max(np.median(local),1e-30))
    if prominence<18:continue
    floor=np.median(spectra[:,(k+neighbors)%size],axis=1)
    persistence=float(np.mean(spectra[:,k]>10*np.maximum(floor,1e-30)))
    if persistence<.8:continue
    left=right=0
    while left<40 and power[(k-left-1)%size]>power[k]/10:left+=1
    while right<40 and power[(k+right+1)%size]>power[k]/10:right+=1
    width=(left+right+1)*rate/size
    if width>6000:continue
    log=np.log(np.maximum(power[(k+np.array([-1,0,1]))%size],1e-30))
    denominator=log[0]-2*log[1]+log[2]
    delta=float(np.clip(.5*(log[0]-log[2])/denominator,-.5,.5)) if abs(denominator)>1e-20 else 0
    frequency+=delta*rate/size
    selected.append({'offset_hz':frequency,'frequency_hz':center+frequency,'prominence_db':float(prominence),
                     'persistence':persistence,'width_hz':width})
    if len(selected)>=args.max_notches:break

def apply_notch(data,frequency):
    radius=np.exp(-2*np.pi*args.half_width_hz/rate)
    length=int(np.ceil(np.log(1e-6)/np.log(radius)))+1
    impulse=np.empty(length,dtype=complex);gain=(1+radius)/2;impulse[0]=gain
    k=np.arange(1,length)
    impulse[1:]=gain*(radius-1)*radius**(k-1)*np.exp(2j*np.pi*frequency*k/rate)
    fft_size=8192
    if length>=fft_size:raise ValueError('陷波过窄，超出原型计算预算')
    step=fft_size-length+1;transform=np.fft.fft(impulse,fft_size)
    output=np.empty_like(data);overlap=np.zeros(length-1,dtype=complex)
    for first in range(0,len(data),step):
        chunk=data[first:first+step];block=np.r_[overlap,chunk]
        filtered=np.fft.ifft(np.fft.fft(block,fft_size)*transform)
        output[first:first+len(chunk)]=filtered[length-1:length-1+len(chunk)]
        overlap=block[-(length-1):]
    return output

filtered=x
for notch in selected:filtered=apply_notch(filtered,notch['offset_hz'])
filtered[:learn]=x[:learn] # 学习期不使用未来得到的系数改写输入。
quantized=np.empty(raw.shape,dtype=np.uint8)
quantized[:,0]=np.clip(np.rint(filtered.real*128+127.5),0,255).astype(np.uint8)
quantized[:,1]=np.clip(np.rint(filtered.imag*128+127.5),0,255).astype(np.uint8)
destination=Path(args.output);destination.mkdir(parents=True,exist_ok=False)
(destination/'capture.sigmf-data').write_bytes(quantized.tobytes())
result={'note':__doc__,'learn_samples':learn,'notches':selected,'half_width_hz':args.half_width_hz,
        'original_power_dbfs':float(10*np.log10(np.mean(abs(x)**2))),
        'filtered_power_dbfs':float(10*np.log10(np.mean(abs(filtered)**2)))}
clean={'global':{'core:datatype':'cu8','core:sample_rate':rate,'core:version':'1.2.6',
                 'signalscope:source':'offline-notch-prototype-not-original','signalscope:processing':result},
       'captures':[{'core:sample_start':0,'core:frequency':center}],'annotations':[]}
(destination/'capture.sigmf-meta').write_text(json.dumps(clean,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
