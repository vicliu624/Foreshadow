"""可控低功率 LoRa 射频测试；只事后关联接收结果，不向检测器提供发射参数。"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import random
import shlex
import subprocess
import time
from pager_tx import PagerTx

def receiver_snapshot(host,root,executable=None):
    code=f'''import json,time
from pathlib import Path
root=Path({root!r})
status=json.loads((root/'monitor-status.json').read_text())
records=[]
for meta in root.glob('event-*/capture.sigmf-meta'):
    item=json.loads(meta.read_text())
    records.append({{'path':str(meta),'name':meta.parent.name,'metadata':item}})
print(json.dumps({{'remote_unix_seconds':time.time(),'status':status,'records':records}}))
'''
    remote_command = shlex.join([executable,'store-snapshot','--input',root]) if executable else 'python3 -'
    request = None if executable else code
    for attempt in range(3):
        started=time.time()
        try:
            result=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5',host,remote_command],input=request,
                                  capture_output=True,text=True,encoding='utf-8',timeout=12,check=True)
            break
        except subprocess.CalledProcessError as error:
            if error.returncode!=255 or attempt==2:
                raise
        except subprocess.TimeoutExpired:
            if attempt==2:
                raise
        time.sleep(.25*(attempt+1))
    ended=time.time();data=json.loads(result.stdout)
    data['clock_offset_seconds']=data['remote_unix_seconds']-(started+ended)/2
    data['clock_uncertainty_seconds']=(ended-started)/2
    return data

def matching_events(record,config,airtime_us,*,earliest_sample=None):
    metadata=record['metadata'];global_meta=metadata['global']
    if global_meta.get('signalscope:status')=='recording':return
    saved=global_meta.get('signalscope:result',{})
    for event in saved.get('events',[]):
        if event.get('evidence_level')!='lora_structure_match':continue
        if event.get('bandwidth_hz')!=config['bandwidth'] or event.get('sf_candidate')!=config['sf'] or event.get('iq_inverted')!=config['inverted']:continue
        error=event.get('center_frequency_hz',0)-config['frequency']
        if abs(error)>max(2500,config['bandwidth']*.02):continue
        rate=float(global_meta['core:sample_rate'])
        symbols=rate*2**config['sf']/config['bandwidth']
        expected_start=event['sfd_start_sample']-(config['preamble']+2)*symbols
        if earliest_sample is not None and expected_start<earliest_sample:
            continue
        expected_end=expected_start+airtime_us*rate/1e6
        head=(expected_start-saved['stream_first_sample'])/rate
        tail=(saved['stream_last_sample_exclusive']-expected_end)/rate
        yield {'record':record['path'],'event':event,'frequency_error_hz':error,'record_end_sample':saved['stream_last_sample_exclusive'],
                'expected_frame_head_margin_seconds':head,'expected_frame_tail_margin_seconds':tail,
                'expected_frame_iq_covered':head>=0 and tail>=0,
                'pretrigger_truncated':saved.get('pretrigger_truncated'),
                'possibly_truncated':saved.get('possibly_truncated'),
                'association':'sample_lower_bound_frequency_bw_sf_polarity_match' if earliest_sample is not None else 'frequency_bw_sf_polarity_match',
                'transmitter_identity_verified_by_payload':False,'frame_crc_verified':False}

def match_record(record,config,airtime_us):
    return next(matching_events(record,config,airtime_us),None)

def ready_receiver(host,root):
    deadline=time.monotonic()+25
    while True:
        snapshot=receiver_snapshot(host,root);status=snapshot['status']
        if status.get('capture_pipeline') not in ('source_owned_ring_v2','independent_capture_writer_v3'):raise RuntimeError('接收端不是独立原始缓冲录制版本，先更新接收软件')
        detection=status.get('detector',{})
        if set(detection.get('bandwidth_candidates_hz',[]))!={125000,250000,500000} or detection.get('sf_min',99)>7 or detection.get('sf_max',0)<12 or not detection.get('normal_iq') or not detection.get('inverted_iq'):
            raise RuntimeError('接收配置不是预定的宽带盲搜索基线，未发射测试信号')
        updated=datetime.fromisoformat(status['updated_utc'].replace('Z','+00:00')).timestamp()
        if snapshot['remote_unix_seconds']-updated>12:raise RuntimeError('接收状态已过期，未发射测试信号')
        if (status.get('state')=='listening' and status.get('analysis_lag_seconds',100)<.2
                and status.get('recording_lag_seconds',0)<.2 and not status.get('storage_status_stale',False)
                and not status.get('storage',{}).get('active',False)):return snapshot
        if time.monotonic()>=deadline:raise RuntimeError('接收端未就绪、分析积压或上一录制未结束，未发射测试信号')
        time.sleep(1)

def cases(quick):
    output=[dict(name='baseline',frequency=478321000,bandwidth=250000,sf=11,cr=5,power=-9,preamble=8,sync=0x12,inverted=False,length=32)]
    if quick:return output
    for width in (125000,250000,500000):
        for sf in range(7,13):
            index=len(output)
            output.append(dict(name=f'bw{width}-sf{sf}',frequency=478150003+index*25013,bandwidth=width,sf=sf,
                               cr=5,power=-9,preamble=8,sync=0x12,inverted=False,length=8))
    for name,width,sf,length,preamble,sync,inverted,frequency in [
        ('longer-payload',125000,9,128,8,0x12,False,478211007),
        ('longest-minimum-sf12',125000,12,8,8,0x12,False,478411009),
        ('inverted',250000,11,32,8,0x12,True,478611011),
        ('public-sync',250000,9,32,8,0x34,False,478711013),
        ('short-preamble',250000,7,32,6,0x12,False,478025017),
        ('long-preamble',250000,11,32,16,0x12,False,478975019)]:
        output.append(dict(name=name,frequency=frequency,bandwidth=width,sf=sf,cr=5,power=-9,preamble=preamble,sync=sync,inverted=inverted,length=length))
    return output

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM11');parser.add_argument('--receiver',default='vicliu@192.168.50.92')
    parser.add_argument('--store',default='/home/vicliu/SignalScope-events');parser.add_argument('--output',required=True)
    parser.add_argument('--quick',action='store_true');parser.add_argument('--only',action='append',default=[])
    parser.add_argument('--edges',action='store_true',help='追加接近搜索两侧边缘的 BW500/SF7 短包')
    parser.add_argument('--center-comparison',action='store_true',help='交错比较中心与两侧各 100 kHz 的 BW500/SF8')
    parser.add_argument('--repeat',type=int,choices=range(1,11),default=1,help='对选中配置重复发射，保留每次结果')
    parser.add_argument('--power',type=int,choices=(-9,-6,-3,0));parser.add_argument('--id-base',type=int,default=int(time.time()*1000)&0xffffffff)
    args=parser.parse_args()
    output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True)
    report={'state':'running','started_utc':datetime.now(timezone.utc).isoformat(),'id_base':args.id_base,'receiver_host':args.receiver,'receiver_store':args.store,'poll_interval_seconds':1,'repeat_count':args.repeat,'cases':[],
            'notes':['接收器始终独立搜索；发射参数只用于事后关联','未解调载荷，不能以参数/时间关联证明发射端身份','其他新 LoRa 事件保留为来源未知']}
    def publish():output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    publish()
    with PagerTx(args.port) as tx:
        report['firmware_status']=tx.status();publish()
        if args.center_comparison:
            available=[dict(name=name,frequency=478500185+offset,bandwidth=500000,sf=8,
                            cr=5,power=-9,preamble=8,sync=0x12,inverted=False,length=8)
                       for name,offset in [('center-low',-100000),('center-near-dc',0),('center-high',100000)]]
        else:
            available=cases(args.quick)
        if args.edges:
            for name,frequency in [('edge-low-bw500',478005003),('edge-high-bw500',478994997)]:
                available.append(dict(name=name,frequency=frequency,bandwidth=500000,sf=7,cr=5,power=-9,
                                      preamble=8,sync=0x12,inverted=False,length=8))
        unknown=set(args.only)-{case['name'] for case in available}
        if unknown:raise ValueError('未知测试项: '+', '.join(sorted(unknown)))
        selected=[case for case in available if not args.only or case['name'] in args.only]
        ordered=[]
        rng=random.Random(args.id_base)
        for repeat in range(args.repeat):
            group=selected.copy()
            if args.center_comparison:
                rng.shuffle(group)
            ordered.extend(dict(case,repeat_index=repeat+1) for case in group)
        selected=ordered
        if not selected:raise ValueError('没有匹配的测试用例')
        for index,case in enumerate(selected):
            if args.power is not None:case['power']=args.power
            try:
                before=ready_receiver(args.receiver,args.store)
            except Exception as error:
                report.update(state='receiver_not_ready',error=str(error),finished_utc=datetime.now(timezone.utc).isoformat())
                publish()
                raise
            if before['status'].get('state')!='listening':raise RuntimeError('接收端未处于实时监听，停止发射测试')
            if args.center_comparison and before['status']['receiver_configuration']['frequency_hz']!=478500000:
                report.update(state='receiver_not_ready',error='中心频率不是 478.5 MHz',
                              finished_utc=datetime.now(timezone.utc).isoformat())
                publish()
                raise RuntimeError(report['error'])
            names={record['name'] for record in before['records']}
            config={key:case[key] for key in ('frequency','bandwidth','sf','cr','power','preamble','sync','inverted')}
            record={'case':case,'receiver_before':before['status'],'clock_offset_seconds':before['clock_offset_seconds'],
                    'clock_uncertainty_seconds':before['clock_uncertainty_seconds']}
            report['cases'].append(record);publish()
            tx.configure(**config)
            try:
                receipt=tx.transmit((args.id_base+index)&0xffffffff,case['length'])
            except Exception as error:
                record.update(error=str(error),partial_transmission=getattr(tx,'last_transmission',{}))
                report.update(state='transmission_error',error=str(error),finished_utc=datetime.now(timezone.utc).isoformat())
                publish()
                raise
            record['tx']=receipt;publish()
            deadline=time.monotonic()+12;matches=[];unknown=[]
            while time.monotonic()<deadline:
                try:
                    after=receiver_snapshot(args.receiver,args.store)
                except (subprocess.SubprocessError, ValueError) as error:
                    record.update(association_result='receiver_observation_failed',error=str(error))
                    report.update(state='receiver_observation_failed',finished_utc=datetime.now(timezone.utc).isoformat())
                    publish()
                    raise
                matches=[];unknown=[]
                for candidate in after['records']:
                    if candidate['name'] in names:continue
                    # 缓冲中的旧事件可能在发射后才落盘；用发射前的采样计数排除。
                    matched=list(matching_events(candidate,config,receipt['started']['airtime_us'],
                                                 earliest_sample=before['status']['source_pairs']))
                    if matched:matches.extend(matched)
                    else:unknown.append(candidate['path'])
                if matches and after['status']['source_pairs']>=max(item['record_end_sample'] for item in matches):break
                time.sleep(1)
            record.update(matches=matches,other_new_records=unknown,receiver_after=after['status'])
            record['association_result']='single_correlated_record' if len(matches)==1 else 'ambiguous_multiple_records' if matches else 'not_detected_or_not_recorded'
            record['analysis_dropped_pairs_delta']=after['status']['analysis_dropped_pairs']-before['status']['analysis_dropped_pairs']
            record['result_dropped_groups_delta']=after['status'].get('result_dropped_groups',0)-before['status'].get('result_dropped_groups',0)
            record['result_dropped_events_delta']=after['status'].get('result_dropped_events',0)-before['status'].get('result_dropped_events',0)
            publish();print(json.dumps({'case':case['name'],'result':record['association_result'],'covered':[m['expected_frame_iq_covered'] for m in matches]},ensure_ascii=False),flush=True)
    report['state']='complete';report['finished_utc']=datetime.now(timezone.utc).isoformat();publish()
if __name__=='__main__':main()
