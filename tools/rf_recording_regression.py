"""对保存的受控射频样本做盲检回归；发射参数只用于事后评分。"""
import argparse
import json
import pathlib
import subprocess
import time
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--root', default='build/rf-tests/diagnostics')
    parser.add_argument('--output', required=True)
    parser.add_argument('--threshold', type=float, action='append', required=True)
    parser.add_argument('--repeat', type=int, choices=(1, 2), default=1,
                        help='重复录制，验证大量早期探针不会屏蔽后半段结构匹配')
    parser.add_argument('--workers', type=int, choices=(1,2,3,4), default=3)
    args = parser.parse_args()
    cases = [
        ('sf7-bw125-raw-retry', 478175016, 125000, 7),
        ('public-sync-raw', 478711013, 250000, 9),
        ('sf7-bw500-raw', 478475172, 500000, 7),
        ('sf7-bw500-raw-power0', 478475172, 500000, 7),
    ]
    report = {'note': '接收器未传入发射频率、BW、SF；匹配不代表载荷或身份验证。', 'trials': []}
    for name, frequency, bandwidth, sf in cases:
        source = pathlib.Path(args.root) / name / 'capture/capture.sigmf-meta'
        if not source.exists():
            raise FileNotFoundError(source)
        for threshold in args.threshold:
            started = time.monotonic()
            with tempfile.TemporaryDirectory(prefix='signalscope-rf-regression-') as temporary:
                original_pairs = source.with_suffix('.sigmf-data').stat().st_size // 2
                actual = source
                if args.repeat == 2:
                    actual = pathlib.Path(temporary) / 'capture.sigmf-meta'
                    original_meta = json.loads(source.read_text(encoding='utf-8'))
                    clean_meta = {'global': {key: original_meta['global'][key]
                                            for key in ('core:datatype', 'core:sample_rate', 'core:version')},
                                  'captures': [{'core:sample_start': 0,
                                                'core:frequency': original_meta['captures'][0]['core:frequency']}],
                                  'annotations': []}
                    actual.write_text(json.dumps(clean_meta), encoding='utf-8')
                    raw = source.with_suffix('.sigmf-data').read_bytes()
                    actual.with_suffix('.sigmf-data').write_bytes(raw * 2)
                result = subprocess.run(
                    [args.exe, 'detect', '--input', str(actual), '--threshold-db', str(threshold), '--workers', str(args.workers)],
                    capture_output=True, encoding='utf-8', check=True)
            decoded = json.loads(result.stdout)
            matches = [e for e in decoded['events'] if e['evidence_level'] == 'lora_structure_match']
            associated = [e for e in matches if e['bandwidth_hz'] == bandwidth
                          and e['sf_candidate'] == sf
                          and abs(e['center_frequency_hz'] - frequency) < 10000]
            trial = {'case': name, 'threshold_db': threshold,
                     'workers': args.workers,
                     'repeats': args.repeat,
                     'matched_each_repeat': [any(i*original_pairs <= e['first_observed_sample'] < (i+1)*original_pairs
                                                 for e in associated) for i in range(args.repeat)],
                     'elapsed_seconds': time.monotonic() - started,
                     'associated_structure_matches': associated,
                     'other_structure_matches': [e for e in matches if e not in associated]}
            report['trials'].append(trial)
            destination = pathlib.Path(args.output)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            print(name, threshold, 'matched', len(associated), 'other', len(matches)-len(associated), flush=True)
    if any(not all(trial['matched_each_repeat']) for trial in report['trials']):
        raise SystemExit('存在未匹配的射频片段；完整结果已保存')


if __name__ == '__main__':
    main()
