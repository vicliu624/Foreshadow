"""Pager 低功率 LoRa 发射控制；只有 send 子命令会发射，每次都是有限包数。"""
import argparse
from datetime import datetime,timezone
import json
import time
import serial

def expected_payload(identifier,length):
    if not 0<=identifier<=0xffffffff or not 8<=length<=240:raise ValueError('载荷 ID 或长度无效')
    result=bytearray(b'AMC1'+identifier.to_bytes(4,'little'))
    random=identifier^0x9e3779b9
    for _ in range(8,length):
        random^=(random<<13)&0xffffffff
        random^=random>>17
        random^=(random<<5)&0xffffffff
        random&=0xffffffff;result.append(random&255)
    return bytes(result)

def fnv1a(data):
    result=2166136261
    for byte in data:result=((result^byte)*16777619)&0xffffffff
    return result

class PagerTx:
    def __init__(self,port='COM11'):
        self.port=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=2)
        self.port.rts=False;self.port.dtr=True;self.port.port=port;self.port.open()
        self.buffer=bytearray()
        try:
            deadline=time.monotonic()+8
            while True:
                self.port.write(b'PING\n')
                try:status=self.read_kind('status',.75)
                except TimeoutError:
                    if time.monotonic()<deadline:continue
                    raise
                if status.get('kind')=='status':break
                if time.monotonic()>=deadline:raise RuntimeError('设备未完成串口握手')
            if status.get('firmware') not in ('amc-pager-tx-0.1','amc-pager-tx-0.2','amc-pager-tx-0.3'):raise RuntimeError('设备不是 AMC 可控发射固件，拒绝继续')
            self.initial_status=status
        except BaseException:
            self.port.close();raise
    def __enter__(self):return self
    def __exit__(self,*args):
        try:self.stop()
        except (serial.SerialException,TimeoutError):pass
        self.port.close()
    def read_kind(self,kind,timeout,identifier=None):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if b'\n' not in self.buffer:
                fragment=self.port.readline()
                if not fragment:continue
                self.buffer.extend(fragment)
                if len(self.buffer)>4096:raise RuntimeError('设备回复超出限制')
            if b'\n' not in self.buffer:continue
            line,_,remaining=self.buffer.partition(b'\n');self.buffer=bytearray(remaining)
            try:record=json.loads(line)
            except (json.JSONDecodeError,UnicodeDecodeError):continue
            if record.get('ok') is False:return record
            if record.get('kind') in ((kind,) if isinstance(kind,str) else kind) and (identifier is None or record.get('id')==identifier):return record
        raise TimeoutError('串口等待回复超时: '+kind)
    def status(self):
        # STATUS 无副作用；丢失回复时重试查询，不重发 TX。
        for attempt in range(3):
            self.port.write(b'STATUS\n')
            try:return self.read_kind('status',3)
            except TimeoutError:
                if attempt==2:raise
    def configure(self,frequency=478875000,bandwidth=250000,sf=11,cr=5,power=-9,preamble=8,sync=0x12,inverted=False):
        if not 470000000<=frequency<=510000000 or bandwidth not in (125000,250000,500000) or not 7<=sf<=12 or not 5<=cr<=8 or not -9<=power<=0 or not 6<=preamble<=64 or not 0<=sync<=255:
            raise ValueError('不支持的发射配置')
        command=f'SET {frequency} {bandwidth} {sf} {cr} {power} {preamble} {sync} {int(inverted)}\n'
        expected={'frequency_hz':frequency,'bandwidth_hz':bandwidth,'sf':sf,'cr':cr,'power_dbm':power,'preamble':preamble,'sync':sync,'inverted':bool(inverted)}
        for attempt in range(3):
            self.port.write(command.encode('ascii'))
            try:result=self.read_kind('status',2)
            except TimeoutError:
                self.port.write(b'STATUS\n')
                try:result=self.read_kind('status',2)
                except TimeoutError:
                    if attempt<2:continue
                    raise
            if not result.get('ok') or not result.get('radio_ready'):raise RuntimeError('无线电配置失败: '+json.dumps(result))
            if all(result.get(key)==value for key,value in expected.items()):return result
        raise RuntimeError('配置回读与请求不一致，未发射')
    def configure_gfsk(self, frequency):
        if not 470000000 <= frequency <= 510000000:
            raise ValueError('GFSK 测试频点超出固件范围')
        self.port.write(f'GFSK {frequency}\n'.encode('ascii'))
        mode = self.read_kind('mode', 3)
        if mode.get('modulation') != 'gfsk':
            raise RuntimeError('Pager 未进入 GFSK 模式')
        try:
            result = self.read_kind('status', 3)
        except TimeoutError:
            result = self.status()
        if not result.get('radio_ready') or result.get('frequency_hz') != frequency or result.get('power_dbm') != -9:
            raise RuntimeError('GFSK 配置回读不一致')
        return {**result, 'modulation': 'gfsk', 'symbol_rate': 2400, 'deviation_hz': 4800}

    def transmit(self,identifier,length=32):
        payload=expected_payload(identifier,length)
        before=self.status();self.last_transmission={'before':before}
        while True:
            requested=time.time();self.port.write(f'TX {identifier} {length}\n'.encode('ascii'))
            try:started=self.read_kind('tx_started',3,identifier)
            except TimeoutError:
                recovered=self.status()
                if recovered.get('last_started_id')!=identifier or recovered.get('started_count',0)<=before.get('started_count',0):raise
                started={'ok':True,'kind':'tx_started_status_recovery','id':identifier,'length':recovered['last_length'],
                         'airtime_us':recovered['last_airtime_us'],'payload_fnv1a32':recovered['last_payload_hash'],
                         'start_us':recovered['last_start_us'],'receipt_source':'status_recovery'}
            if started.get('error')=='cooldown':
                delay=started.get('remaining_ms',0)
                if not 0<=delay<=10000:raise RuntimeError('冷却时间异常')
                time.sleep(delay/1000+.02);continue
            break
        if not started.get('ok'):raise RuntimeError('未发射: '+json.dumps(started))
        self.last_transmission.update(started=started,host_request_unix_seconds=requested)
        if started.get('payload_fnv1a32')!=fnv1a(payload):raise RuntimeError('发射固件载荷生成与主机预期不一致')
        try:done=self.read_kind('tx_done',3,identifier)
        except TimeoutError:
            recovered=self.status()
            if recovered.get('last_completed_id')!=identifier or recovered.get('completed_count',0)<=before.get('completed_count',0) or recovered.get('last_result')!=0:raise
            done={'ok':True,'kind':'tx_done_status_recovery','id':identifier,'elapsed_us':recovered['last_elapsed_us'],
                  'code':0,'receipt_source':'status_recovery'}
        self.last_transmission['done']=done
        if not done.get('ok'):raise RuntimeError('发射未完成: '+json.dumps(done))
        return {'host_request_unix_seconds':requested,'host_done_unix_seconds':time.time(),
                'host_request_utc':datetime.fromtimestamp(requested,timezone.utc).isoformat(),
                'started':started,'done':done,'payload_hex':payload.hex(),
                'note':'TX_DONE 为芯片状态；未代表接收端已解调这些字节'}
    def stop(self):
        self.port.write(b'STOP\n')
        return self.read_kind(('idle','aborted'),2)

    def transmit_amplitude(self,frequency,mode):
        if mode not in ('OOK','ASK') or not 470000000<=frequency<=510000000:
            raise ValueError('无效的调幅测试配置')
        command=f'AMP {frequency} {mode}\n'.encode('ascii')
        for attempt in range(2):
            self.port.write(command)
            result=self.read_kind('amp_done',3)
            if result.get('error')=='cooldown' and attempt==0:
                time.sleep(2.05)
                continue
            if (result.get('kind')!='amp_done' or result.get('modulation')!=mode
                    or result.get('frequency_hz')!=frequency):
                raise RuntimeError('调幅测试未确认完成: '+json.dumps(result))
            return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM11')
    subs=parser.add_subparsers(dest='command',required=True)
    subs.add_parser('status');subs.add_parser('stop')
    send=subs.add_parser('send')
    send.add_argument('--frequency',type=int,default=478875000);send.add_argument('--bw',type=int,default=250000)
    send.add_argument('--sf',type=int,default=11);send.add_argument('--cr',type=int,default=5)
    send.add_argument('--power',type=int,default=-9);send.add_argument('--preamble',type=int,default=8)
    send.add_argument('--sync',type=lambda x:int(x,0),default=0x12);send.add_argument('--inverted',action='store_true')
    send.add_argument('--length',type=int,default=32);send.add_argument('--id',type=int,default=1)
    gfsk=subs.add_parser('send-gfsk')
    gfsk.add_argument('--frequency',type=int,default=478625000)
    gfsk.add_argument('--length',type=int,default=32)
    gfsk.add_argument('--id',type=int,default=1)
    amp=subs.add_parser('send-amp')
    amp.add_argument('--mode',choices=('OOK','ASK'),required=True)
    amp.add_argument('--frequency',type=int,default=478725000)
    args=parser.parse_args()
    with PagerTx(args.port) as device:
        if args.command=='status':result=device.status()
        elif args.command=='stop':result=device.stop()
        elif args.command=='send-amp':
            result=device.transmit_amplitude(args.frequency,args.mode)
        elif args.command=='send-gfsk':
            configured=device.configure_gfsk(args.frequency)
            result={'configuration':configured,'transmission':device.transmit(args.id,args.length)}
        else:
            configured=device.configure(args.frequency,args.bw,args.sf,args.cr,args.power,args.preamble,args.sync,args.inverted)
            result={'configuration':configured,'transmission':device.transmit(args.id,args.length)}
        print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
