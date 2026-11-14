"""验证循环前缀候选及无前缀多载波、单音反例。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


def main():
    rate = 1_750_000
    rng = np.random.default_rng(261106)
    time = np.arange(rate//4)/rate
    iq = .006*(rng.normal(size=len(time))+1j*rng.normal(size=len(time)))
    cases = [(0.02, 384, 48, -187000), (.09, 256, 64, 220000), (.15, 256, 0, -120000)]
    for start, useful, prefix, offset in cases:
        carriers = np.zeros((100, useful), dtype=complex)
        active = np.r_[np.arange(1, useful//4), np.arange(3*useful//4, useful)]
        shape = (len(carriers), len(active))
        carriers[:, active] = rng.choice([-3, -1, 1, 3], shape) + 1j*rng.choice([-3, -1, 1, 3], shape)
        symbols = np.fft.ifft(carriers, axis=1)
        if prefix:
            symbols = np.concatenate((symbols[:, -prefix:], symbols), axis=1)
        waveform = symbols.ravel()
        waveform *= .12/np.sqrt(np.mean(abs(waveform)**2))
        waveform += .22*np.r_[np.zeros(3), waveform[:-3]]
        first = round(rate*start)
        target = slice(first, first+len(waveform))
        iq[target] += waveform*np.exp(2j*np.pi*offset*time[target])
    iq[350000:405000] += .18*np.exp(2j*np.pi*110000*time[350000:405000])
    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'ofdm.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': rate},
            'captures': [{'core:frequency': 433920000, 'core:sample_start': 0}],
        }), encoding='utf-8')
        np.rint(127.5+128*np.column_stack((iq.real, iq.imag))).clip(0, 255).astype('uint8').tofile(
            meta.with_suffix('.sigmf-data'))
        tool = Path(__file__).resolve().parents[1]/'tools/classify_subghz.py'
        result = json.loads(subprocess.check_output([
            sys.executable, str(tool), '--input', str(meta), '--scan', '--ofdm',
        ]))
    events = result['ofdm_candidates']
    assert len(events) == 2, events
    for event, (start, useful, prefix, _) in zip(events, cases):
        assert abs(event['start_seconds']-start) <= .002, event
        assert round(event['useful_symbol_seconds']*rate) == useful, event
        assert round(event['prefix_seconds']*rate) == prefix, event
    print('Two cyclic prefixes detected. Prefix-free multicarrier and CW excluded.')


if __name__ == '__main__':
    main()
