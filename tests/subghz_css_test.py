"""联合扫描的重复扫频、真实 LoRa 及窄带反例回归。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from classify_subghz import apply_line_notches

RATE = 3_000_000

def main():
    # 片尾载波通过陷波器后不应在相距很远的片头产生循环卷积伪影。
    tone = np.zeros(32768, dtype=np.complex64)
    tone[-3000:] = .6*np.exp(2j*np.pi*75000*np.arange(3000)/RATE)
    filtered = apply_line_notches(tone, RATE, [75000])
    assert len(filtered) == len(tone) and np.max(abs(filtered[:512])) < .001
    command = [sys.executable, str(ROOT/'tools/classify_subghz.py'), '--scan', '--lora-exe', sys.argv[1]]
    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'input.sigmf-meta'
        data = meta.with_suffix('.sigmf-data')
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'cu8', 'core:sample_rate': RATE},
            'captures': [{'core:frequency': 478500000, 'core:sample_start': 0}],
        }), encoding='utf-8')
        for kind in ('css', 'ook', 'fsk'):
            directory = Path(temporary)/kind
            subprocess.run([sys.argv[2], '--mode', kind, '--output', str(directory)], check=True)
            waveform = np.fromfile(directory/'waveform.iq', dtype='<i2')
            np.rint(127.5+waveform/256).clip(0, 255).astype('uint8').tofile(data)
            result = json.loads(subprocess.check_output(command+['--input', str(meta)]))
            assert len(result['css_candidates']) == (3 if kind == 'css' else 0), result
            assert not result['lora_events'], result
            assert all(c['bandwidth_hz'] == 500000 and c['sf_equivalent'] == 7
                       and c['repeated_chirps'] >= 6 for c in result['css_candidates']), result

        fixture = ROOT/'tests/fixtures/lora-edge-high-sfd-transition'
        meta.write_bytes((fixture/'capture.json').read_bytes())
        data.write_bytes((fixture/'capture.cu8').read_bytes())
        result = json.loads(subprocess.check_output(command+['--input', str(meta)]))
        assert len(result['lora_events']) == 1 and not result['css_candidates'], result
        original = data.read_bytes()
        diagnostic = json.loads(subprocess.check_output(command+['--input', str(meta), '--lora-notch']))
        assert diagnostic['lora_events'] == result['lora_events']
        assert diagnostic['css_candidates'] == result['css_candidates']
        assert data.read_bytes() == original
        print('Repeated CSS, known LoRa, OOK and 2-FSK verified.')


if __name__ == '__main__':
    main()
