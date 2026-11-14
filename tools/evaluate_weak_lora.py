"""独立噪声种子评估检测率与反例误判；不代表射频灵敏度或现场虚警率。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np


def generate(kind, snr_db, seed, rate=2000000):
    bw, sf = 500000, 7
    m = 1 << sf
    n = rate*m//bw
    rng = np.random.default_rng(seed)
    sigma = .04
    # 带内 SNR；复噪声功率为 2 sigma²，均匀噪声带宽比例 BW/Fs。
    amplitude = np.sqrt(2*sigma**2*bw/rate*10**(snr_db/10))
    if kind in ('tone', 'periodic_fsk'):
        amplitude = .18
    elif kind == 'noise':
        amplitude = 0.
    count = int(rate*(.45 if kind in ('noise', 'tone', 'periodic_fsk') else .05))
    index = np.arange(count)
    offset = int(rng.integers(-450000, 450001))
    first = 2048+int(rng.integers(0, n))
    x = sigma*(rng.normal(size=count)+1j*rng.normal(size=count))
    if kind == 'tone':
        x += .18*np.exp(2j*np.pi*offset*index/rate)
    elif kind == 'periodic_fsk':
        frequency = offset+np.where((index//(n//2)) % 2, bw/4, -bw/4)
        x += .18*np.exp(2j*np.pi*np.cumsum(frequency)/rate)
    elif kind != 'noise':
        position = (index-first)/n
        end = 2.25 if kind != 'wrong_sfd' else (2.125 if seed % 2 else 2.375)
        active = (position >= 0) & (position < 10+end+3)
        local = position[active]
        shift = np.zeros(len(local))
        direction = np.ones(len(local))
        shift[(local >= 8) & (local < 9)] = 3 if kind == 'wrong_sync' else 24
        shift[(local >= 9) & (local < 10)] = 5 if kind == 'wrong_sync' else 32
        down = (local >= 10) & (local < 10+end)
        direction[down] = -1
        payload = local >= 10+end
        symbols = rng.integers(0, m, size=3)
        payload_position = local[payload]-10-end
        shift[payload] = symbols[np.minimum(payload_position.astype(int), 2)]
        local[payload] = payload_position
        fraction = (local+shift/m) % 1
        t = fraction*n/rate
        phase = direction*(-bw*t/2+bw*t*t/(2*n/rate))
        x[active] += amplitude*np.exp(2j*np.pi*(phase+offset*index[active]/rate))
    iq = np.empty((count, 2), dtype=np.uint8)
    iq[:, 0] = np.clip(np.rint(127.5+128*x.real), 0, 255)
    iq[:, 1] = np.clip(np.rint(127.5+128*x.imag), 0, 255)
    return iq.tobytes(), dict(kind=kind, seed=seed,
                              inband_snr_db=snr_db if kind not in ('noise', 'tone', 'periodic_fsk') else None,
                              first_sample=first, frequency_offset_hz=offset,
                              amplitude=float(amplitude), noise_component_sigma=sigma)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--trials', type=int, default=20)
    parser.add_argument('--seed-base', type=int, default=810000)
    parser.add_argument('--rate', type=int, choices=(1750000, 2000000), default=2000000)
    parser.add_argument('--dense-short-symbols', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.trials <= 100:
        parser.error('每组种子数须为 1–100')
    if args.seed_base < 0:
        parser.error('种子起点须为非负数')
    report = {'note': __doc__, 'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'binary_sha256': hashlib.sha256(Path(args.exe).read_bytes()).hexdigest(),
              'sample_rate_hz': args.rate, 'dense_short_symbols': args.dense_short_symbols, 'groups': []}
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='signalscope-weak-evaluation-') as temporary:
        meta = Path(temporary)/'capture.sigmf-meta'
        meta.write_text(json.dumps({'global': {'core:datatype': 'cu8', 'core:sample_rate': args.rate,
                                               'core:version': '1.2.6'},
                                    'captures': [{'core:sample_start': 0, 'core:frequency': 478500000}],
                                    'annotations': []}), encoding='utf-8')
        configurations = [('lora', snr) for snr in (-4, -7, -10)]
        configurations += [(kind, -7) for kind in ('wrong_sync', 'wrong_sfd', 'noise', 'tone', 'periodic_fsk')]
        for group_index, (kind, snr) in enumerate(configurations):
            group = {'kind': kind, 'inband_snr_db': snr if kind not in ('noise', 'tone', 'periodic_fsk') else None,
                     'trials': [], 'detected': 0, 'unexpected': 0}
            report['groups'].append(group)
            for trial in range(args.trials):
                raw, truth = generate(kind, snr, args.seed_base+group_index*1000+trial, args.rate)
                meta.with_suffix('.sigmf-data').write_bytes(raw)
                command = [args.exe, 'detect', '--input', str(meta), '--threshold-db', '11', '--workers', '4']
                if args.dense_short_symbols:
                    command.extend(['--dense-short-symbols', '1'])
                result = subprocess.run(command,
                                        capture_output=True, encoding='utf-8', check=True)
                data = json.loads(result.stdout)
                matches = [e for e in data['events'] if e['evidence_level'] == 'lora_structure_match']
                correct = kind == 'lora' and any(e['sf_candidate'] == 7 and e['bandwidth_hz'] == 500000
                    and abs(e['center_frequency_hz']-478500000-truth['frequency_offset_hz']) < 10000 for e in matches)
                unexpected = any(kind != 'lora' or e['sf_candidate'] != 7 or e['bandwidth_hz'] != 500000
                    or abs(e['center_frequency_hz']-478500000-truth['frequency_offset_hz']) >= 10000 for e in matches)
                group['detected'] += int(correct)
                group['unexpected'] += int(unexpected)
                group['trials'].append({**truth, 'input_sha256': hashlib.sha256(raw).hexdigest(),
                                        'correct': bool(correct), 'unexpected': unexpected,
                                        'matches': matches, 'processing_seconds': data['processing_seconds']})
                destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            print(json.dumps({k: v for k, v in group.items() if k != 'trials'}), flush=True)


if __name__ == '__main__':
    main()
