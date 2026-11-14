"""用两个实收 Pager 波形验证分频带重检和结果去重。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
RATE = 1_750_000
CENTER = 478_500_000
sys.path.insert(0, str(ROOT/'tools'))
from classify_subghz import Channelizer


def extract_waveform(executable, root, name):
    fixture = ROOT/'tests/fixtures'/name
    meta = root/(name+'.sigmf-meta')
    meta.write_bytes((fixture/'capture.json').read_bytes())
    raw = (fixture/'capture.cu8').read_bytes()
    meta.with_suffix('.sigmf-data').write_bytes(raw)
    result = json.loads(subprocess.check_output([
        executable, 'detect', '--input', str(meta), '--threshold-db', '11',
    ]))
    event, = [e for e in result['events'] if e['evidence_level']=='lora_structure_match']
    components = np.frombuffer(raw, dtype='uint8').astype(float).reshape(-1, 2)
    iq = (components[:, 0]-127.5+1j*(components[:, 1]-127.5))/128
    first = event['sfd_start_sample']-6000
    wave = iq[first:first+22000]
    assert first >= 0 and len(wave) == 22000
    offset = event['center_frequency_hz']-CENTER
    return wave*np.exp(-2j*np.pi*offset*np.arange(len(wave))/RATE)/10**(event['matched_component_dbfs']/20)


def main():
    executable = sys.argv[1]
    rng = np.random.default_rng(2611062)
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        names = ('lora-edge-low-fine-sync', 'lora-edge-high-sfd-transition')
        waves = [extract_waveform(executable, root, name) for name in names]
        bandlimited = [Channelizer(wave, RATE, 560000).extract(0)[0] for wave in waves]
        meta = root/'mix.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': RATE},
            'captures': [{'core:frequency': CENTER, 'core:sample_start': 0}],
        }), encoding='utf-8')
        cases = [(waves, (-325000, 325000), delay, 0) for delay in (0, 224, 896)]
        cases += [(bandlimited, (-420000, 260000), 224, -9), (bandlimited, (-270000, 415000), 896, 9)]
        for sources, offsets, delay, relative_db in cases:
            iq = .002*(rng.normal(size=48000)+1j*rng.normal(size=48000))
            starts = (7000, 7000+delay)
            gains = (.055, .055*10**(relative_db/20))
            for wave, first, offset, gain in zip(sources, starts, offsets, gains):
                shifted = wave*np.exp(2j*np.pi*offset*np.arange(len(wave))/RATE)
                iq[first:first+len(wave)] += gain*shifted
            np.rint(127.5+128*np.column_stack((iq.real, iq.imag))).clip(0,255).astype('uint8').tofile(
                meta.with_suffix('.sigmf-data'))
            result = json.loads(subprocess.check_output([
                sys.executable, str(ROOT/'tools/classify_subghz.py'), '--input', str(meta),
                '--scan', '--lora-exe', executable, '--lora-split',
            ]))
            events = result['lora_events']+result['lora_split_events']
            assert len(events) == 2 and result['lora_split_events'], result
            for first, offset in zip(starts, offsets):
                matched = [e for e in events if e['sf']==7 and e['bandwidth_hz']==500000
                           and not e['iq_inverted']
                           and abs(e['center_frequency_hz']-CENTER-offset)<8000
                           and abs(e['sfd_start_seconds']*RATE-first-6000)<28]
                assert len(matched) == 1, events
    print('Separated Pager structures found once each in equal-level and band-limited near-far cases.')


if __name__ == '__main__':
    main()
