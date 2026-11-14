"""有限被动 I/Q 的谱与时间结构调查；不推断设备身份或业务协议。"""
import argparse
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
meta = Path(args.input)
description = json.loads(meta.read_text(encoding='utf-8'))
rate = description['global']['core:sample_rate']
center = description['captures'][0]['core:frequency']
raw = np.fromfile(meta.with_suffix('.sigmf-data'), dtype=np.uint8).reshape(-1, 2)
x = (raw[:, 0].astype(float)-127.5+1j*(raw[:, 1].astype(float)-127.5))/128
n, hop = 8192, 4096
windows = np.lib.stride_tricks.sliding_window_view(x, n)[::hop]
window = np.hanning(n)
transformed = np.fft.fftshift(np.fft.fft(windows*window, axis=1), axes=1)
power = abs(transformed)**2/(rate*np.sum(window**2))
mean = power.mean(axis=0)
median = np.median(power, axis=0)
background = np.median(np.lib.stride_tricks.sliding_window_view(np.pad(median, 50, mode='edge'), 101), axis=1)
prominence = 10*np.log10(np.maximum(median, 1e-30)/np.maximum(background, 1e-30))
frequency = np.fft.fftshift(np.fft.fftfreq(n, 1/rate))
times = (np.arange(len(windows))*hop+n/2)/rate
peaks = [i for i in range(1, n-1) if prominence[i] > 8 and median[i] >= median[i-1] and median[i] > median[i+1]]
selected = []
for i in sorted(peaks, key=lambda i: prominence[i], reverse=True):
    if all(abs(i-j) > 8 for j in selected):
        selected.append(i)
    if len(selected) == 6:
        break
lines = []
for i in selected:
    lo, hi = i, i
    while lo > 0 and median[lo-1] > background[lo-1]*2:
        lo -= 1
    while hi < n-1 and median[hi+1] > background[hi+1]*2:
        hi += 1
    occupancy = np.mean(np.max(power[:, max(0, i-1):i+2], axis=1) > background[i]*4)
    lines.append({'frequency_hz': float(center+frequency[i]), 'offset_hz': float(frequency[i]),
                  'median_prominence_db': float(prominence[i]), 'above_local_floor_3db_width_hz': float((hi-lo+1)*rate/n),
                  'occupancy_above_floor6db': float(occupancy)})
report = {'note': __doc__, 'duration_seconds': len(x)/rate, 'sample_rate_hz': rate,
          'fft_bin_hz': rate/n, 'stft_window_seconds': n/rate,
          'mean_power_dbfs': float(10*np.log10(np.mean(abs(x)**2))),
          'dc_i': float(x.real.mean()), 'dc_q': float(x.imag.mean()), 'persistent_narrow_components': lines,
          'limits': '有限两秒、无接收载荷解调；持续窄带分量不能据此区分外部载波与内部杂散'}
output = Path(args.output)
output.parent.mkdir(parents=True, exist_ok=True)
output.with_suffix('.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
font_path = Path('C:/Windows/Fonts/msyh.ttc')
if font_path.exists():
    plt.rcParams['font.family'] = font_manager.FontProperties(fname=str(font_path)).get_name()
else:
    plt.rcParams['font.sans-serif'] = ['Noto Sans CJK SC', 'WenQuanYi Zen Hei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
fig, axes = plt.subplots(2, 2, figsize=(15, 9), constrained_layout=True)
fig.suptitle('被动诊断采集：探针提示不等于 LoRa 数据包（无测试发射）', fontsize=17)
floor = float(np.median(10*np.log10(np.maximum(mean, 1e-30))))
im = axes[0, 0].imshow(10*np.log10(np.maximum(power.T, 1e-30)), origin='lower', aspect='auto',
    extent=[times[0], times[-1], (center+frequency[0])/1e6, (center+frequency[-1])/1e6], vmin=floor-8, vmax=floor+24, cmap='magma')
axes[0, 0].set(title='连续时频图', xlabel='录制内时间 / 秒', ylabel='接收频率 / MHz')
fig.colorbar(im, ax=axes[0, 0], label='数字域 PSD / dBFS/Hz')
axes[0, 1].plot((center+frequency)/1e6, 10*np.log10(np.maximum(mean, 1e-30)), lw=.7)
axes[0, 1].plot((center+frequency)/1e6, 10*np.log10(np.maximum(background, 1e-30)), lw=1, label='局部背景参考')
axes[0, 1].set(title='平均功率谱与持续窄带分量', xlabel='接收频率 / MHz', ylabel='数字域 PSD / dBFS/Hz')
axes[0, 1].legend();axes[0, 1].grid(alpha=.25)
if selected:
    i = selected[0];mask=abs(frequency-frequency[i]) <= 10000
    axes[1, 0].imshow(10*np.log10(np.maximum(power[:, mask].T, 1e-30)), origin='lower', aspect='auto',
        extent=[times[0], times[-1], (center+frequency[mask][0])/1e6, (center+frequency[mask][-1])/1e6], vmin=floor-8, vmax=floor+24, cmap='magma')
    axes[1, 0].set(title='最突出窄带分量放大（长窗观察，非协议识别）', xlabel='录制内时间 / 秒', ylabel='接收频率 / MHz')
axes[1, 1].axis('off')
text = ['两秒原始采集；未发射测试信号', f'全带平均功率：{report["mean_power_dbfs"]:.2f} dBFS', '持续窄带分量（非设备身份）：']
for row in lines:
    text.append(f'{row["frequency_hz"]/1e6:.6f} MHz　局部突出 {row["median_prominence_db"]:.1f} dB　出现比例 {row["occupancy_above_floor6db"]:.0%}')
text += ['', '这些峰可能包含载波、接收链杂散或直流残留。', 'FFT 栅格约 214 Hz，频率未做 RF 标定。', '出现比例是短时谱超过背景阈值的比例，不是发包占空比。', '不能据此断言是 LoRa、抄表、FSK 或某个设备。', '需结合前导码、同步与 SFD；接收 CRC 未验证。']
axes[1, 1].text(.02, .96, '\n'.join(text), va='top', fontsize=10, linespacing=1.45)
fig.savefig(output.with_suffix('.png'), dpi=150)
plt.close(fig)
print(json.dumps(report, ensure_ascii=True))
