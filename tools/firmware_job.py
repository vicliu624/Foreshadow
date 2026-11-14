"""后台构建/烧录任务：输出日志与明确退出状态，不把观察超时当作失败。"""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project',required=True)
parser.add_argument('--output',required=True)
parser.add_argument('--upload',action='store_true')
args=parser.parse_args()
output=Path(args.output).resolve();output.parent.mkdir(parents=True,exist_ok=True)
command=[sys.executable,'-m','platformio','run','--project-dir',str(Path(args.project).resolve())]
if args.upload:command+=['--target','upload','--upload-port','COM11']
report={'command':command,'helper_pid':os.getpid(),'started_utc':datetime.now(timezone.utc).isoformat(),'state':'running'}
def publish():output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
with output.with_suffix('.log').open('w',encoding='utf-8') as log:
    child=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    report['pid']=child.pid;publish()
    report['returncode']=child.wait()
report.update(state='complete',finished_utc=datetime.now(timezone.utc).isoformat());publish()
