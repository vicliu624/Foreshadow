"""离屏短录制重放后测主线程静态刷新开销；不证明实时采集性能。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def ticks(pid):
    fields = Path(f'/proc/{pid}/task/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    return int(fields[11])+int(fields[12])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui', required=True)
    parser.add_argument('--cli', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='signalscope-gui-profile-') as temporary:
        root = Path(temporary)
        subprocess.run([args.cli, 'synth', '--output', str(root/'tone'), '--seconds', '.1'],
                       check=True, capture_output=True)
        raw = root/'tone/capture.sigmf-data'
        original = hashlib.sha256(raw.read_bytes()).hexdigest()
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(root/'config'),
                   OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
        with (root/'gui.log').open('w') as log:
            process = subprocess.Popen([args.gui, '--replay', str(root/'tone/capture.sigmf-meta'),
                '--output', str(root/'events'), '--rate', '1750000', '--workers', '4', '--threshold-db', '11',
                '--autostart', '--smoke-seconds', '30', '--screenshot', str(output.with_suffix('.png')),
                '--screenshot-delay-ms', '20000'], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                time.sleep(10)
                if process.poll() is not None:
                    raise RuntimeError('界面进程提前结束')
                first, started = ticks(process.pid), time.monotonic()
                time.sleep(10)
                last, elapsed = ticks(process.pid), time.monotonic()-started
                process.wait(timeout=40)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
        if process.returncode:
            raise RuntimeError((root/'gui.log').read_text())
        if not output.with_suffix('.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
            raise RuntimeError('没有有效界面截图')
        if hashlib.sha256(raw.read_bytes()).hexdigest() != original or list((root/'events').glob('**/*.sigmf-data')):
            raise RuntimeError('重放修改或重复保存了原始 I/Q')
        result = {'note': __doc__, 'gui_sha256': hashlib.sha256(Path(args.gui).read_bytes()).hexdigest(),
                  'main_thread_cpu_seconds': (last-first)/os.sysconf('SC_CLK_TCK'),
                  'observed_seconds': elapsed, 'screenshot': str(output.with_suffix('.png')),
                  'iq_unchanged': True, 'replay_did_not_record': True, 'returncode': process.returncode}
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(result, ensure_ascii=True))


if __name__ == '__main__':
    main()
