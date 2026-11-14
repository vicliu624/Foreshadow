"""Run native consumers against a real independent service and mock hardware.

The server sees only the independent test driver. The client intentionally sees
the same hostile plugin path; loading that local driver would violate isolation.
"""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def main():
    server, driver, *command = sys.argv[1:]
    with socket.socket() as address:
        address.bind(('127.0.0.1', 0))
        port = address.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='receiver-service-') as temporary:
        env = os.environ.copy()
        env.update(SOAPY_SDR_ROOT=temporary, SOAPY_SDR_PLUGIN_PATH=driver,
                   FORESHADOW_SDRPLAY_SERVICE=f'tcp://127.0.0.1:{port}', AMC_TEST_REMOTE_SERVICE='1', AMC_TEST_SOAPY_REALTIME='1')
        with (Path(temporary)/'service.log').open('w+b') as log:
            process = subprocess.Popen([server, f'--bind=127.0.0.1:{port}'], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+10
                while True:
                    if process.poll() is not None:
                        raise RuntimeError('Independent test receiver service exited')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.2):
                            break
                    except OSError:
                        if time.monotonic()>deadline:
                            raise RuntimeError('Receiver service did not become ready')
                        time.sleep(.05)
                client = env.copy()
                marker = Path(temporary)/'unexpected-local-driver.txt'
                client['AMC_TEST_LOCAL_DRIVER_MARKER'] = str(marker)
                result = subprocess.run(command, env=client, timeout=150)
                if marker.exists():
                    raise RuntimeError('Client auto-loaded a local hardware driver instead of using the independent service')
                if result.returncode:
                    log.flush();log.seek(0);print(log.read().decode('utf-8',errors='replace')[-6000:])
                return result.returncode
            finally:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()


if __name__ == '__main__':
    raise SystemExit(main())
