"""在 uConsole 短暂停止在线分析后测吞吐，并保证恢复原接收配置。"""
import json
import argparse
import os
from pathlib import Path
import signal
import subprocess
import time
import threading
import hashlib
import resource
from host_metrics import sample_host_metrics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workers', type=int, choices=(1,2,3,4), default=3)
parser.add_argument('--monitor-seconds', type=int, default=0)
parser.add_argument('--rate', type=int, choices=(1750000,1875000,2000000))
parser.add_argument('--quota-mib', type=int, choices=(64,128,256,1024), default=64)
parser.add_argument('--exe', type=Path, default=Path('build-next/signalscope'))
parser.add_argument('--validation-output', type=Path, help='指定本轮的新事件目录，供独立射频测试脚本读取')
parser.add_argument('--dense-short-symbols', action='store_true', help='验证短符号逐符号探测的实时开销')
parser.add_argument('--fsk', action='store_true', help='开启 FSK 旁路，验证多调制实时接收')
args = parser.parse_args()
if not 0 <= args.monitor_seconds <= 600:
    parser.error('--monitor-seconds 须为 0–600')
benchmark_exe=args.exe.resolve().with_name('signalscope-benchmark-lora')
if not args.monitor_seconds and (not benchmark_exe.is_file() or not os.access(benchmark_exe,os.X_OK)):
    parser.error(f'请先构建并将基准工具放在检测程序同目录：{benchmark_exe}')
base = Path.home()
output = args.validation_output or base/'SignalScope-validation'/('live-'+str(time.time_ns()))
if args.monitor_seconds and output.exists():
    parser.error('实机验证需要新的事件目录')
manifest = base / '.local/share/signalscope/install-manifest.json'
status_path = base / 'SignalScope-events/monitor-status.json'
state = json.loads(manifest.read_text())
status = json.loads(status_path.read_text())
if status['storage']['active']:
    raise RuntimeError('正在录制事件，拒绝打断')
if status['state'] != 'listening':
    raise RuntimeError('接收机状态不是 listening')
pid = state['receiver_pid']
installed = base / '.local/bin/signalscope-gui'
if Path(f'/proc/{pid}/exe').resolve() != installed:
    raise RuntimeError('接收程序 PID 或路径不匹配')
configuration = status['receiver_configuration']
detector = status.get('detector', {})
paused = time.time()
os.kill(pid, signal.SIGTERM)
try:
    for _ in range(150):
        if not Path(f'/proc/{pid}/exe').exists():
            break
        time.sleep(.02)
    else:
        raise RuntimeError('原接收机未退出')
    if args.monitor_seconds:
        tested_binary_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest()
        command = [str(args.exe), 'monitor', '--output', str(output),
                   '--seconds', str(args.monitor_seconds), '--frequency', str(configuration['frequency_hz']),
                   '--rate', str(args.rate or configuration['sample_rate_hz']), '--gain',
                   'auto' if configuration['auto_gain'] else str(configuration['gain_tenths_db']/10),
                   '--threshold-db', '11', '--workers', str(args.workers), '--quota-mib', str(args.quota_mib)]
        if args.dense_short_symbols:
            command.extend(['--dense-short-symbols', '1'])
        if args.fsk:
            command.extend(['--fsk', '1'])
        metrics = []
        metrics_stop = threading.Event()
        def sample_host():
            while not metrics_stop.is_set():
                sample = sample_host_metrics()
                metrics.append(sample)
                if len(metrics) > 160:
                    del metrics[0]
                metrics_stop.wait(1)
        metrics_thread = threading.Thread(target=sample_host, daemon=True)
        metrics_thread.start()
        try:
            with Path('live-validation-result.json').open('w') as log:
                usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
                completed = subprocess.run(command, stdout=log, check=False, timeout=args.monitor_seconds+30)
                usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        finally:
            metrics_stop.set()
            metrics_thread.join(timeout=2)
            Path('live-validation-host-metrics.json').write_text(json.dumps(metrics, indent=2))
        content = Path('live-validation-result.json').read_text()
        result = json.loads(content) if content.strip() else json.loads((output/'monitor-status.json').read_text())
        result['validation_returncode'] = completed.returncode
        result['validation_binary_sha256'] = tested_binary_sha256
        result['validation_executable'] = str(args.exe.resolve())
        result['validation_cpu_seconds'] = (usage_after.ru_utime-usage_before.ru_utime
                                            +usage_after.ru_stime-usage_before.ru_stime)
        Path('live-validation-result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps({'validation_output': str(output), **{key: result.get(key) for key in
              ('state', 'elapsed_seconds', 'analysis_lag_seconds', 'analysis_dropped_pairs',
               'result_dropped_groups', 'source_pairs', 'processed_pairs', 'cpu_percent_of_machine',
               'validation_returncode', 'error', 'source_result')}}), flush=True)
        completed.check_returncode()
    else:
        subprocess.run([str(benchmark_exe), '--exe', str(args.exe),
                        '--input', 'capture.sigmf-meta', '--threshold', '18', '--threshold', '11',
                        '--workers', str(args.workers), '--output', 'threshold-benchmark-isolated.json'], check=True, timeout=90)
finally:
    # 即使基准失败也恢复安装版本，不安装实验二进制。
    if not Path(f'/proc/{pid}/exe').exists():
        env = dict(os.environ, XDG_RUNTIME_DIR='/run/user/1000', WAYLAND_DISPLAY='wayland-0',
                   QT_QPA_PLATFORM='wayland', DBUS_SESSION_BUS_ADDRESS='unix:path=/run/user/1000/bus')
        receiver = subprocess.Popen([str(installed), '--frequency', str(configuration['frequency_hz']/1e6),
                                     '--gain', 'auto' if configuration['auto_gain'] else str(configuration['gain_tenths_db']/10),
                                     '--rate', str(configuration['sample_rate_hz']),
                                     '--workers', str(detector.get('workers', 3)),
                                     '--threshold-db', str(detector.get('preamble_threshold_db', 18)),
                                     '--output', str(base/'SignalScope-events'), '--autostart'],
                                    env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL, start_new_session=True)
        state['receiver_pid'] = receiver.pid
        manifest.write_text(json.dumps(state, indent=2))
        Path('benchmark-receiver-interruption.json').write_text(json.dumps(
            {'paused_unix_seconds': paused, 'restored_unix_seconds': time.time(),
             'receiver_pid': receiver.pid, 'configuration': configuration}, indent=2))
        print('恢复接收进程', receiver.pid, flush=True)
