"""Discover narrowband bursts and ASK, FSK and PSK candidates in SigMF I/Q."""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

import numpy as np


MAX_PAIRS = 4_194_304
FORMATS = {'cu8': ('u1', 127.5, 128), 'ci16_le': ('<i2', 0, 32768)}


@dataclass(frozen=True)
class Recording:
    data: Path
    rate: int
    center: float
    datatype: str
    pairs: int

    def read(self, first, count):
        if not 0 < count <= MAX_PAIRS:
            raise ValueError(f'Each analysis window must contain 1 to {MAX_PAIRS} I/Q pairs')
        dtype, midpoint, scale = FORMATS[self.datatype]
        stride = 2*np.dtype(dtype).itemsize
        raw = np.fromfile(self.data, dtype=dtype, count=count*2, offset=first*stride)
        if len(raw) != count*2:
            raise ValueError('The I/Q file length changed during reading')
        raw = raw.reshape(-1, 2).astype(np.float32)
        return ((raw[:, 0]-midpoint)+1j*(raw[:, 1]-midpoint))/scale


def inspect_recording(meta):
    info = json.loads(meta.read_text(encoding='utf-8'))
    capture = info['captures']
    if len(capture) != 1 or capture[0]['core:sample_start'] != 0:
        raise ValueError('Expected a single capture with a fixed center frequency')
    rate = int(info['global']['core:sample_rate'])
    center = float(capture[0]['core:frequency'])
    if rate <= 0 or not np.isfinite(center):
        raise ValueError('Invalid recording sample rate or center frequency')
    datatype = info['global']['core:datatype']
    if datatype not in FORMATS:
        raise ValueError('Supported sample formats are CU8 and CI16_LE')
    data = meta.with_suffix('.sigmf-data')
    stride = 2*np.dtype(FORMATS[datatype][0]).itemsize
    byte_count = data.stat().st_size
    if byte_count % stride:
        raise ValueError('The I/Q file contains an incomplete sample pair')
    return Recording(data, rate, center, datatype, byte_count//stride)


class Channelizer:
    """共享宽带 FFT，在窄带采样率上直接重建各信道。"""

    def __init__(self, samples, rate, width):
        self.rate = rate
        self.width = width
        self.decimation = max(1, int(rate//max(64_000, width*2)))
        self.channel_rate = rate/self.decimation
        self.count = (len(samples)+self.decimation-1)//self.decimation
        # 保护区与抽取采样格对齐，保持返回序列的第一个样本对应录制起点。
        guard = min(len(samples), max(32, round(rate/width*12)))
        self.guard = (guard+self.decimation-1)//self.decimation
        narrow_size = fft_length(self.count+2*self.guard)
        self.wide_size = narrow_size*self.decimation
        self.bin_hz = rate/self.wide_size
        padded = np.pad(samples, (self.guard*self.decimation, 0))
        self.spectrum = np.fft.fft(padded, self.wide_size)
        self.bins = np.arange(narrow_size)
        self.bins[(narrow_size+1)//2:] -= narrow_size
        taper = np.clip((width/2-abs(self.bins)*self.bin_hz)/(width*.1), 0, 1)
        self.response = (.5-.5*np.cos(np.pi*taper)).astype(np.float32)

    def extract(self, offset):
        if abs(offset)+self.width/2 >= self.rate/2:
            raise ValueError('The target channel must fit inside the recorded band')
        shift = round(offset/self.bin_hz)
        spectrum = self.spectrum[(self.bins+shift)%self.wide_size]*self.response
        filtered = np.fft.ifft(spectrum)[self.guard:self.guard+self.count]/self.decimation
        # FFT 先移整数频点，窄带输出再补偿剩余频偏和保护区相位。
        residual = offset-shift*self.bin_hz
        phase = shift*self.bin_hz*self.guard/self.channel_rate
        filtered *= np.exp(2j*np.pi*(phase-residual*np.arange(self.count)/self.channel_rate))
        return filtered, self.channel_rate


def extract_channel(samples, rate, offset, width):
    return Channelizer(samples, rate, width).extract(offset)


def fft_length(minimum):
    """给长时段滤波选接近原长度的 FFT，避免大质因数开销。"""
    length = ((minimum+255)//256)*256
    while True:
        remainder = length
        for factor in (2, 3, 5):
            while remainder%factor == 0:
                remainder //= factor
        if remainder == 1:
            return length
        length += 256


def frame_power(frames, window):
    spectrum = np.fft.fft(frames*window, axis=1)
    return (abs(spectrum)**2).astype(np.float32, copy=False)


def power_blocks(windows, window):
    for start in range(0, len(windows), 128):
        yield start, frame_power(windows[start:start+128], window)


def spectral_background(windows, window, percentile):
    # 分层抽取背景帧，限制长录制中的逐频点排序开销。
    if len(windows) > 384:
        bounds = np.arange(385)*len(windows)//384
        indices = bounds[:-1]+(np.arange(384)*53+19)%np.diff(bounds)
        windows = windows[indices]
    return np.percentile(frame_power(windows, window), percentile, axis=0)


def discover_channels(samples, rate, width, limit):
    """用短时功率相对各频点背景的变化寻找窄带候选。"""
    nfft = 2**max(8, min(13, round(np.log2(rate/1000))))
    hop = nfft//2
    if len(samples) < nfft+15*hop:
        return [], 0
    windows = np.lib.stride_tricks.sliding_window_view(samples, nfft)[::hop]
    window = np.hanning(nfft).astype(np.float32)
    floor = spectral_background(windows, window, 25)
    reference = np.maximum(floor, max(float(np.median(floor))*.25, 1e-12))
    strongest = np.zeros(nfft, dtype=np.float32)
    repeated = np.zeros(nfft, dtype=bool)
    tail = np.zeros((2, nfft), dtype=bool)
    for _, power in power_blocks(windows, window):
        np.maximum(strongest, power.max(axis=0), out=strongest)
        above = np.concatenate((tail, power > 100*reference))
        repeated |= (above[:-2] & above[1:-1] & above[2:]).any(axis=0)
        tail = above[-2:].copy()
    # 只重排汇总结果，避免为每一帧复制整行频谱。
    strongest, repeated, reference = (np.fft.fftshift(values) for values in (strongest, repeated, reference))
    # 单帧脉冲和稳定载波不应占用信道名额。
    bin_hz = rate/nfft
    # 与相邻频点的最强峰比较，滤掉强载波或键控边沿的旁瓣。
    neighborhood = max(1, round(width/bin_hz))
    local_peak = np.lib.stride_tricks.sliding_window_view(
        np.pad(strongest, neighborhood, mode='edge'), 2*neighborhood+1).max(axis=1)
    active = np.flatnonzero(repeated & (strongest >= .005*local_peak))
    if active.size:
        active = active[strongest[active] >= strongest[active].max()*1e-4]
    gap_bins = max(1, round(width*.30/bin_hz))
    groups = np.split(active, np.flatnonzero(np.diff(active)>gap_bins)+1)
    candidates = []
    for group in groups:
        if not len(group):
            continue
        span = (group[-1]-group[0]+1)*bin_hz
        if span > width*.8:
            continue
        offset = (float(group[0]+group[-1])/2-nfft//2)*bin_hz
        if abs(offset)+width/2 >= rate/2:
            continue
        strength = float(np.max(strongest[group]/reference[group]))
        candidates.append((strength, offset))
    candidates.sort(reverse=True)
    return sorted(offset for _, offset in candidates[:limit]), len(candidates)


def blind_css(samples, rate, center, time_origin):
    """在三种时频分辨率上查找重复扫频并去除重复报告。"""
    middle = max(6, min(10, round(np.log2(rate*.00015))))
    candidates = []
    for exponent in (middle-2, middle, middle+2):
        nfft = 2**exponent
        for candidate in sweep_ridges(samples, rate, center, time_origin, nfft):
            candidates.append({**candidate, 'stft_window_samples': nfft})
    kept = []
    for candidate in sorted(candidates, key=lambda c: (c['repeated_chirps'], c['observed_span_hz']), reverse=True):
        if not any(same_sweep(candidate, other) for other in kept):
            kept.append(candidate)
    return sorted(kept, key=lambda c: c['start_seconds'])


def same_sweep(first, second):
    overlap = min(first['end_seconds'], second['end_seconds'])-max(first['start_seconds'], second['start_seconds'])
    shorter = min(first['end_seconds']-first['start_seconds'], second['end_seconds']-second['start_seconds'])
    return (overlap >= .5*shorter
            and abs(first['observed_mid_frequency_hz']-second['observed_mid_frequency_hz'])
            <= .2*max(first['observed_span_hz'], second['observed_span_hz'])
            and abs(first['sweep_rate_hz_per_second']-second['sweep_rate_hz_per_second'])
            <= .15*abs(first['sweep_rate_hz_per_second'])
            and abs(first['repeat_interval_seconds']-second['repeat_interval_seconds'])
            <= .18*first['repeat_interval_seconds'])


def sweep_ridges(samples, rate, center, time_origin, nfft):
    hop = nfft//4
    points = 12 if nfft <= 64 else 8
    if len(samples) < nfft+points*hop:
        return []
    frames = np.lib.stride_tricks.sliding_window_view(samples, nfft)[::hop]
    window = np.hanning(nfft).astype(np.float32)
    floor = spectral_background(frames, window, 50)
    background_penalty = 4*floor
    bins = np.empty(len(frames))
    quality = np.empty(len(frames), dtype=bool)
    for first, power in power_blocks(frames, window):
        frame = np.arange(len(power))
        # 持续谱线只有在超过自己的背景时才会成为脊线。
        peak = (np.argmax(power-background_penalty, axis=1)+nfft//2)%nfft
        peak = peak.clip(1, nfft-2)
        raw_peak = (peak+nfft//2)%nfft
        target = slice(first, first+len(power))
        quality[target] = power[frame, raw_peak] >= np.maximum(15*floor[raw_peak], 1e-10)
        a, b, c = (np.log(np.maximum(power[frame, (raw_peak+shift)%nfft], 1e-20)) for shift in (-1, 0, 1))
        fraction = .5*(a-c)/np.minimum(a-2*b+c, -1e-6)
        bins[target] = peak+fraction.clip(-.5, .5)

    windows = np.lib.stride_tricks.sliding_window_view(bins, points)
    valid = np.lib.stride_tricks.sliding_window_view(quality, points).all(axis=1)
    axis = np.arange(points)-(points-1)/2
    slope = (windows@axis)/(axis@axis)
    residual = np.max(abs(windows-windows.mean(axis=1, keepdims=True)-slope[:, None]*axis), axis=1)
    indices = np.flatnonzero(valid & (abs(slope)*(points-1)>=3) & (residual<=.65))
    if not len(indices):
        return []
    steps = slope[indices]
    breaks = ((np.diff(indices) != 1) | (steps[1:]*steps[:-1] <= 0)
              | (abs(np.diff(steps)) > .45*np.maximum(abs(steps[1:]), abs(steps[:-1]))))
    bin_hz = rate/nfft
    sweeps = []
    for group in np.split(indices, np.flatnonzero(breaks)+1):
        first, last = group[0], group[-1]+points-1
        low, high = sorted((bins[first], bins[last]))
        span = (high-low)*bin_hz
        if len(group)<3 or span<40000:
            continue
        sweeps.append(((first*hop+nfft/2)/rate, (last*hop+nfft/2)/rate,
                       center+((low+high)/2-nfft/2)*bin_hz, span,
                       float(np.median(slope[group])*bin_hz*rate/hop)))

    events = []
    repeated = []
    for sweep in sweeps:
        if repeated:
            first, previous = repeated[0], repeated[-1]
            period = sweep[0]-previous[0]
            same = (first[4]*sweep[4]>0 and abs(sweep[4]-first[4])<=.15*abs(first[4])
                    and abs(sweep[2]-first[2])<=.18*first[3]
                    and abs(sweep[3]-first[3])<=.28*first[3]
                    and 0 < period <= 2*(first[1]-first[0]))
            if len(repeated)>1:
                expected = (previous[0]-first[0])/(len(repeated)-1)
                same = same and abs(period-expected)<=.18*expected
            if not same:
                append_css_ridge(events, repeated, time_origin)
                repeated = []
        repeated.append(sweep)
    append_css_ridge(events, repeated, time_origin)
    return events


def append_css_ridge(events, sweeps, time_origin):
    if len(sweeps) < 3:
        return
    values = np.asarray(sweeps)
    events.append({
        'modulation_candidate': 'CSS',
        'evidence_level': 'repeated_sweep_ridge',
        'start_seconds': time_origin+sweeps[0][0],
        'end_seconds': time_origin+sweeps[-1][1],
        'observed_mid_frequency_hz': float(np.median(values[:, 2])),
        'observed_span_hz': float(np.median(values[:, 3])),
        'sweep_rate_hz_per_second': float(np.median(values[:, 4])),
        'repeat_interval_seconds': float(np.median(np.diff(values[:, 0]))),
        'repeated_chirps': len(sweeps),
    })


def two_levels(values):
    """按两个稳态电平分组，并检查两组在时间上多次交替。"""
    if len(values) < 16:
        return None
    low, high = np.percentile(values, [20, 80])
    for _ in range(8):
        upper = values > (low+high)/2
        if min(upper.sum(), (~upper).sum()) < len(values)*.12:
            return None
        low, high = np.median(values[~upper]), np.median(values[upper])
    separation = high-low
    error = np.median(abs(values-np.where(upper, high, low)))
    transitions = np.count_nonzero(upper[1:] != upper[:-1])
    if separation <= 0 or error > separation*.22 or transitions < 6:
        return None
    return float(low), float(high)


def four_tones(frequency, rate, width):
    centers = np.percentile(frequency, [12.5, 37.5, 62.5, 87.5])
    for _ in range(8):
        groups = np.searchsorted((centers[:-1]+centers[1:])/2, frequency)
        if np.bincount(groups, minlength=4).min() < .1*len(frequency):
            return None
        centers = np.array([np.median(frequency[groups==i]) for i in range(4)])
    gaps = np.diff(centers)
    if gaps.min() < 800 or gaps.min() < .65*gaps.max() or centers[-1]-centers[0] > width*.8:
        return None
    # 真正的四频状态应各自停留多次，不能由二频切换边缘凑成四组。
    changes = np.flatnonzero(groups[1:] != groups[:-1])+1
    starts = np.r_[0, changes]
    lengths = np.diff(np.r_[starts, len(groups)])
    dwell = np.bincount(groups[starts][lengths >= max(3, round(rate*.00012))], minlength=4)
    if dwell.min() < 3:
        return None
    for i in range(4):
        if np.percentile(abs(frequency[groups==i]-centers[i]), 75) > gaps.min()*.22:
            return None
    return centers.tolist()


def burst_ranges(power, rate):
    block = max(1, round(rate*.001))
    size = len(power)//block
    if size < 12:
        return []
    levels = power[:size*block].reshape(size, block).mean(axis=1)
    floor = max(float(np.percentile(levels, 20)), 1e-12)
    active = np.flatnonzero(levels > max(floor*5, float(levels.max())*.002))
    groups = np.split(active, np.flatnonzero(np.diff(active)>5)+1)
    return [(int(g[0])*block, (int(g[-1])+1)*block, floor)
            for g in groups if len(g)>=4]


def prefix_evidence(burst, rate):
    samples = burst[:65536]
    spectrum = abs(np.fft.fft(samples, fft_length(2*len(samples))))**2
    total = spectrum.sum()
    if total <= 0 or spectrum.max() > .12*total:
        return None
    maximum = min(len(samples)//10, round(rate*.004))
    minimum = max(24, round(rate*.000020))
    correlation = abs(np.fft.ifft(spectrum)[:maximum+1])
    if maximum <= minimum or correlation[0] <= 0:
        return None
    correlation /= correlation[0]
    peaks = np.flatnonzero((correlation[1:-1]>correlation[:-2]) &
                           (correlation[1:-1]>=correlation[2:]))+1
    peaks = peaks[(peaks>=minimum) & (correlation[peaks]>=.035)]
    best = None
    for lag in peaks[np.argsort(correlation[peaks])[-8:]]:
        local = np.r_[correlation[max(1, lag-16):lag-3], correlation[lag+4:lag+17]]
        if local.size == 0 or correlation[lag] < 2*np.median(local):
            continue
        product = samples[lag:]*samples[:-lag].conj()
        sample_power = abs(samples)**2
        index = np.arange(len(product))
        for divisor in (4, 8, 16, 32):
            prefix = round(lag/divisor)
            if prefix < 6:
                continue
            period = lag+prefix
            phase = index%period
            folded = np.bincount(phase, weights=product.real, minlength=period)
            folded = folded+1j*np.bincount(phase, weights=product.imag, minlength=period)
            folded /= np.bincount(phase, minlength=period)
            extended = np.r_[folded, folded[:prefix-1]]
            accumulated = np.r_[0, np.cumsum(extended)]
            sums = accumulated[prefix:]-accumulated[:-prefix]
            first = int(np.argmax(abs(sums)))
            starts = np.arange(first, len(product)-prefix+1, period)
            if len(starts)<8:
                continue
            points = starts[:, None]+np.arange(prefix)
            cross = product[points].sum(axis=1)
            power_a = sample_power[points].sum(axis=1)
            power_b = sample_power[points+lag].sum(axis=1)
            norm = np.sqrt(power_a*power_b)
            score = abs(cross.sum())/max(norm.sum(), 1e-20)
            coherence = abs(cross)/np.maximum(norm, 1e-20)
            control = abs(sums[(first+period//2)%period])/max(abs(sums[first]), 1e-20)
            if score < .62 or np.mean(coherence>.55)<.7 or control>.38:
                continue
            if best is None or score > best['prefix_coherence']:
                best = {'useful_symbol_seconds': float(lag/rate), 'prefix_seconds': prefix/rate,
                        'prefix_coherence': float(score), 'checked_symbols': len(starts)}
    return best


def ofdm_candidates(samples, rate, origin):
    events = []
    for first, last, _ in burst_ranges(abs(samples)**2, rate):
        evidence = prefix_evidence(samples[first:last], rate)
        if evidence:
            events.append({'modulation_candidate': 'OFDM', 'evidence_level': 'cyclic_prefix_candidate',
                           'start_seconds': origin+first/rate, 'end_seconds': origin+last/rate, **evidence})
    return events


def phase_modulation(burst, rate, width, floor):
    # 保留突发内部，避免包头和包尾的噪声干扰载波估计。
    edge = max(1, round(rate*.001))
    samples = burst[edge:min(len(burst)-edge, edge+65536)]
    if len(samples) < 128:
        return None
    amplitude = abs(samples)
    median = np.median(amplitude)
    if median**2 < 8*floor:
        return None
    unit = samples/np.maximum(amplitude, 1e-12)
    index = np.arange(len(unit))
    fft_size = 2**int(np.ceil(np.log2(len(unit)*2)))
    frequencies = np.fft.fftfreq(fft_size, 1/rate)
    window = np.hanning(len(unit))*np.minimum(amplitude/median, 1)
    instant = np.angle(samples[1:]*samples[:-1].conj())*(rate/(2*np.pi))
    strong = (amplitude[1:] >= .65*median) & (amplitude[:-1] >= .65*median)
    for order, label in ((2, 'BPSK'), (4, 'QPSK')):
        spectrum = abs(np.fft.fft(unit**order*window, fft_size))**2
        allowed = abs(frequencies) <= min(width*.35*order, rate*.45)
        peak = np.argmax(np.where(allowed, spectrum, 0))
        if spectrum[peak] < .16*spectrum.sum():
            continue
        carrier = frequencies[peak]/order
        # 相位跳变之间的瞬时频率应回到同一个载波，区别于多个 FSK 稳态。
        if np.mean(abs(instant[strong]-carrier) < max(600, width*.03)) < .65:
            continue
        corrected = unit*np.exp(-2j*np.pi*carrier/rate*index)
        best = 0.0
        for symbol_rate in (1200, 2400, 4800, 9600, 19200):
            period = rate/symbol_rate
            if period < 4:
                continue
            symbols = np.arange(int((len(unit)-1)/period))
            if len(symbols) < 24:
                continue
            for phase in np.arange(8)/8:
                locations = np.rint((symbols+phase)*period).astype(int)
                valid = amplitude[locations] >= .45*median
                if valid.sum() < .8*len(locations):
                    continue
                constellation = corrected[locations[valid]]
                moment = (constellation**order).mean()
                if abs(moment) < .83:
                    continue
                rotated = constellation*np.exp(-1j*np.angle(moment)/order)
                phase_states = np.rint(np.angle(rotated)*order/(2*np.pi)).astype(int)%order
                if np.bincount(phase_states, minlength=order).min() < .12*len(phase_states):
                    continue
                if np.count_nonzero(np.diff(phase_states)) < .2*len(phase_states):
                    continue
                best = max(best, float(abs(moment)))
        if best:
            return {'modulation_candidate': label, 'carrier_offset_hz': float(carrier),
                    'phase_coherence': best}
    return None


def classify(channel, rate, width, time_origin=0.0):
    power = abs(channel)**2
    events = []
    smooth = max(1, round(rate*.00010))
    average = np.ones(smooth)/smooth
    for first, last, floor in burst_ranges(power, rate):
        burst = channel[first:last]
        energy = np.convolve(abs(burst)**2, average, mode='valid')
        frequency = np.angle(burst[1:]*burst[:-1].conj())*(rate/(2*np.pi))
        frequency = np.convolve(frequency, average, mode='valid')
        amplitude = two_levels(energy)
        # 对 FSK 先要求幅度接近恒定，避免把 OOK 关闭期间的噪声误当频移。
        result = phase_modulation(burst, rate, width, floor)
        if result is None and np.percentile(energy, 20) > .4*np.percentile(energy, 80):
            tones = four_tones(frequency, rate, width)
            if tones is not None:
                result = {'modulation_candidate': '4-FSK', 'tone_offsets_hz': tones}
            else:
                frequencies = two_levels(frequency)
                if frequencies is not None:
                    low, high = frequencies
                    if 800 <= high-low <= width*.8:
                        result = {'modulation_candidate': '2-FSK/GFSK', 'tone_offsets_hz': [low, high]}
        if result is None and amplitude is not None:
            low, high = amplitude
            if high > 3*max(low, floor) and high > 8*floor:
                # 高电平须呈现稳定载波，排除包络变化的宽带噪声突发。
                gate = energy[:-1] > (low+high)/2
                on = frequency[gate]
                if len(on) and np.percentile(abs(on-np.median(on)), 80) < max(400, width*.025):
                    result = {'modulation_candidate': 'OOK' if low <= max(3*floor, .04*high) else 'ASK',
                              'power_level_ratio_db': float(10*np.log10(high/max(low, 1e-12))),
                              'carrier_offset_hz': float(np.median(on))}
        events.append({'start_seconds': time_origin+first/rate, 'end_seconds': time_origin+last/rate,
                       **(result or {'modulation_candidate': 'unknown'})})
    return events


def detect_lora(executable, samples, rate, center, time_origin, search_width=1000000):
    # CU8 的归一化可精确反转。同一时段交给两套检测器，保留原录制。
    with TemporaryDirectory(prefix='signalscope-combined-') as temporary:
        meta = Path(temporary)/'segment.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': rate},
            'captures': [{'core:frequency': center, 'core:sample_start': 0}],
        }), encoding='utf-8')
        raw = np.rint(127.5+128*np.column_stack((samples.real, samples.imag))).clip(0, 255).astype('uint8')
        raw.tofile(meta.with_suffix('.sigmf-data'))
        result = subprocess.run(
            [str(executable.resolve()), 'detect', '--input', str(meta), '--threshold-db', '11',
             '--search-width', str(search_width)],
            capture_output=True, timeout=120, check=False,
        )
    if result.returncode:
        raise ValueError('LoRa detection failed: '+result.stderr[-1024:].decode('utf-8', errors='replace'))
    events = []
    css = []
    for event in json.loads(result.stdout)['events']:
        if event['evidence_level'] == 'css_candidate' and event['peak_concentration'] >= .08:
            resolution = event['bandwidth_hz']/(1 << event['sf_candidate'])
            low, high = event['center_frequency_interval_hz']
            css.append({
                'modulation_candidate': 'CSS',
                'evidence_level': 'css_candidate',
                'bandwidth_hz': event['bandwidth_hz'],
                'sf_equivalent': event['sf_candidate'],
                'direction': event['preamble_direction'],
                'center_frequency_interval_hz': [low-resolution, high+resolution],
                'start_seconds': time_origin+event['first_observed_sample']/rate,
                'end_seconds': time_origin+event['confirmed_at_sample']/rate,
                'repeated_chirps': event['repeated_chirps'],
                'peak_concentration': event['peak_concentration'],
            })
        if event['evidence_level'] != 'lora_structure_match':
            continue
        events.append({
            'modulation_candidate': 'LoRa',
            'evidence_level': event['evidence_level'],
            'center_frequency_hz': event['center_frequency_hz'],
            'bandwidth_hz': event['bandwidth_hz'],
            'sf': event['sf_candidate'],
            'iq_inverted': event['iq_inverted'],
            'structure_start_seconds': time_origin+event['first_observed_sample']/rate,
            'sfd_start_seconds': time_origin+event['sfd_start_sample']/rate,
            'sfd_end_seconds': time_origin+event['sfd_end_sample']/rate,
            'related_unknown_fragments': [],
        })
    return events, summarize_css(css, events)


def persistent_lines(samples, rate):
    nfft = 2**max(8, min(13, round(np.log2(rate/850))))
    if len(samples) < 4*nfft:
        return []
    frames = np.lib.stride_tricks.sliding_window_view(samples, nfft)[::nfft//4]
    window = np.hanning(nfft).astype(np.float32)
    spectrum = np.fft.fftshift(spectral_background(frames, window, 70))
    local = np.median(np.lib.stride_tricks.sliding_window_view(
        np.pad(spectrum, 12, mode='edge'), 25), axis=1)
    strongest = spectrum.max()
    bin_hz = rate/nfft
    peaks = []
    for peak in np.argsort(spectrum)[::-1]:
        if spectrum[peak] < .12*strongest:
            break
        if not 2 <= peak < nfft-2 or spectrum[peak] < 24*max(local[peak], 1e-15):
            continue
        if any(abs(peak-other) * bin_hz < 3500 for other in peaks):
            continue
        peaks.append(int(peak))
        if len(peaks) == 2:
            break
    if not peaks:
        return []
    raw = (np.array(peaks)+nfft//2)%nfft
    count = np.zeros(len(peaks), dtype=int)
    for _, power in power_blocks(frames, window):
        count += np.count_nonzero(power[:, raw] > 10*local[peaks], axis=0)
    lines = []
    for peak, hits in zip(peaks, count):
        if hits < .35*len(frames):
            continue
        a, b, c = np.log(np.maximum(spectrum[peak-1:peak+2], 1e-20))
        fraction = float(np.clip(.5*(a-c)/min(a-2*b+c, -1e-9), -.5, .5))
        lines.append((peak+fraction-nfft//2)*bin_hz)
    return lines


def apply_line_notches(samples, rate, lines):
    # 留出两端保护区，避免片尾强信号通过循环卷积串到片头。
    guard = min(len(samples), max(1, round(rate*.008)))
    size = fft_length(len(samples)+2*guard)
    spectrum = np.fft.fft(np.pad(samples, (guard, 0)), size)
    frequencies = np.fft.fftfreq(size, 1/rate)
    for tone in lines:
        taper = np.clip((abs(frequencies-tone)-1000)/500, 0, 1)
        spectrum *= .5-.5*np.cos(np.pi*taper)
    filtered = np.fft.ifft(spectrum)[guard:guard+len(samples)]
    return np.clip(filtered.real, -127.5/128, 127.5/128) + 1j*np.clip(
        filtered.imag, -127.5/128, 127.5/128)


def recover_lora(executable, samples, rate, center, origin, originals):
    lines = persistent_lines(samples, rate)
    recovered = []
    if lines:
        filtered = apply_line_notches(samples, rate, lines)
        candidates, _ = detect_lora(executable, filtered, rate, center, origin)
        for event in candidates:
            if not any(same_lora_event(event, original) for original in originals):
                recovered.append({**event, 'evidence_level': 'filtered_lora_structure_match'})
    return {'notch_frequencies_hz': [center+tone for tone in lines], 'recovered_events': recovered}


def same_lora_event(event, other):
    bandwidth = event['bandwidth_hz']
    period = (1 << event['sf'])/bandwidth
    return (event['sf'] == other['sf'] and bandwidth == other['bandwidth_hz']
            and event['iq_inverted'] == other['iq_inverted']
            and abs(event['sfd_start_seconds']-other['sfd_start_seconds']) <= .5*period
            and abs(event['center_frequency_hz']-other['center_frequency_hz']) <= 2/period)


def recover_split_lora(executable, samples, rate, center, origin, originals):
    # 在两侧窄化分析范围，缓解不同频带的同模板包争夺峰值。
    channelizer = Channelizer(samples, rate, rate*.4)
    recovered = []
    for offset in (-.2*rate, .2*rate):
        channel, channel_rate = channelizer.extract(offset)
        if channel_rate != rate:
            raise ValueError('Split-band LoRa analysis requires the original sample rate')
        candidates, _ = detect_lora(executable, channel, rate, center+offset, origin, search_width=200000)
        for event in candidates:
            if any(same_lora_event(event, other) for other in (*originals, *recovered)):
                continue
            recovered.append({**event, 'evidence_level': 'subband_lora_structure_match',
                              'analysis_center_hz': center+offset})
    return sorted(recovered, key=lambda event: event['structure_start_seconds'])


def summarize_css(candidates, lora_events):
    kept = []
    for candidate in sorted(candidates, key=lambda item: -item['peak_concentration']):
        low, high = candidate['center_frequency_interval_hz']
        confirmed = any(
            candidate['bandwidth_hz'] == event['bandwidth_hz']
            and candidate['sf_equivalent'] == event['sf']
            and (candidate['direction'] == 'down') == event['iq_inverted']
            and low <= event['center_frequency_hz'] <= high
            and candidate['start_seconds'] < event['sfd_end_seconds']
            and event['structure_start_seconds'] < candidate['end_seconds']
            for event in lora_events
        )
        if confirmed:
            continue
        duplicate = next((other for other in kept
                          if candidate['bandwidth_hz'] == other['bandwidth_hz']
                          and candidate['sf_equivalent'] == other['sf_equivalent']
                          and candidate['direction'] == other['direction']
                          and max(candidate['start_seconds'], other['start_seconds'])
                          < min(candidate['end_seconds'], other['end_seconds'])
                          and low < other['center_frequency_interval_hz'][1]
                          and other['center_frequency_interval_hz'][0] < high), None)
        if duplicate is None:
            kept.append({**candidate, 'matching_windows': 1})
        else:
            duplicate['matching_windows'] += 1
    return sorted(kept, key=lambda item: item['start_seconds'])


def estimate_lora_activity(events, channels, channel_width, sweeps, samples, rate, center, time_origin):
    eligible = [
        event for event in events
        if any(abs(channel['channel_hz']-event['center_frequency_hz'])+channel_width/2 <= event['bandwidth_hz']/2
               and any(fragment['modulation_candidate'] == 'unknown'
                       and event['sfd_end_seconds'] < fragment['end_seconds'] for fragment in channel['events'])
               for channel in channels)
        or any(sweep['end_seconds'] > event['sfd_end_seconds'] and sweep_matches_lora(sweep, event)
               for sweep in sweeps)
    ]
    # 限制重复宽带滤波的成本，剩余事件仍保留完整的结构证据。
    for event in eligible[:16]:
        offset = event['center_frequency_hz']-center
        width = event['bandwidth_hz']
        if abs(offset)+width/2 >= rate/2:
            continue
        channel, channel_rate = extract_channel(samples, rate, offset, width)
        for first, last, _ in burst_ranges(abs(channel)**2, channel_rate):
            start = time_origin+first/channel_rate
            end = time_origin+last/channel_rate
            if start <= event['sfd_start_seconds'] and event['sfd_end_seconds'] <= end:
                event['band_activity_end_seconds'] = end
                break


def associate_unknown(channels, width, lora_events):
    for channel in channels:
        frequency = channel['channel_hz']
        remaining = []
        for event in channel['events']:
            related = []
            if event['modulation_candidate'] == 'unknown':
                related = [lora for lora in lora_events
                           if abs(frequency-lora['center_frequency_hz'])+width/2 <= lora['bandwidth_hz']/2
                           and lora['structure_start_seconds'] <= event['start_seconds']
                           and event['end_seconds'] <= lora.get('band_activity_end_seconds', lora['sfd_end_seconds'])]
            if len(related) == 1:
                lora = related[0]
                relation = ('within_structure_window' if event['end_seconds'] <= lora['sfd_end_seconds']
                            else 'within_continuing_band_activity')
                lora['related_unknown_fragments'].append({'channel_hz': frequency, 'relation': relation, **event})
            else:
                remaining.append(event)
        channel['events'] = remaining


def sweep_matches_lora(candidate, event):
    bandwidth = event['bandwidth_hz']
    period = (1 << event['sf'])/bandwidth
    slope = (-1 if event['iq_inverted'] else 1)*bandwidth/period
    return (abs(candidate['observed_mid_frequency_hz']-event['center_frequency_hz']) <= bandwidth*.2
            and abs(candidate['sweep_rate_hz_per_second']-slope) <= .15*abs(slope)
            and abs(candidate['repeat_interval_seconds']-period) <= .18*period)


def associate_blind_css(candidates, lora_events):
    remaining = []
    for candidate in candidates:
        related = []
        for event in lora_events:
            if not sweep_matches_lora(candidate, event):
                continue
            period = (1 << event['sf'])/event['bandwidth_hz']
            overlap = min(candidate['end_seconds'], event['sfd_start_seconds'])-max(
                candidate['start_seconds'], event['structure_start_seconds'])
            if overlap >= 2*period:
                related.append((event, 'preamble_overlap'))
            elif ('band_activity_end_seconds' in event
                  and candidate['start_seconds'] >= event['sfd_end_seconds']
                  and candidate['end_seconds'] <= event['band_activity_end_seconds']+.001):
                # 频带活动的时间分辨率为 1 ms。
                related.append((event, 'within_continuing_band_activity'))
        if len(related) == 1:
            event, relation = related[0]
            event.setdefault('blind_css_evidence', []).append({**candidate, 'relation': relation})
        else:
            remaining.append(candidate)
    return remaining


def scan_window(recording, first, count, args):
    samples = recording.read(first, count)
    sample_rate, center = recording.rate, recording.center
    origin = first/sample_rate
    offsets, discovered = discover_channels(samples, sample_rate, args.width_hz, args.max_channels)
    channels = []
    if offsets:
        channelizer = Channelizer(samples, sample_rate, args.width_hz)
        for offset in offsets:
            channel, rate = channelizer.extract(offset)
            channels.append({'channel_hz': center+offset, 'events': classify(channel, rate, args.width_hz, origin)})
    result = {'evidence': 'waveform_candidate', 'capture_center_hz': center,
              'sample_rate_hz': sample_rate, 'channel_width_hz': args.width_hz,
              'analysis_start_seconds': origin, 'analysis_duration_seconds': count/sample_rate,
              'discovered_channels': discovered, 'channels': channels}
    if args.blind_css:
        result['blind_css_candidates'] = blind_css(samples, sample_rate, center, origin)
    if args.ofdm:
        result['ofdm_candidates'] = ofdm_candidates(samples, sample_rate, origin)
    if args.lora_exe is not None:
        result['lora_events'], result['css_candidates'] = detect_lora(
            args.lora_exe, samples, sample_rate, center, origin)
        estimate_lora_activity(result['lora_events'], channels, args.width_hz,
                               result.get('blind_css_candidates', []), samples, sample_rate, center, origin)
        if args.blind_css:
            result['blind_css_candidates'] = associate_blind_css(
                result['blind_css_candidates'], result['lora_events'])
        associate_unknown(channels, args.width_hz, result['lora_events'])
        if args.lora_notch:
            result['lora_notch'] = recover_lora(
                args.lora_exe, samples, sample_rate, center, origin, result['lora_events'])
        if args.lora_split:
            result['lora_split_events'] = recover_split_lora(
                args.lora_exe, samples, sample_rate, center, origin, result['lora_events'])
    return result


def scan_all(recording, first, end, args):
    rate = recording.rate
    margin = min(rate//4, MAX_PAIRS//4)
    core = min(rate, MAX_PAIRS-2*margin)
    for left in range(first, end, core):
        right = min(end, left+core)
        begin = max(first, left-margin)
        finish = min(end, right+margin)
        result = scan_window(recording, begin, finish-begin, args)
        lo, hi = left/rate, right/rate
        # 同一个突发可能在相邻分析窗中出现，按起点归到一个核心窗。
        for channel in result['channels']:
            channel['events'] = [event for event in channel['events'] if lo <= event['start_seconds'] < hi]
        for key, field in (('lora_events', 'structure_start_seconds'),
                           ('lora_split_events', 'structure_start_seconds'),
                           ('css_candidates', 'start_seconds'), ('blind_css_candidates', 'start_seconds'),
                           ('ofdm_candidates', 'start_seconds')):
            if key in result:
                result[key] = [event for event in result[key] if lo <= event[field] < hi]
        if 'lora_notch' in result:
            result['lora_notch']['recovered_events'] = [
                event for event in result['lora_notch']['recovered_events']
                if lo <= event['structure_start_seconds'] < hi]
        result.update(report_start_seconds=lo, report_end_seconds=hi)
        print(json.dumps(result), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--channel-hz', type=int, help='Target channel center, defaulting to the recording center')
    mode.add_argument('--scan', action='store_true', help='Discover and analyze narrowband bursts in the recorded band')
    mode.add_argument('--scan-all', action='store_true', help='Scan the selected interval in windows and emit one JSON line per window')
    parser.add_argument('--width-hz', type=int, default=40_000)
    parser.add_argument('--max-channels', type=int, default=8)
    parser.add_argument('--start-seconds', type=float, default=0.0)
    parser.add_argument('--duration-seconds', type=float)
    parser.add_argument('--lora-exe', type=Path, help='Run the specified SignalScope CLI for LoRa detection during a scan')
    parser.add_argument('--blind-css', action='store_true', help='Find repeated sweeps without a waveform template during a scan')
    parser.add_argument('--ofdm', action='store_true', help='Find OFDM cyclic-prefix candidates in wideband bursts')
    parser.add_argument('--lora-notch', action='store_true', help='Keep the original result and also try suppressing persistent narrowband lines')
    parser.add_argument('--lora-split', action='store_true', help='Repeat offline LoRa detection on either side of the capture center')
    args = parser.parse_args()
    if not 4000 <= args.width_hz <= 200000:
        parser.error('Channel width must be between 4000 and 200000 Hz')
    if not 1 <= args.max_channels <= 16:
        parser.error('Channel limit must be between 1 and 16')
    if not np.isfinite(args.start_seconds) or args.start_seconds < 0:
        parser.error('Start time must be finite and nonnegative')
    if args.duration_seconds is not None and (not np.isfinite(args.duration_seconds) or args.duration_seconds <= 0):
        parser.error('Analysis duration must be finite and positive')
    if (args.lora_exe is not None or args.blind_css or args.ofdm) and not (args.scan or args.scan_all):
        parser.error('Options --lora-exe, --blind-css and --ofdm require --scan or --scan-all')
    if (args.lora_notch or args.lora_split) and args.lora_exe is None:
        parser.error('Options --lora-notch and --lora-split require --lora-exe')
    try:
        recording = inspect_recording(args.input)
        rate, center = recording.rate, recording.center
        if args.width_hz >= rate:
            raise ValueError('Channel width must be smaller than the recording sample rate')
        if args.lora_exe is not None and recording.datatype != 'cu8':
            raise ValueError('Combined LoRa detection currently requires a CU8 recording')
        if args.lora_split and rate < 1750000:
            raise ValueError('Split-band LoRa detection requires at least 1.75 MS/s')
        first = round(args.start_seconds*rate)
        end = recording.pairs
        if args.duration_seconds is not None:
            end = min(end, first+round(args.duration_seconds*rate))
        if first >= end:
            raise ValueError('The selected interval contains no I/Q samples')
        if args.scan_all:
            scan_all(recording, first, end, args)
            return
        if args.scan:
            result = scan_window(recording, first, end-first, args)
        else:
            samples = recording.read(first, end-first)
            target = center if args.channel_hz is None else args.channel_hz
            channel, channel_rate = extract_channel(samples, rate, target-center, args.width_hz)
            result = {'channel_hz': target, 'channel_width_hz': args.width_hz,
                      'evidence': 'waveform_candidate', 'events': classify(channel, channel_rate, args.width_hz, first/rate),
                      'analysis_start_seconds': first/rate, 'analysis_duration_seconds': (end-first)/rate}
    except (OSError, ValueError, KeyError, OverflowError, subprocess.TimeoutExpired) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
