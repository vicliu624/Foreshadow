"""在 uConsole 原位更新自有程序；保存回退副本，启动失败自动恢复。"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time
import argparse
from deployment_validation import validate_runtime_report

home = Path.home()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir',type=Path,default=home/'signalscope-dev/build-next')
parser.add_argument('--expected-algorithm',choices=('dechirp-fft-structure-v3','dechirp-fft-structure-v3.1'),default='dechirp-fft-structure-v3')
parser.add_argument('--validation-report',type=Path,required=True)
parser.add_argument('--validation-note',required=True)
parser.add_argument('--require-fsk',action='store_true',help='要求实机报告和启动后的界面均运行 FSK 旁路')
args=parser.parse_args()
report_bytes = args.validation_report.read_bytes()
validated_cli_sha256 = hashlib.sha256((args.build_dir/'signalscope').read_bytes()).hexdigest()
report = json.loads(report_bytes)
validate_runtime_report(report, validated_cli_sha256, args.expected_algorithm)
def fsk_ready(status):
    fsk = status.get('fsk_analysis', {})
    return not fsk.get('unavailable', True) and fsk.get('scanned_blocks', 0)>0
if args.require_fsk and not fsk_ready(report):
    raise ValueError('实机报告未验证 FSK 旁路')
prefix = home/'.local'
share = prefix/'share/signalscope'
manifest_path = share/'install-manifest.json'
manifest = json.loads(manifest_path.read_text())
old_pid = manifest['receiver_pid']
gui = prefix/'bin/signalscope-gui'
if Path(f'/proc/{old_pid}/exe').resolve() != gui:
    raise RuntimeError('接收程序 PID 或路径不匹配，拒绝更新')
status_path = home/'SignalScope-events/monitor-status.json'
deadline = time.monotonic()+15
while True:
    status = json.loads(status_path.read_text())
    if not status['storage']['active']:
        break
    if time.monotonic() > deadline:
        raise RuntimeError('当前事件仍在录制，未更新')
    time.sleep(.25)
rx = status['receiver_configuration']
stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+str(time.time_ns())
deployment = share/('deployment-'+stamp)
backup, stage = deployment/'rollback', deployment/'stage'
backup.mkdir(parents=True)
files = [Path('bin/signalscope'), Path('bin/signalscope-gui')]
settings_path = home/'.config/SignalScope/SignalScope.conf'
for relative in files:
    destination = backup/relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    if (prefix/relative).exists():
        shutil.copy2(prefix/relative, destination)
shutil.copy2(manifest_path, backup/'install-manifest.json')
if settings_path.exists():
    shutil.copy2(settings_path, backup/'SignalScope.conf')
(deployment/'owner.json').write_text(json.dumps({'owner':'signalscope-deployment-v1','created_utc':stamp}))
subprocess.run(['cmake','--install',str(args.build_dir),'--component','SignalScope',
                '--prefix',str(stage)],check=True,timeout=60)
for relative in files:
    if not (stage/relative).is_file():
        raise RuntimeError('暂存安装缺少文件: '+str(relative))
if hashlib.sha256((stage/'bin/signalscope').read_bytes()).hexdigest() != validated_cli_sha256:
    raise RuntimeError('暂存命令行程序与实机验证的哈希不一致')
saved_report = deployment/'runtime-validation.json'
saved_report.write_bytes(report_bytes)

env = dict(os.environ, XDG_RUNTIME_DIR='/run/user/1000', WAYLAND_DISPLAY='wayland-0',
           QT_QPA_PLATFORM='wayland', DBUS_SESSION_BUS_ADDRESS='unix:path=/run/user/1000/bus')
base_args = [str(gui),'--frequency',str(rx['frequency_hz']/1e6),'--gain',
             'auto' if rx['auto_gain'] else str(rx['gain_tenths_db']/10),
             '--output',str(home/'SignalScope-events'),'--autostart']

def launch(arguments):
    return subprocess.Popen(arguments,env=env,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,start_new_session=True)

def stop_owned(pid):
    path = Path(f'/proc/{pid}/exe')
    if not path.exists():
        return
    if path.resolve() != gui:
        raise RuntimeError('待停止进程不属于已安装接收程序')
    os.kill(pid,signal.SIGTERM)
    end = time.monotonic()+8
    while Path(f'/proc/{pid}/exe').exists():
        if time.monotonic()>end:
            raise RuntimeError('接收程序未按时退出')
        time.sleep(.05)

def replace_file(source, destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    temporary = destination.with_name(destination.name+'.signalscope-new')
    shutil.copy2(source,temporary)
    os.replace(temporary,destination)

new_process = None
try:
    stop_owned(old_pid)
    for relative in files:
        replace_file(stage/relative,prefix/relative)
    launched_ns = time.time_ns()
    new_process = launch(base_args+['--rate','1750000','--workers','4','--threshold-db','11'])
    end = time.monotonic()+15
    while True:
        if new_process.poll() is not None:
            raise RuntimeError('新界面进程提前退出')
        current = json.loads(status_path.read_text())
        if (status_path.stat().st_mtime_ns>=launched_ns
                and current.get('capture_pipeline')=='independent_capture_writer_v3'
                and current.get('state')=='listening'
                and current.get('receiver_configuration',{}).get('sample_rate_hz')==1750000
                and current.get('detector',{}).get('workers')==4
                and current.get('detector',{}).get('algorithm')==args.expected_algorithm
                and current.get('detector',{}).get('preamble_threshold_db')==11
                and current.get('detector',{}).get('downchirp_threshold_db')==9
                and (not args.require_fsk or fsk_ready(current))):
            break
        if time.monotonic()>end:
            raise RuntimeError('新接收程序未进入预期监听配置')
        time.sleep(.25)
    updated = dict(manifest,receiver_pid=new_process.pid,capture_pipeline='independent_capture_writer_v3',
                   installed_utc=datetime.now(timezone.utc).isoformat(),rollback_directory=str(backup),
                   receiver_profile={'sample_rate_hz':1750000,'workers':4,'threshold_db':11},
                   detector_algorithm=args.expected_algorithm,
                   runtime_validation_report=str(saved_report),
                   validation_note=args.validation_note)
    updated['gui_sha256'] = hashlib.sha256(gui.read_bytes()).hexdigest()
    updated['cli_sha256'] = hashlib.sha256((prefix/'bin/signalscope').read_bytes()).hexdigest()
    updated.pop('plot_sha256',None)
    if args.require_fsk:
        updated['receiver_profile']['fsk_enabled'] = True
    manifest_path.write_text(json.dumps(updated,ensure_ascii=False,indent=2))
    print(json.dumps({'installed':True,'pid':new_process.pid,'rollback':str(backup),'status':current},ensure_ascii=False))
except BaseException:
    if new_process is not None:
        stop_owned(new_process.pid)
    for relative in files:
        if (backup/relative).exists():
            replace_file(backup/relative,prefix/relative)
        else:
            (prefix/relative).unlink(missing_ok=True)
    if (backup/'SignalScope.conf').exists():
        replace_file(backup/'SignalScope.conf',settings_path)
    if not Path(f'/proc/{old_pid}/exe').exists():
        restored = launch(base_args)
        manifest['receiver_pid'] = restored.pid
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2))
    raise
