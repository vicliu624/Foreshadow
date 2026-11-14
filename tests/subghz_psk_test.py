"""含载波偏移及平滑相位过渡的 BPSK/QPSK 宽带回归。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


def main():
    rate, center = 1_750_000, 433_920_000
    rng = np.random.default_rng(261722)
    iq = .004*(rng.normal(size=350000)+1j*rng.normal(size=350000))
    cases = [(2, -120000, 2400, 'BPSK'), (4, 220000, 4800, 'QPSK')]
    for order, offset, baud, _ in cases:
        period = rate//baud
        symbols = np.exp(2j*np.pi*rng.integers(0, order, 96)/order)
        index = np.arange(len(symbols)*period)
        transition = np.minimum((index%period)/(period*.20), 1)
        raised = (1-np.cos(np.pi*transition))/2
        waveform = np.repeat(symbols, period)*raised
        waveform += np.repeat(np.r_[symbols[0], symbols[:-1]], period)*(1-raised)
        waveform *= .13*np.exp(2j*np.pi*offset*index/rate)
        for start in (50000, 155000, 260000):
            iq[start:start+len(waveform)] += waveform
    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'psk.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': rate},
            'captures': [{'core:frequency': center, 'core:sample_start': 0}],
        }), encoding='utf-8')
        np.rint(127.5+128*np.column_stack((iq.real, iq.imag))).clip(0, 255).astype('uint8').tofile(
            meta.with_suffix('.sigmf-data'))
        script = Path(__file__).resolve().parents[1]/'tools/classify_subghz.py'
        result = json.loads(subprocess.check_output([sys.executable, str(script), '--input', str(meta), '--scan', '--ofdm']))
        assert not result['ofdm_candidates'], result
        assert len(result['channels']) == len(cases), result
        for channel, (_, offset, _, label) in zip(result['channels'], cases):
            assert abs(channel['channel_hz']-center-offset) < 2000, channel
            assert [event['modulation_candidate'] for event in channel['events']] == [label]*3, channel
            for event in channel['events']:
                assert abs(channel['channel_hz']+event['carrier_offset_hz']-center-offset) < 150, event
    print('BPSK and QPSK identified at separate frequencies with overlapping bursts.')


if __name__ == '__main__':
    main()
