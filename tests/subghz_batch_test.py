"""回归长录制分窗和跨窗突发归属。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


def main():
    rate, center = 1_750_000, 433_920_000
    rng = np.random.default_rng(261100)
    count = 3*rate
    iq = .003*(rng.normal(size=count)+1j*rng.normal(size=count))
    symbols = np.repeat(np.r_[np.tile([1, 0], 8), rng.integers(0, 2, 64)], round(rate/2400))
    carrier = .18*symbols*np.exp(2j*np.pi*97000/rate*np.arange(len(symbols)))
    starts = (.986, 1.53, 1.988, 2.48)
    for start in starts:
        first = round(start*rate)
        iq[first:first+len(carrier)] += carrier
    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'long.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': rate},
            'captures': [{'core:frequency': center, 'core:sample_start': 0}],
        }), encoding='utf-8')
        np.rint(127.5+128*np.column_stack((iq.real, iq.imag))).clip(0, 255).astype('uint8').tofile(
            meta.with_suffix('.sigmf-data'))
        tool = Path(__file__).resolve().parents[1]/'tools/classify_subghz.py'
        output = subprocess.check_output([sys.executable, str(tool), '--input', str(meta), '--scan-all'])
    rows = [json.loads(line) for line in output.splitlines()]
    assert [(row['report_start_seconds'], row['report_end_seconds']) for row in rows] == [(0,1), (1,2), (2,3)]
    matched = [
        event for row in rows for channel in row['channels']
        if abs(channel['channel_hz']-center-97000)<2000
        for event in channel['events'] if event['modulation_candidate']=='OOK'
    ]
    assert len(matched) == len(starts), matched
    for start, event in zip(starts, matched):
        assert abs(event['start_seconds']-start) < .004, event
        assert event['end_seconds'] > start+.025, event
    print('Four bursts found once each, including the two crossing window boundaries.')


if __name__ == '__main__':
    main()
