"""只读备份 ESP Flash；扩大串口缓冲并限制在途窗口，验证芯片返回的 MD5。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
import types
import contextlib
import io
import esptool

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--output',required=True)
parser.add_argument('--size',type=lambda x:int(x,0),default=16*1024*1024)
parser.add_argument('--window',type=int,default=16)
parser.add_argument('--baud',type=int,default=460800)
args=parser.parse_args()
output=Path(args.output)
if output.exists():raise RuntimeError('备份已存在，拒绝覆盖')
if args.size<=0 or args.size>32*1024*1024 or args.size%4096:raise RuntimeError('备份大小无效')
if not 1<=args.window<=64 or args.baud not in (115200,460800,921600):raise RuntimeError('传输配置无效')
device=esptool.detect_chip(args.port,baud=115200)
if hasattr(device._port,'set_buffer_size'):device._port.set_buffer_size(rx_size=1024*1024,tx_size=65536)
device._port.write_timeout=3
device._port.timeout=3
stub=device.run_stub()
if args.baud!=115200:stub.change_baud(args.baud)
stub._port.write_timeout=3;stub._port.timeout=3

def read_one_at_a_time(self,offset,length,progress_fn=None):
    # 使用 Espressif READ_FLASH 协议，sector=4096，在途块数显式受限。
    self.check_command('read flash (bounded window)',self.ESP_CMDS['READ_FLASH'],struct.pack('<IIII',offset,length,4096,args.window))
    data=bytearray()
    while len(data)<length:
        block=self.read()
        if not block or len(block)>4096 or len(data)+len(block)>length:raise RuntimeError('Flash 数据块长度无效')
        data.extend(block)
        self.write(struct.pack('<I',len(data)))
        if len(data)%(64*1024)==0:
            output.with_suffix('.progress.json').write_text(json.dumps({'bytes':len(data),'total':length,'updated_unix_seconds':time.time()}))
        if progress_fn:progress_fn(len(data),length,offset)
    digest=self.read()
    if len(digest)!=16 or digest!=hashlib.md5(data).digest():raise RuntimeError('Flash 备份 MD5 校验失败')
    return bytes(data)

stub.read_flash=types.MethodType(read_one_at_a_time,stub)
try:
    output.parent.mkdir(parents=True,exist_ok=True)
    partial=output.with_suffix('.partial')
    checkpoint=output.with_suffix('.checkpoint.json')
    committed=0;sha=hashlib.sha256();md5=hashlib.md5()
    if partial.exists():
        saved=json.loads(checkpoint.read_text())
        committed=saved['bytes'];data=partial.read_bytes()
        if len(data)!=committed or hashlib.sha256(data).hexdigest()!=saved['sha256']:raise RuntimeError('已有分段备份校验失败')
        sha.update(data);md5.update(data)
    with partial.open('ab') as file:
        while committed<args.size:
            count=min(65536,args.size-committed)
            for attempt in range(4):
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        data=esptool.cmds.read_flash(stub,committed,count,flash_size='16MB',no_progress=True)
                    break
                except (esptool.FatalError,OSError) as exc:
                    if attempt==3:raise
                    print(json.dumps({'retry_offset':committed,'attempt':attempt+1,'error':str(exc)}),flush=True)
                    stub._port.close()
                    device=esptool.detect_chip(args.port,baud=115200)
                    if hasattr(device._port,'set_buffer_size'):device._port.set_buffer_size(rx_size=1024*1024,tx_size=65536)
                    device._port.write_timeout=3;device._port.timeout=3
                    stub=device.run_stub()
                    if args.baud!=115200:stub.change_baud(args.baud)
                    stub._port.write_timeout=3;stub._port.timeout=3
                    stub.read_flash=types.MethodType(read_one_at_a_time,stub)
            file.write(data);file.flush();sha.update(data);md5.update(data);committed+=len(data)
            checkpoint.write_text(json.dumps({'bytes':committed,'sha256':sha.hexdigest(),'updated_unix_seconds':time.time()}))
            if committed%(1024*1024)==0 or committed==args.size:print(json.dumps({'committed_bytes':committed,'total':args.size}),flush=True)
    chip_digest=stub.flash_md5sum(0,args.size)
    if chip_digest.lower()!=md5.hexdigest():raise RuntimeError('分段备份与整片 Flash MD5 不一致，未发布为完整备份')
    partial.rename(output)
    report={'bytes':committed,'sha256':sha.hexdigest(),'chip_md5':chip_digest,'port':args.port,'read_window':args.window,'baud':args.baud,'created_unix_seconds':time.time()}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))
finally:
    stub.hard_reset()
    stub._port.close()
