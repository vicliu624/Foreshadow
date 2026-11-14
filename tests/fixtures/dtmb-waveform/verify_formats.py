"""Offline complete-waveform qualification at the RSP's 10 MS/s input rate.

SciPy resampling and explicit test-file quantization are independent of the
application's sampler and sample-codec implementation. No hardware is used.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
import numpy as np
import scipy
from scipy.signal import resample_poly
from generate import generate,shape,ROOT

def main():
    p=argparse.ArgumentParser();p.add_argument('--decoder',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);start=time.monotonic()
    symbols,transport,info=generate(945,22,1800,'c3780',True,True);del transport
    samples=resample_poly(shape(symbols),125,189);del symbols
    values=np.column_stack((samples.real,samples.imag)).reshape(-1);del samples
    fixture=(ROOT/'tests/fixtures/television-programs/programs.ts').read_bytes();expected=[fixture[i:i+188] for i in range(0,len(fixture),188)]
    reports=[]
    for datatype,scale,offset,minimum,maximum,dtype in (('ci16_le',32768,0,-32768,32767,'<i2'),('cu8',128,127.5,0,255,'u1')):
        path=a.output/(datatype+'.sigmf-data');integer=np.rint(values*scale+offset)
        clipping=int(np.count_nonzero((integer<minimum)|(integer>maximum)))
        np.clip(integer,minimum,maximum).astype(dtype).tofile(path)
        metadata={'global':{'core:version':'1.0.0','core:datatype':datatype,'core:sample_rate':10000000},
                  'captures':[{'core:sample_start':0,'core:frequency':538000000}],'annotations':[]}
        path.with_suffix('.sigmf-meta').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf8')
        output=path.with_suffix('.ts');case_start=time.monotonic()
        result=subprocess.run([str(a.decoder.resolve()),'--capture',str(path.resolve()),'10000000',datatype,str(output.resolve()),'538000000','10'],capture_output=True,timeout=120)
        path.with_suffix('.stdout').write_bytes(result.stdout);path.with_suffix('.stderr').write_bytes(result.stderr)
        state=json.loads(result.stdout);raw=output.read_bytes();actual=[raw[i:i+188] for i in range(0,len(raw)-187,188) if (raw[i+1]&31)*256+raw[i+2]!=8191]
        fec=state['stages'][3]['facts'];catalog=state['transport']
        passed=(result.returncode==0 and actual==expected and catalog['section_crc_errors']==catalog['continuity_errors']==0
                and [p['program_id'] for p in catalog['programs']]==[101,202] and fec.get('final_syndrome_weight')=='0' and fec.get('bch_unclean_blocks')=='0')
        report={'datatype':datatype,'sample_rate':10000000,'input_pairs':len(values)//2,'clipped_components':clipping,'iq_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                'passed':passed,'exit':result.returncode,'seconds':time.monotonic()-case_start,'exact_media_packets':actual==expected,
                'media_packets':len(actual),'section_crc_errors':catalog['section_crc_errors'],'continuity_errors':catalog['continuity_errors'],
                'mode':state['mode'],'timing':state.get('symbol_timing'),'fec':{key:fec.get(key) for key in ('clean_frames','converged_codewords','final_syndrome_weight','bch_unclean_blocks')}}
        reports.append(report);print(json.dumps(report),flush=True)
        proof={'scope':'10 MS/s clean synthetic IQ files with odd/even superframe SI; not an over-the-air receiver or sustained-input result',
               'numpy_version':np.__version__,'scipy_version':scipy.__version__,'resampling_ratio':[125,189],'source':info,
               'seconds':time.monotonic()-start,'cases':reports,'passed':len(reports)==2 and all(row['passed'] for row in reports)}
        (a.output/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8')
    return 0 if all(row['passed'] for row in reports) else 1

if __name__=='__main__':raise SystemExit(main())
