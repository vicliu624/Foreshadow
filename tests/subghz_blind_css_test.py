"""验证非模板重复扫频与单次扫频、载波和 FSK 的区别。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


def chirp(rate, duration, offset, span):
    time = np.arange(round(rate*duration))/rate
    return .15*np.exp(2j*np.pi*(offset*time+span*(time**2/(2*duration)-time/2)))


def main():
    rate, center = 1_750_000, 478_500_000
    rng = np.random.default_rng(261901)
    time = np.arange(500000)/rate
    iq = .004*(rng.normal(size=len(time))+1j*rng.normal(size=len(time)))
    iq += .05*np.exp(2j*np.pi*340000*time)
    cases = [(0.03, .0017, -200000, 187000), (.12, .0023, 170000, -213000),
             (.165, .00039, -170000, 360000), (.21, .0065, 210000, -201000)]
    for start, duration, offset, span in cases:
        waveform = np.tile(chirp(rate, duration, offset, span), 8)
        first = round(start*rate)
        iq[first:first+len(waveform)] += waveform
    single = chirp(rate, .002, 90000, 150000)
    iq[180000:180000+len(single)] += single
    tones = np.repeat(np.tile([-1, 1], 25), 729)
    iq[90000:90000+len(tones)] += .13*np.exp(2j*np.pi*np.cumsum(-320000+4500*tones)/rate)

    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'sweeps.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': rate},
            'captures': [{'core:frequency': center, 'core:sample_start': 0}],
        }), encoding='utf-8')
        np.rint(127.5+128*np.column_stack((iq.real, iq.imag))).clip(0, 255).astype('uint8').tofile(
            meta.with_suffix('.sigmf-data'))
        tool = Path(__file__).resolve().parents[1]/'tools/classify_subghz.py'
        result = json.loads(subprocess.check_output([
            sys.executable, str(tool), '--input', str(meta), '--scan', '--blind-css',
            '--lora-exe', sys.argv[1],
        ]))
    assert not result['lora_events'] and not result['css_candidates'], result
    events = result['blind_css_candidates']
    assert len(events) == len(cases), events
    for event, (start, duration, offset, span) in zip(events, cases):
        assert event['repeated_chirps'] == 8, event
        assert abs(event['start_seconds']-start) < .0002, event
        assert abs(event['observed_mid_frequency_hz']-center-offset) < 3000, event
        assert abs(event['repeat_interval_seconds']/duration-1) < .05, event
        assert abs(event['sweep_rate_hz_per_second']/(span/duration)-1) < .08, event
    print('Four non-template sweep trains found. Single chirp, CW and FSK excluded.')


if __name__ == '__main__':
    main()
