"""对失败射频用例做有限原始采集；结束后恢复正常接收，不使用已知参数辅助盲检测。"""
import argparse
from contextlib import nullcontext
import json
import os
from pathlib import Path
import shlex
import subprocess
import time
from pager_tx import PagerTx
from rf_test_matrix import receiver_snapshot

def remote(host,code):
    p=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5',host,'python3','-'],input=code,capture_output=True,text=True,encoding='utf-8',timeout=15,check=True)
    return p.stdout

def native_pager(executable,port,*arguments):
    result=subprocess.run([str(executable),'--port',port,*arguments],capture_output=True,encoding='utf-8',timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f'Pager 工具退出，返回码 {result.returncode}')
    return json.loads(result.stdout)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',required=True);parser.add_argument('--case',required=True);parser.add_argument('--output',required=True)
    parser.add_argument('--receiver',default='vicliu@192.168.50.92');parser.add_argument('--port',default='COM11')
    parser.add_argument('--power',type=int,choices=(-9,-6,-3,0))
    parser.add_argument('--rate',type=int,choices=(1750000,1875000,2000000))
    parser.add_argument('--monitor-exe',help='用目标机上的候选 CLI 同时运行实时检测和原始对照录制')
    parser.add_argument('--replay-exe',type=Path,help='在本机重检采集的原始对照文件')
    parser.add_argument('--pager-exe',type=Path,help='用 C++ Pager 工具执行本次诊断发射')
    parser.add_argument('--snapshot-exe',help='目标机上支持 store-snapshot 的 CLI 路径')
    args=parser.parse_args()
    if args.pager_exe is not None:
        args.pager_exe=args.pager_exe.absolute()
        if not args.pager_exe.is_file() or not os.access(args.pager_exe,os.X_OK):
            parser.error(f'无法执行 C++ Pager 工具：{args.pager_exe}')
    compare_exe=None
    if args.replay_exe is not None and (not args.monitor_exe or not args.replay_exe.is_file()):
        parser.error('--replay-exe 需要现有本机程序，并与 --monitor-exe 合用')
    if args.replay_exe is not None:
        args.replay_exe=args.replay_exe.absolute()
        suffix='.exe' if os.name=='nt' else ''
        compare_exe=args.replay_exe.with_name('signalscope-compare-lora-audit'+suffix)
        if not compare_exe.is_file() or not os.access(compare_exe,os.X_OK):
            parser.error(f'请先构建并将对照程序放在离线检测程序同目录：{compare_exe}')
    report=json.loads(Path(args.report).read_text(encoding='utf-8'))
    case=next(item['case'] for item in report['cases'] if item['case']['name']==args.case)
    if args.power is not None:case['power']=args.power
    config={key:case[key] for key in ('frequency','bandwidth','sf','cr','power','preamble','sync','inverted')}
    if args.pager_exe is not None:
        status=native_pager(args.pager_exe,args.port)
        if not status.get('radio_ready') or status.get('transmitting'):
            raise RuntimeError('Pager 未就绪或正在发射')
    before=receiver_snapshot(args.receiver,'/home/vicliu/SignalScope-events',args.snapshot_exe)
    if before['status']['state']!='listening' or before['status'].get('storage',{}).get('active'):
        raise RuntimeError('正式接收机未就绪或正在保存事件')
    rx=before['status']['receiver_configuration']
    detector=before['status']['detector']
    capture_rate=args.rate or rx['sample_rate_hz']
    local=Path(args.output).resolve();local.mkdir(parents=True,exist_ok=False)
    temporary='/tmp/signalscope-rf-diag-'+str(time.time_ns())
    capture=None;pause_attempted=False;failure=None;result={'case':case,'receiver_configuration':rx,'capture_rate_hz':capture_rate,'source':'controlled_tx_raw_capture_for_diagnosis','created_unix_seconds':time.time(),'monitor_executable':args.monitor_exe}
    with (nullcontext() if args.pager_exe is not None else PagerTx(args.port)) as tx:
        if tx is not None:tx.configure(**config)
        try:
            code=f'''from pathlib import Path
import os,json,signal,time,shutil
for old in Path('/tmp').glob('signalscope-rf-diag-*'):
    marker=old/'owner.json'
    if old.is_symlink() or not marker.is_file():continue
    value=json.loads(marker.read_text())
    if value.get('owner')=='signalscope-rf-diagnostic' and value.get('expires_unix_seconds',1e30)<time.time():shutil.rmtree(old)
manifest=Path('/home/vicliu/.local/share/signalscope/install-manifest.json')
pid=json.loads(manifest.read_text())['receiver_pid']
exe=Path(f'/proc/{{pid}}/exe')
if exe.resolve()!=Path('/home/vicliu/.local/bin/signalscope-gui'):raise RuntimeError('接收程序 PID 不匹配')
path=Path({temporary!r});path.mkdir()
(path/'owner.json').write_text(json.dumps({{'owner':'signalscope-rf-diagnostic','expires_unix_seconds':time.time()+120}}))
os.kill(pid,signal.SIGTERM)
for _ in range(150):
    if not exe.exists():break
    time.sleep(.02)
else:raise RuntimeError('接收程序未退出')
print('receiver paused for 3-second raw test')
'''
            pause_attempted=True
            remote(args.receiver,code)
            if args.monitor_exe:
                command=[args.monitor_exe,'monitor','--output',temporary+'/events',
                         '--audit-iq-output',temporary+'/capture','--workers',str(detector['workers']),
                         '--threshold-db',str(detector['preamble_threshold_db']),'--quota-mib','64']
            else:
                command=['/home/vicliu/.local/bin/signalscope','capture','--output',temporary+'/capture']
            command+=['--seconds','3','--frequency',str(rx['frequency_hz']),'--rate',str(capture_rate),
                      '--gain','auto' if rx['auto_gain'] else str(rx['gain_tenths_db']/10)]
            with (local/'capture.log').open('w',encoding='utf-8') as log:
                # Start the budget before SSH launch. Reserve one second of firmware airtime plus 200 ms.
                send_before_ms=time.time_ns()//1_000_000+1800
                capture=subprocess.Popen(['ssh',args.receiver,shlex.join(command)],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                for _ in range(20):
                    if capture.poll() is not None:raise RuntimeError('原始采集提前退出，见 capture.log')
                    size=int(remote(args.receiver,f"from pathlib import Path\np=Path({temporary+'/capture/capture.sigmf-data'!r})\nprint(p.stat().st_size if p.exists() else 0)"))
                    if size>=262144:break
                    time.sleep(.05)
                else:raise RuntimeError('未等到真实采样数据，未发射')
                identifier=int(time.time()*1000)&0xffffffff
                if args.pager_exe is not None:
                    if capture.poll() is not None or time.time_ns()//1_000_000>=send_before_ms:
                        raise RuntimeError('采集窗口已不足，未发射')
                    tx_args=['--send',str(identifier),'--length',str(case['length']),
                             '--send-before-unix-ms',str(send_before_ms)]
                    for key in ('frequency','bandwidth','sf','cr','power','preamble','sync'):
                        tx_args.extend(('--bw' if key=='bandwidth' else '--'+key,str(config[key])))
                    if config['inverted']:tx_args.append('--inverted')
                    result['tx']=native_pager(args.pager_exe,args.port,*tx_args)
                else:
                    result['tx']=tx.transmit(identifier,case['length'])
                code=capture.wait(timeout=15);result['capture_returncode']=code
                if code:raise RuntimeError('采集失败，见 capture.log')
        except BaseException as error:
            failure=error;result['error']=str(error);result['partial_transmission']=getattr(tx,'last_transmission',{})
        finally:
            if capture and capture.poll() is None:
                try:capture.wait(timeout=6)
                except subprocess.TimeoutExpired:capture.terminate();capture.wait(timeout=3)
            if capture:result['capture_returncode']=capture.returncode
            code=f'''from pathlib import Path
import json,os,subprocess,time
manifest=Path('/home/vicliu/.local/share/signalscope/install-manifest.json')
state=json.loads(manifest.read_text())
installed=Path('/home/vicliu/.local/bin/signalscope-gui')
current=Path(f"/proc/{{state['receiver_pid']}}/exe")
if current.exists():
    if current.resolve()!=installed:raise RuntimeError('清单 PID 不属于接收程序')
    print(state['receiver_pid'])
    raise SystemExit(0)
env=dict(os.environ,XDG_RUNTIME_DIR='/run/user/1000',WAYLAND_DISPLAY='wayland-0',QT_QPA_PLATFORM='wayland',DBUS_SESSION_BUS_ADDRESS='unix:path=/run/user/1000/bus')
p=subprocess.Popen(['/home/vicliu/.local/bin/signalscope-gui','--frequency',str({rx['frequency_hz']}/1e6),'--gain',{('auto' if rx['auto_gain'] else str(rx['gain_tenths_db']/10))!r},'--rate',str({rx['sample_rate_hz']}),'--workers',str({detector['workers']}),'--threshold-db',str({detector['preamble_threshold_db']}),'--output','/home/vicliu/SignalScope-events','--autostart'],env=env,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
state['receiver_pid']=p.pid
temp=manifest.with_suffix('.tmp')
temp.write_text(json.dumps(state,indent=2))
temp.replace(manifest)
print(p.pid)
'''
            if pause_attempted:
                try:
                    result['restored_receiver_pid']=int(remote(args.receiver,code).strip())
                except Exception as error:
                    result['restore_error']=str(error)
                    failure=failure or error
            (local/'trial.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    try:
        subprocess.run(['scp','-r',args.receiver+':'+temporary+'/capture',str(local)],check=True)
        if args.monitor_exe:
            subprocess.run(['scp',args.receiver+':'+temporary+'/events/monitor-status.json',str(local)],check=True)
    except subprocess.CalledProcessError:
        if not failure:raise
    cleanup=f'''from pathlib import Path
import json,shutil
path=Path({temporary!r})
if path.resolve()!=path or not str(path).startswith('/tmp/signalscope-rf-diag-') or path.is_symlink():raise RuntimeError('非法清理路径')
if json.loads((path/'owner.json').read_text())['owner']!='signalscope-rf-diagnostic':raise RuntimeError('目录标识不匹配')
shutil.rmtree(path)
'''
    remote(args.receiver,cleanup)
    if failure:raise RuntimeError('诊断发射、采集或恢复失败；详情见 trial.json') from failure
    if compare_exe is not None:
        subprocess.run([str(compare_exe),'--input',str(local/'capture/capture.sigmf-meta'),
                        '--live-status',str(local/'monitor-status.json'),'--exe',str(args.replay_exe),
                        '--output',str(local/'comparison.json')],check=True,timeout=250)
    print(json.dumps({'output':str(local),'case':case['name'],'capture_returncode':result['capture_returncode'],'restored_receiver_pid':result['restored_receiver_pid']},ensure_ascii=False))
if __name__=='__main__':main()
