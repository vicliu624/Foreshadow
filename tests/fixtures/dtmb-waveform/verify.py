"""Run the real native receive chain on independently modulated clean IQ.

This is an explicit offline qualification command, not an app runtime or an
RF transmission tool. Ordinary CTest needs neither Python nor NumPy.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile,time
from generate import generate,shape,ROOT

def cases(selection):
    if selection=='smoke':
        return [(945,22,'c3780'),(420,7,'c3780'),(595,7,'c3780'),(595,7,'c1'),(945,17,'c3780'),(945,3,'c3780')]
    return [(pn,si,'c3780') for pn in (420,595,945) for si in range(3,25)]+[(595,si,'c1') for si in range(5,11)]

def verify(decoder,output,pn,si,body,frames,phase_cycle,keep_iq,superframe_markers=False):
    start=time.monotonic();folder=Path(tempfile.mkdtemp(prefix=f'{pn}-{si}-{body}-',dir=output))
    iq=folder/'capture.cf32';decoded=folder/'decoded.ts'
    samples,transport,info=generate(pn,si,frames,body,phase_cycle,superframe_markers);samples=shape(samples)
    info.update({'sample_rate':15120000,'samples':samples.size,'shaping':'SRRC alpha .05, 2 samples per symbol, 64-symbol span'})
    samples.tofile(iq);del samples,transport
    info.update({'iq_sha256':hashlib.sha256(iq.read_bytes()).hexdigest(),'case_directory':str(folder),
                 'generator_seconds':time.monotonic()-start})
    process=subprocess.run([str(decoder),'--capture',str(iq),'15120000','cf32_le',str(decoded),'538000000','10'],
                           capture_output=True,timeout=120)
    (folder/'receiver.stdout').write_bytes(process.stdout);(folder/'receiver.stderr').write_bytes(process.stderr)
    state=json.loads(process.stdout) if process.stdout else {}
    fixture=(ROOT/'tests/fixtures/television-programs/programs.ts').read_bytes()
    expected=[fixture[i:i+188] for i in range(0,len(fixture),188)]
    raw=decoded.read_bytes() if decoded.exists() else b''
    actual=[raw[i:i+188] for i in range(0,len(raw)-187,188) if (raw[i+1]&31)*256+raw[i+2]!=8191]
    stages=state.get('stages',[]);fec=stages[3].get('facts',{}) if len(stages)>3 else {}
    catalog=state.get('transport',{});mode=state.get('mode',{})
    passed=(process.returncode==0 and actual==expected and mode.get('pn_symbols')==pn and mode.get('system_info')==si
            and mode.get('frame_body')==body and catalog.get('section_crc_errors')==0 and catalog.get('continuity_errors')==0
            and [p['program_id'] for p in catalog.get('programs',[])]==[101,202]
            and fec.get('final_syndrome_weight')=='0' and fec.get('bch_unclean_blocks')=='0')
    info.update({'passed':passed,'exit':process.returncode,'seconds':time.monotonic()-start,'state':state.get('state'),
                 'error':state.get('error_key'),'mode':mode,'exact_media_packets':actual==expected,
                 'media_packets':len(actual),'expected_media_packets':len(expected),'transport_bytes':len(raw),
                 'program_ids':[p['program_id'] for p in catalog.get('programs',[])],
                 'section_crc_errors':catalog.get('section_crc_errors'),'continuity_errors':catalog.get('continuity_errors'),
                 'fec':{key:fec[key] for key in ('converged_codewords','clean_frames','bch_corrected_errors','bch_unclean_blocks','final_syndrome_weight','trailing_fec_llrs') if key in fec},
                 'timing':state.get('symbol_timing')})
    (folder/'proof.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf8')
    if not keep_iq:iq.unlink()  # Only the newly created fixture in our unique case directory.
    return info

def main():
    p=argparse.ArgumentParser();p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cases',choices=('smoke','all'),default='smoke');p.add_argument('--frames',type=int,default=1200)
    p.add_argument('--phase-cycle',action='store_true');p.add_argument('--keep-iq',action='store_true')
    p.add_argument('--superframe-markers',action='store_true');a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);reports=[]
    for pn,si,body in cases(a.cases):
        try:report=verify(a.decoder.resolve(),a.output.resolve(),pn,si,body,a.frames,a.phase_cycle,a.keep_iq,a.superframe_markers)
        except Exception as error:report={'pn':pn,'si':si,'frame_body':body,'passed':False,'exception':repr(error)}
        reports.append(report);print(json.dumps(report),flush=True)
        proof={'scope':'clean synthetic IQ through the actual receiver, TS and programme tables; not desktop/audio-device or on-air acceptance',
               'selection':a.cases,'expected_cases':len(cases(a.cases)),'completed_cases':len(reports),
               'passed':all(row['passed'] for row in reports) and len(reports)==len(cases(a.cases)),
               'tables_sha256':hashlib.sha256(Path(__file__).with_name('gb20600-tables.json').read_bytes()).hexdigest(),'cases':reports}
        (a.output/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8')
    return 0 if all(row['passed'] for row in reports) else 1

if __name__=='__main__':raise SystemExit(main())
