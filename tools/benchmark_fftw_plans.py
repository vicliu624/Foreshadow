"""独立测量 FFTW 计划；不连接射频硬件，不用于声称检测实时性能。"""
import ctypes as c
import ctypes.util
import json
from pathlib import Path
import time
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sizes', type=int, nargs='+', default=[512,1024,2048,4096,8192,16384,32768,65536])
parser.add_argument('--output', default='fftw-plan-benchmark.json')
args = parser.parse_args()

library = c.CDLL(ctypes.util.find_library('fftw3f'))
library.fftwf_malloc.argtypes = [c.c_size_t]
library.fftwf_malloc.restype = c.c_void_p
library.fftwf_free.argtypes = [c.c_void_p]
library.fftwf_plan_dft_1d.argtypes = [c.c_int, c.c_void_p, c.c_void_p, c.c_int, c.c_uint]
library.fftwf_plan_dft_1d.restype = c.c_void_p
library.fftwf_execute.argtypes = [c.c_void_p]
library.fftwf_destroy_plan.argtypes = [c.c_void_p]
library.fftwf_set_timelimit.argtypes = [c.c_double]
rows = []
for n in args.sizes:
    if not 16 <= n <= 262144:
        raise ValueError('FFT 点数超出检测器限制')
    source, destination = library.fftwf_malloc(n*8), library.fftwf_malloc(n*8)
    try:
        for name, flag in [('estimate', 64), ('measure', 0)]:
            library.fftwf_set_timelimit(.1)
            started = time.monotonic()
            plan = library.fftwf_plan_dft_1d(n, source, destination, -1, flag)
            if not plan:
                raise RuntimeError('FFTW plan failed')
            planning = time.monotonic()-started
            c.memset(source, 0, n*8)
            started = time.monotonic()
            for _ in range(200):
                library.fftwf_execute(plan)
            row = {'n': n, 'mode': name, 'plan_seconds': planning,
                   'execute_seconds_per_fft': (time.monotonic()-started)/200}
            rows.append(row)
            library.fftwf_destroy_plan(plan)
    finally:
        library.fftwf_free(source)
        library.fftwf_free(destination)
Path(args.output).write_text(json.dumps(rows, indent=2))
print(json.dumps(rows))
