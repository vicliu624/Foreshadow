"""Independent service death/restart and sustained native client acceptance."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def main():
    server, driver, cli, client_modules = sys.argv[1:]
    with socket.socket() as address:
        address.bind(('127.0.0.1', 0));port=address.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='receiver-lifecycle-') as temporary:
        root=Path(temporary)
        env=os.environ.copy()
        env.update(SOAPY_SDR_ROOT=temporary,SOAPY_SDR_PLUGIN_PATH=driver,AMC_TEST_SOAPY_REALTIME='1',
                   FORESHADOW_SDRPLAY_SERVICE=f'tcp://127.0.0.1:{port}',SIGNALSCOPE_DISTRIBUTION_ROOT=str(root/'state'),
                   SIGNALSCOPE_RECEIVER_PLUGIN_PATH=client_modules)
        client=env.copy();marker=root/'local-driver-loaded.txt';client['AMC_TEST_LOCAL_DRIVER_MARKER']=str(marker)
        log=(root/'service.log').open('w+b');service=None
        def start():
            p=subprocess.Popen([server,f'--bind=127.0.0.1:{port}'],env=env,stdout=log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+8
            while True:
                if p.poll() is not None:raise RuntimeError('Service exited during startup')
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=.1):return p
                except OSError:
                    if time.monotonic()>deadline:raise RuntimeError('Service startup timed out')
                    time.sleep(.03)
        def stop(p):
            p.terminate()
            try:p.wait(timeout=3)
            except subprocess.TimeoutExpired:p.kill();p.wait()
        def command(args,expected=0):
            r=subprocess.run([cli,*args,'--receiver','sdrplay'],env=client,capture_output=True,text=True,timeout=12)
            if r.returncode!=expected:raise RuntimeError(f'{args}: {r.returncode}: {r.stderr[-2000:]}')
            return json.loads(r.stdout) if not expected else None
        try:
            service=start();capture=root/'中断采集 📡'
            with (root/'capture.out').open('wb') as out,(root/'capture.err').open('wb') as err:
                p=subprocess.Popen([cli,'capture','--receiver','sdrplay','--seconds','15','--settle','0','--output',str(capture)],env=client,stdout=out,stderr=err)
                deadline=time.monotonic()+8
                while not (capture/'capture.sigmf-data').exists() or (capture/'capture.sigmf-data').stat().st_size<65536:
                    if p.poll() is not None:raise RuntimeError('Capture stopped before the deliberate service failure')
                    if time.monotonic()>deadline:raise RuntimeError('Capture made no progress')
                    time.sleep(.02)
                stop(service);service=None;started=time.monotonic()
                code=p.wait(timeout=10)
            meta=json.loads((capture/'capture.sigmf-meta').read_text(encoding='utf-8'))['global']
            assert code!=0 and meta['signalscope:status']=='error',meta
            assert meta['signalscope:sample_count']>0 and (capture/'capture.sigmf-data').stat().st_size==meta['signalscope:sample_count']*4,meta
            assert command(['devices'])['reason_code']=='service_unavailable'
            command(['devices','--settings','{"service_endpoint":"tcp://127.0.0.1:0"}'],1)
            service=start()
            assert command(['probe'])['hardware']=='RSPdx-R2'
            resumed=root/'重启后采集 📡';started=time.monotonic()
            result=command(['capture','--seconds','2','--settle','0','--output',str(resumed)])
            elapsed=time.monotonic()-started
            assert result['status']=='complete' and result['accepted_pairs']==4_000_000 and elapsed>=1.8,result
            assert (resumed/'capture.sigmf-data').stat().st_size==16_000_000
            assert not marker.exists(),'Client loaded the local hardware module'
            print(json.dumps({'service_failure':'accepted prefix preserved','restart':'passed','sustained_pairs':result['accepted_pairs'],
                              'sustained_seconds':elapsed,'local_driver_loaded':False}))
            return 0
        finally:
            if service is not None:stop(service)
            log.close()


if __name__=='__main__':
    raise SystemExit(main())
