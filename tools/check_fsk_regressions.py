"""核对两个确定性周期 FSK 反例，保存实际二进制与输入哈希。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--fixtures', default='tests/fixtures')
    parser.add_argument('--output', required=True)
    parser.add_argument('--expected-algorithm', required=True)
    args = parser.parse_args()
    report = {'state': 'running', 'binary_sha256': hashlib.sha256(Path(args.exe).read_bytes()).hexdigest(),
              'expected_algorithm': args.expected_algorithm, 'trials': []}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    for name in ('periodic-fsk-v3-false-1', 'periodic-fsk-v3-false-2'):
        fixture = Path(args.fixtures)/name
        raw = (fixture/'capture.cu8').read_bytes()
        provenance = json.loads((fixture/'provenance.json').read_text(encoding='utf-8'))
        digest = hashlib.sha256(raw).hexdigest()
        if digest != provenance['fixture_sha256']:
            raise RuntimeError('反例数据哈希错误: '+name)
        with tempfile.TemporaryDirectory(prefix='signalscope-fsk-regression-') as temporary:
            meta = Path(temporary)/'capture.sigmf-meta'
            meta.write_bytes((fixture/'capture.json').read_bytes())
            meta.with_suffix('.sigmf-data').write_bytes(raw)
            process = subprocess.run([args.exe, 'detect', '--input', str(meta), '--threshold-db', '11', '--workers', '4'],
                                     capture_output=True, encoding='utf-8', check=True)
            result = json.loads(process.stdout)
        events = [e for e in result['events'] if e['evidence_level'] == 'lora_structure_match']
        valid = (result['algorithm'] == args.expected_algorithm and result['source_pairs'] == len(raw)//2
                 and not events and not result.get('results_at_limit', False))
        report['trials'].append({'fixture': name, 'input_sha256': digest, 'algorithm': result['algorithm'],
                                 'source_pairs': result['source_pairs'], 'structure_events': events, 'passed': valid})
    report['state'] = 'passed' if all(row['passed'] for row in report['trials']) else 'failed'
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=True))
    return 0 if report['state'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
