"""离线复核临时候选：搜索常见 LoRa 参数，保留不支持/超时信息，不把能量当识别。"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe',required=True)
parser.add_argument('--input',required=True)
parser.add_argument('--report',required=True)
args=parser.parse_args()
metadata=json.loads(Path(args.input).read_text(encoding='utf-8'))
rate=float(metadata['global']['core:sample_rate'])
probe=metadata['global'].get('signalscope:result',{}).get('probe',{})
widths=[125000,250000,500000,62500,31250,15625,7812.5,32000000/768,32000000/1536,32000000/3072,32000000/384]
models=[]
if probe.get('bandwidth_hz') and probe.get('sf_candidate'):
    slope=float(probe['bandwidth_hz'])**2/2**int(probe['sf_candidate'])
    for width in widths:
        sf=math.log2(width**2/slope)
        if abs(sf-round(sf))<1e-6 and 5<=sf<=12:
            models.append((width,int(round(sf)),int(round(sf))))
for width in widths:
    maximum=min(12,int(math.floor(math.log2(262144*width/rate))))
    if maximum>=5:models.append((width,5,maximum))
results=[]
started=time.monotonic()
for width,low,high in models:
    command=[args.exe,'detect','--input',args.input,'--bw',str(width),'--sf-min',str(low),'--sf-max',str(high),
             '--search-width',str(min(1800000,rate-width)),'--probe-stride','1']
    try:
        process=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=90)
        if process.returncode:
            result={'bw':width,'sf_min':low,'sf_max':high,'error':process.stderr.strip()}
        else:
            output=json.loads(process.stdout)
            matches=[e for e in output['events'] if e['evidence_level']=='lora_structure_match']
            css=[e for e in output['events'] if e['evidence_level']=='css_candidate']
            result={'bw':width,'sf_min':low,'sf_max':high,'matches':matches,'css_count':len(css),
                    'processing_seconds':output.get('processing_seconds')}
    except subprocess.TimeoutExpired:
        result={'bw':width,'sf_min':low,'sf_max':high,'error':'90 秒复核预算耗尽，未完成'}
    results.append(result)
    report={'input':str(Path(args.input).resolve()),'elapsed_seconds':time.monotonic()-started,'results':results}
    Path(args.report).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False),flush=True)
    if result.get('matches'):
        break
