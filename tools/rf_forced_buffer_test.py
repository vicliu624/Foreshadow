"""在有限压力接收器中发两帧，第二帧须完整落在分析暂停窗口；不验证载荷。"""
import argparse
import json
from pathlib import Path
import time
from pager_tx import PagerTx
from rf_test_matrix import ready_receiver, receiver_snapshot, match_record
from rf_diagnostic_capture import remote


def read_remote(host, path):
    return json.loads(remote(host, f"import json\nfrom pathlib import Path\np=Path({path!r})\nprint(p.read_text() if p.exists() else 'null')\n"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receiver', default='vicliu@192.168.50.92')
    parser.add_argument('--port', default='COM11')
    parser.add_argument('--store', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--first-sf', type=int, choices=(7, 8, 9), default=7)
    parser.add_argument('--second-sf', type=int, choices=(8, 9, 10), default=10)
    args = parser.parse_args()
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    report = {'state': 'running', 'note': __doc__, 'store': args.store, 'transmissions': []}
    def publish():
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    publish()
    try:
        before = ready_receiver(args.receiver, args.store)
        owner = read_remote(args.receiver, args.store+'/forced-validation-owner.json')
        if not owner or owner.get('owner') != 'signalscope-live-buffer-test-v1':
            raise RuntimeError('不是有限硬件压力接收器，未发射')
        if read_remote(args.receiver, args.store+'/analysis-pause.json'):
            raise RuntimeError('控制发射前已触发暂停，保留环境录制，本次不算受控验证')
        report['receiver_before'] = before['status']
        report['clock_offset_seconds'] = before['clock_offset_seconds']
        report['clock_uncertainty_seconds'] = before['clock_uncertainty_seconds']
        configs = [
            dict(frequency=478211007, bandwidth=125000, sf=args.first_sf, cr=5, power=-9, preamble=8, sync=0x12, inverted=False),
            dict(frequency=478611011, bandwidth=250000, sf=args.second_sf, cr=5, power=-9, preamble=8, sync=0x34, inverted=True),
        ]
        rx = before['status']['receiver_configuration']
        if rx['frequency_hz'] != 478500000 or rx['sample_rate_hz'] != 1750000:
            raise RuntimeError('当前验证窗口不是 478–479 MHz / 1.75 MS/s，未发射')
        with PagerTx(args.port) as tx:
            base_id = int(time.time()*1000) & 0xffffffff
            tx.configure(**configs[0])
            receipt = tx.transmit(base_id, 8)
            report['transmissions'].append({'configuration': configs[0], 'receipt': receipt})
            publish()
            deadline = time.monotonic()+12
            while True:
                pause = read_remote(args.receiver, args.store+'/analysis-pause.json')
                if pause and pause['state'] == 'paused':
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError('第一帧后未等到分析暂停，未发送第二帧')
                time.sleep(.1)
            report['pause_seen'] = pause
            tx.configure(**configs[1])
            receipt = tx.transmit((base_id+1) & 0xffffffff, 8)
            report['transmissions'].append({'configuration': configs[1], 'receipt': receipt})
            publish()
        deadline = time.monotonic()+75
        while True:
            result = read_remote(args.receiver, args.store+'/forced-result.json')
            if result is not None:
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('有限接收器未发布最终压力结果')
            time.sleep(1)
        snapshot = receiver_snapshot(args.receiver, args.store)
        pause = result['forced_pause']
        offset, uncertainty = report['clock_offset_seconds'], report['clock_uncertainty_seconds']
        second = report['transmissions'][1]['receipt']
        timing_safe = (second['host_request_unix_seconds']+offset-uncertainty >= pause['begin_unix_seconds']
                       and second['host_done_unix_seconds']+offset+uncertainty <= pause['end_unix_seconds'])
        report.update(receiver_result=result, pause=pause, second_transmission_within_pause=timing_safe)
        for transmission in report['transmissions']:
            matches = []
            for record in snapshot['records']:
                match = match_record(record, transmission['configuration'], transmission['receipt']['started']['airtime_us'])
                if match:
                    matches.append(match)
            transmission['matches'] = matches
        passed = (result.get('forced_buffer_validation_passed') and timing_safe and
                  all(len(t['matches']) == 1 and t['matches'][0]['expected_frame_iq_covered'] for t in report['transmissions']))
        report['state'] = 'passed' if passed else 'failed'
        publish()
        print(json.dumps({'state': report['state'], 'timing_safe': timing_safe,
                          'analysis_dropped_pairs': result['analysis_dropped_pairs'],
                          'analysis_queue_high_water_blocks': result['analysis_queue_high_water_blocks']}, ensure_ascii=True))
        return 0 if passed else 1
    except BaseException as error:
        report.update(state='failed', error=str(error))
        publish()
        raise


if __name__ == '__main__':
    raise SystemExit(main())
