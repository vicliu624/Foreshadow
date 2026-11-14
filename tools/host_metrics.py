"""读取可用的 Linux 主机指标；可选 PSI 缺失不影响内存、磁盘等指标。"""
from pathlib import Path
import time


def sample_host_metrics(proc_root=Path('/proc')):
    sample = {'unix_seconds': time.time()}
    for name, relative in [('cpu_ticks', 'stat'), ('io_pressure', 'pressure/io'),
                           ('memory', 'meminfo'), ('disks', 'diskstats')]:
        try:
            content = (proc_root / relative).read_text()
            if name == 'cpu_ticks':
                content = content.splitlines()[0]
            elif name == 'memory':
                content = [line for line in content.splitlines()
                           if line.split(':')[0] in ('Dirty', 'Writeback', 'MemAvailable')]
            elif name == 'disks':
                content = [line for line in content.splitlines()
                           if len(line.split()) >= 3 and line.split()[2].startswith(('mmcblk', 'sd'))]
            sample[name] = content
        except (OSError, ValueError, IndexError) as error:
            sample[name + '_error'] = str(error)
    return sample
