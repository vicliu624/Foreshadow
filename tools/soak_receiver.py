"""只读观察已安装接收器的长稳状态；不发射、不重启、不保留连续 I/Q。"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import time


COUNTERS = ('analysis_dropped_pairs', 'analysis_gaps', 'result_dropped_events', 'result_dropped_groups')


def faults(current, previous, initial):
    errors = []
    if current['identity'] != initial['identity']:
        errors.append('接收进程或安装版本发生变化，连续运行证明中断')
    if current['state'] != 'listening' or current['status_age_seconds'] > 30:
        errors.append('接收器停止或状态超过 30 秒未更新')
    for key in COUNTERS:
        if current[key] != initial[key]:
            errors.append(key + ' 发生变化')
    if (previous and current['status_updated_utc'] != previous['status_updated_utc']
            and current['source_pairs'] <= previous['source_pairs']):
        errors.append('采集样本计数未推进')
    analysis_limit = max(2., current.get('analysis_queue_capacity_seconds', 0)
                         +current.get('analysis_inflight_seconds', 0)+.25)
    if current['analysis_lag_seconds'] > analysis_limit or current['recording_lag_seconds'] > 2:
        errors.append('分析或录制积压超过已声明的有界延迟预算')
    if current['disk_refused'] != initial['disk_refused']:
        errors.append('存储拒绝事件录制')
    if current['event_data_bytes'] > current['quota_bytes'] + current['max_event_bytes']:
        errors.append('事件数据超出配额及一个在写片段的容差')
    if current['disk_free_bytes'] < 256 * 1024 * 1024:
        errors.append('磁盘剩余不足 256 MiB')
    return errors


def observe(home):
    manifest = json.loads((home/'.local/share/signalscope/install-manifest.json').read_text())
    pid = manifest['receiver_pid']
    executable = Path(f'/proc/{pid}/exe')
    if executable.resolve(strict=True) != home/'.local/bin/signalscope-gui':
        raise RuntimeError('接收进程路径不属于安装版')
    stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    status_path = home/'SignalScope-events/monitor-status.json'
    status = json.loads(status_path.read_text())
    storage = status['storage']
    updated = datetime.fromisoformat(status['updated_utc'].replace('Z', '+00:00')).timestamp()
    data_bytes = 0
    files = 0
    for directory in (home/'SignalScope-events').glob('event-*'):
        if directory.is_symlink() or not directory.is_dir():
            continue
        data = directory/'capture.sigmf-data'
        if data.is_file() and not data.is_symlink():
            data_bytes += data.stat().st_size
            files += 1
    return {
        'observed_utc': datetime.now(timezone.utc).isoformat(),
        'identity': [pid, stat[19], manifest['cli_sha256'], manifest['gui_sha256']],
        'state': status['state'], 'status_age_seconds': max(0, time.time()-updated),
        'status_updated_utc': status['updated_utc'],
        'source_pairs': status['source_pairs'], 'processed_pairs': status['processed_pairs'],
        'recording_processed_pairs': status['recording_processed_pairs'],
        **{key: status[key] for key in COUNTERS},
        'analysis_lag_seconds': status['analysis_lag_seconds'],
        'analysis_queue_capacity_seconds': status.get('analysis_queue_capacity_seconds', 0),
        'analysis_inflight_seconds': status.get('analysis_inflight_seconds', 0),
        'analysis_queue_high_water_blocks': status.get('analysis_queue_high_water_blocks', 0),
        'recording_lag_seconds': status['recording_lag_seconds'],
        'rss_bytes': status['rss_bytes'], 'cpu_percent_of_machine': status['cpu_percent_of_machine'],
        'disk_refused': storage['disk_refused'], 'quota_bytes': storage['quota_bytes'],
        'max_event_bytes': storage['max_event_bytes'], 'event_data_bytes': data_bytes,
        'event_files': files, 'disk_free_bytes': shutil.disk_usage(home).free,
    }


def save(path, value):
    temporary = path.with_suffix('.new')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(temporary, path)


def verify_history(directory, required_seconds=86400):
    owner = json.loads((directory/'owner.json').read_text(encoding='utf-8'))
    summary = json.loads((directory/'summary.json').read_text(encoding='utf-8'))
    if owner.get('owner') != 'signalscope-readonly-soak':
        raise ValueError('目录不是 SignalScope 长稳观察记录')
    if summary.get('state') != 'complete':
        return {'state': 'incomplete', 'observer_state': summary.get('state'),
                'elapsed_seconds': summary.get('elapsed_seconds'), 'errors': summary.get('errors', [])}
    errors = []
    elapsed = summary['elapsed_seconds']
    if (not isinstance(elapsed, (int,float)) or not elapsed >= max(required_seconds, summary['requested_seconds'])
            or summary.get('errors')):
        errors.append('摘要时长不足或包含故障')
    rows = []
    for path in sorted(directory.glob('samples-*.jsonl')):
        with path.open(encoding='utf-8') as file:
            rows.extend(json.loads(line) for line in file if line.strip())
    rows.sort(key=lambda row: row['sample_index'])
    if (len(rows)<2 or len(rows)!=summary['samples']
            or any(row['sample_index']!=index for index,row in enumerate(rows))):
        errors.append('日志记录缺失、已轮换或重复')
        return {'state': 'invalid', 'elapsed_seconds': elapsed, 'samples': len(rows), 'errors': errors}
    initial, previous = None, None
    first_utc = last_utc = None
    for row in rows:
        index = row['sample_index']
        current = {key: value for key, value in row.items() if key != 'sample_index'}
        observed = datetime.fromisoformat(current['observed_utc'])
        if initial is None:
            initial, first_utc = current, observed
        if last_utc is not None and not 0 < (observed-last_utc).total_seconds() <= 90:
            errors.append(f'采样 {index}：观察时间不连续')
        errors.extend(f'采样 {index}：{reason}' for reason in faults(current, previous, initial))
        previous, last_utc = current, observed
    if initial != summary['initial'] or previous != summary['latest'] or initial['identity'] != summary['receiver_uptime_identity']:
        errors.append('首尾记录或接收程序身份与摘要不一致')
    span = (last_utc-first_utc).total_seconds()
    if span < required_seconds-2 or abs(span-elapsed)>5:
        errors.append('采样日志的时间跨度不足或与摘要不一致')
    return {'state': 'invalid' if errors else 'verified', 'elapsed_seconds': elapsed,
            'history_span_seconds': span, 'samples': len(rows), 'identity': initial['identity'], 'errors': errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--output')
    mode.add_argument('--verify', type=Path, help='只读复核已完成的观察目录')
    parser.add_argument('--seconds', type=int, default=86460)
    parser.add_argument('--interval', type=int, default=60)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 90000 or not 1 <= args.interval <= 60:
        parser.error('时长须为 1–90000 秒，间隔为 1–60 秒')
    if args.verify is not None:
        try:
            verification = verify_history(args.verify, args.seconds)
        except (OSError, ValueError, KeyError, TypeError) as error:
            verification = {'state': 'invalid', 'errors': [str(error)]}
        print(json.dumps(verification, ensure_ascii=False, indent=2))
        return 0 if verification['state']=='verified' else 1
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    save(output/'owner.json', {'owner': 'signalscope-readonly-soak', 'pid': os.getpid()})
    home = Path.home()
    started = time.monotonic()
    samples = 0
    initial = previous = None
    errors = []
    result = {'state': 'running', 'requested_seconds': args.seconds,
              'started_utc': datetime.now(timezone.utc).isoformat(),
              'limitations': '分钟抽样；不证明 USB 物理零丢样、无虚警或射频灵敏度；不解码载荷'}
    try:
        while True:
            current = observe(home)
            if initial is None:
                initial = current
                for filename, expected in (('signalscope', initial['identity'][2]), ('signalscope-gui', initial['identity'][3])):
                    if hashlib.sha256((home/'.local/bin'/filename).read_bytes()).hexdigest() != expected:
                        raise RuntimeError('安装文件哈希与清单不一致')
            errors = faults(current, previous, initial)
            segment, offset = divmod(samples, 751)
            with (output/f'samples-{segment%2}.jsonl').open('w' if offset == 0 else 'a', encoding='utf-8') as log:
                log.write(json.dumps({'sample_index': samples, **current}, ensure_ascii=False)+'\n')
            samples += 1
            elapsed = time.monotonic()-started
            result.update(elapsed_seconds=elapsed, initial=initial, latest=current, errors=errors,
                          samples=samples, history_limit_samples=1502, receiver_uptime_identity=initial['identity'])
            if errors:
                result['state'] = 'failed'
            elif elapsed >= args.seconds:
                result['state'] = 'complete'
            save(output/'summary.json', result)
            if result['state'] != 'running':
                break
            previous = current
            time.sleep(min(args.interval, max(.01, args.seconds-elapsed)))
    except BaseException as error:
        result.update(state='failed', error=str(error), elapsed_seconds=time.monotonic()-started)
        save(output/'summary.json', result)
        raise
    return 0 if result['state'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
