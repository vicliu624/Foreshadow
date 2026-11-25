"""Exercise native remote resource ownership through a real service and TCP faults."""
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlparse


def main():
    executable = sys.argv[1]
    upstream = os.environ['FORESHADOW_SDRPLAY_SERVICE']
    construction = subprocess.run([executable, 'construction', upstream], timeout=35)
    address = urlparse(upstream)
    pairs = []
    lock = threading.Lock()
    stopped = threading.Event()
    threads = []

    def close_pair(pair):
        for connection in pair:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()

    def forward(pair):
        try:
            while not stopped.is_set():
                readable, _, _ = select.select(pair, [], [], .1)
                for source in readable:
                    data = source.recv(65536)
                    if not data:
                        return
                    pair[1 if source is pair[0] else 0].sendall(data)
        except (OSError, ValueError):
            pass
        finally:
            close_pair(pair)

    with tempfile.TemporaryDirectory(prefix='remote-resource-') as temporary, socket.socket() as listener:
        control = Path(temporary)
        listener.bind(('127.0.0.1', 0))
        listener.listen()
        listener.settimeout(.05)
        endpoint = 'tcp://127.0.0.1:' + str(listener.getsockname()[1])

        def accept():
            while not stopped.is_set():
                try:
                    client, _ = listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    return
                try:
                    server = socket.create_connection((address.hostname, address.port), timeout=3)
                    server.settimeout(None)
                except OSError:
                    client.close()
                    stopped.set()
                    return
                pair = (client, server)
                with lock:
                    pairs.append(pair)
                    count = len(pairs)
                staged = control/'accepted.tmp'
                staged.write_text(str(count))
                staged.replace(control/'accepted')
                worker = threading.Thread(target=forward, args=(pair,), daemon=True)
                threads.append(worker)
                worker.start()

        acceptor = threading.Thread(target=accept, daemon=True)
        acceptor.start()
        process = None
        try:
            process = subprocess.Popen([executable, 'reconnect', endpoint, str(control)])
            deadline = time.monotonic()+40
            dropped = 0
            while process.poll() is None:
                try:
                    requested = int((control/'drop').read_text())
                except (FileNotFoundError, ValueError):
                    requested = 0
                if requested > dropped:
                    with lock:
                        # This phase has one command connection followed by log
                        # connections. Break only the latest logging channel.
                        assert len(pairs) >= 2
                        pair = pairs[-1]
                    close_pair(pair)
                    dropped = requested
                if time.monotonic() > deadline:
                    raise RuntimeError('Remote resource acceptance timed out')
                time.sleep(.01)
            if process.returncode:
                raise subprocess.CalledProcessError(process.returncode, process.args)
            if construction.returncode:
                raise subprocess.CalledProcessError(construction.returncode, construction.args)
            print(json.dumps({'remote_log_disconnects': dropped, 'status': 'passed'}))
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            stopped.set()
            listener.close()
            acceptor.join(timeout=4)
            with lock:
                for pair in pairs:
                    close_pair(pair)
            for worker in threads:
                worker.join(timeout=2)


if __name__ == '__main__':
    main()
