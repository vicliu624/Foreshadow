"""Independent offline DTMB test transmitter; never transmits to hardware.

NumPy is an offline fixture-generation dependency, never an app dependency.
"""
from pathlib import Path
import argparse,hashlib,json,math,time
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
TABLES=json.loads(Path(__file__).with_name('gb20600-tables.json').read_text(encoding='utf8'))

def bitstring(hexadecimal,count):
    return np.fromiter(map(int,bin(int(hexadecimal,16))[2:].zfill(count)),np.uint8,count=count)

def generator(rate):
    rows=TABLES['ldpc_generator_first_rows'][str((24,36,48)[rate-1]*127)]
    # Explicit polynomial-to-serial coordinate convention, independently
    # checked against Appendix B in dtmb_fec_finite_tail (all basis blocks).
    return np.array([[bitstring(value,127)[::-1] for value in row] for row in rows],np.uint8)

def encode_ldpc(message,g):
    # Circular binary convolution of information blocks and the standard's
    # generator first rows. Double FFT rounds bounded integer sums (< 6096).
    # Independent exact bitwise oracle verifies a codeword below.
    spectrum=np.fft.rfft(message.reshape(-1,g.shape[0],127),axis=-1)
    gs=np.fft.rfft(g,axis=-1)
    parity=np.fft.irfft(np.einsum('nif,ipf->npf',spectrum,gs),n=127,axis=-1)
    assert np.max(np.abs(parity-np.rint(parity)))<1e-8
    parity=(np.rint(parity).astype(np.int32)&1).astype(np.uint8)
    codeword=np.concatenate((parity.reshape(message.shape[0],-1),message),axis=1)
    # Exact polynomial XOR of cyclically shifted G rows, no FFT or H involved.
    for block in range(g.shape[1]):
        expected=0
        for i in range(g.shape[0]):
            row=int(''.join(map(str,g[i,block])),2)
            for shift in np.flatnonzero(message[0,i*127:(i+1)*127]):
                s=int(shift);expected^=((row>>s)|(row<<(127-s)))&((1<<127)-1)
        assert np.array_equal(bitstring(hex(expected)[2:],127),parity[0,block])
    return codeword[:,5:].reshape(-1)

def bch(bits):
    words=bits.reshape(-1,752);remainder=np.zeros(len(words),np.uint16)
    for i in range(752):
        remainder=(remainder<<1)|words[:,i]
        remainder^=np.where(remainder&1024,1033,0).astype(np.uint16)
    for i in range(10):
        remainder<<=1;remainder^=np.where(remainder&1024,1033,0).astype(np.uint16)
    parity=((remainder[:,None]>>np.arange(9,-1,-1))&1).astype(np.uint8)
    return np.concatenate((words,parity),axis=1)

def scramble(bits,reset):
    # Standard figure 2, D1..D15, initial physical register 100101010000000.
    registers=[int(x) for x in '100101010000000'];sequence=[]
    for _ in range(reset):
        bit=registers[13]^registers[14];sequence.append(bit);registers=[bit]+registers[:-1]
    return bits^np.resize(np.array(sequence,np.uint8),bits.size)

def interleave(values,step):
    # Standard figure 7: branch b delays b*M branch samples.
    shaped=values.reshape(-1,52);output=np.zeros_like(shaped)
    for branch in range(52):
        delay=branch*step
        if not delay:output[:,branch]=shaped[:,branch]
        elif delay<len(shaped):output[delay:,branch]=shaped[:-delay,branch]
    return output.reshape(-1)

def map_symbols(bits,bps):
    labels=(bits.reshape(-1,bps)*(1<<np.arange(bps))).sum(axis=1)
    if bps==5:
        # Figure 4: cross constellation; rows are Q descending, I ascending.
        # Explicit numeric labels transcribed from Figure 4 (b4...b0).
        points={
            0:(-1.5,-1.5),1:(-7.5,-4.5),2:(1.5,-1.5),3:(7.5,-4.5),
            4:(-4.5,-1.5),5:(-7.5,-1.5),6:(4.5,-1.5),7:(7.5,-1.5),
            8:(-1.5,1.5),9:(-7.5,4.5),10:(1.5,1.5),11:(7.5,4.5),
            12:(-4.5,1.5),13:(-7.5,1.5),14:(4.5,1.5),15:(7.5,1.5),
            16:(-1.5,-4.5),17:(-1.5,-7.5),18:(1.5,-4.5),19:(1.5,-7.5),
            20:(-4.5,-4.5),21:(-4.5,-7.5),22:(4.5,-4.5),23:(4.5,-7.5),
            24:(-1.5,4.5),25:(-1.5,7.5),26:(1.5,4.5),27:(1.5,7.5),
            28:(-4.5,4.5),29:(-4.5,7.5),30:(4.5,4.5),31:(4.5,7.5)}
        constellation=np.array([complex(*points[i]) for i in range(32)])/math.sqrt(45)
    else:
        axis={2:[-4.5,4.5],4:[-6,-2,2,6],6:[-7,-5,-3,-1,1,3,5,7]}[bps]
        # Figure 3/5/6 axis labels are binary reflected Gray.
        values={i^(i>>1):x for i,x in enumerate(axis)};width=bps//2;mask=(1<<width)-1
        constellation=np.array([complex(values[i&mask],values[i>>width]) for i in range(1<<bps)])
        constellation/=math.sqrt(np.mean(np.abs(constellation)**2))
    return constellation[labels].astype(np.complex64)

def generate(pn,si,frames,body,phase_cycle=False,superframe_markers=False):
    if pn not in (420,595,945) or si not in range(3,25) or body not in ('c1','c3780'):
        raise ValueError('Unsupported DTMB fixture configuration')
    if body=='c1' and (pn!=595 or not 5<=si<=10):
        raise ValueError('This receiver currently declares C1 support only for PN595/4QAM')
    if not 1200<=frames<=100000:
        raise ValueError('Use 1200..100000 captured frames, including interleaver fill and the complete media fixture')
    if si<=4:rate=3;bps=1;group=2;cws=1
    elif si<=10:rate=(si-5)//2+1;bps=2;group=1;cws=1
    elif si<=16:rate=(si-11)//2+1;bps=4;group=1;cws=2
    elif si<=18:rate=3;bps=5;group=2;cws=5
    else:rate=(si-19)//2+1;bps=6;group=1;cws=3
    captured=frames//group*group;latency=170 if si%2 else 510
    warm=latency+32;frames=captured+warm;groups=frames//group;cwbytes=(4,6,8)[rate-1]*94
    framebytes=cwbytes*cws//group
    null=bytes([0x47,0x1f,0xff,0x10])+bytes([0xff])*184
    transport=bytearray(null*(groups*cws*cwbytes//188))
    fixture=(ROOT/'tests/fixtures/television-programs/programs.ts').read_bytes()
    start=(warm+64)*framebytes;assert start+len(fixture)<=len(transport)
    transport[start:start+len(fixture)]=fixture
    data=np.unpackbits(np.frombuffer(transport,np.uint8))
    coded=encode_ldpc(bch(scramble(data,framebytes*8)).reshape(-1,(24,36,48)[rate-1]*127),generator(rate))
    step=240 if si%2 else 720
    if bps==1:
        interleaved=interleave(coded,step)
        labels=np.packbits(interleaved.reshape(-1,8),axis=1).reshape(-1)
        nr=np.array([int(value,16) for value in TABLES['nr_hex']],np.uint16)[labels]
        expansion=((nr[:,None]>>np.arange(15,-1,-1))&1).astype(np.uint8).reshape(-1)
        symbols=map_symbols(expansion,2)
    else:symbols=interleave(map_symbols(coded,bps),step)
    symbols=symbols.reshape(frames,3744)
    prefix='0000' if body=='c1' else '1111'
    # SI uses the same power-normalized diagonal 4QAM constellation.
    info=np.array([(-1 if bit=='0' else 1)*complex(1,1)/math.sqrt(2) for bit in prefix+TABLES['system_info_bits'][si-1]])
    blocks=np.concatenate((np.tile(info,(frames,1)),symbols),axis=1)
    if superframe_markers:
        per_superframe={420:225,595:216,945:200}[pn]
        for frame in range(0,frames,per_superframe):
            marker=(frame//per_superframe)%2
            blocks[frame,:36]=np.array([(-1 if bit=='0' else 1)*complex(1,1)/math.sqrt(2)
                for bit in prefix+TABLES['system_info_bits'][marker]])
    if body=='c3780':blocks=(np.fft.ifft(blocks[:,TABLES['frequency_output_to_input']],axis=1)*math.sqrt(3780)).astype(np.complex64)
    if pn==595:
        # Figure 12: D1..D10=0000000001; output D10 before the first clock,
        # feedback D10 xor D3, insert at D1. First chips: hex 80249A6B.
        registers=[0]*9+[1];chips=[]
        for _ in range(595):
            chips.append(registers[-1]);bit=registers[9]^registers[2];registers=[bit]+registers[:-1]
        header=(1-2*np.array(chips,float))*complex(1,1)/math.sqrt(2)
        headers=np.tile(header,(frames,1))
    else:
        vectors=TABLES['pn_headers_hex'][str(pn)]
        headers=np.array([(1-2*bitstring(vectors[i%len(vectors) if phase_cycle else 0],len(vectors[0])*4)[:pn].astype(float))*complex(1,1) for i in range(frames)])
    # The receiver starts while the transmitter's convolutional interleaver
    # is already primed, as in an actual broadcast. No zero-filled startup
    # constellation is mislabeled as a steady on-air signal.
    samples=np.concatenate((headers,blocks),axis=1)[warm:].reshape(-1).astype('<c8')*.1
    return samples,bytes(transport),{'pn':pn,'si':si,'frame_body':body,'frames':captured,'tx_warmup_frames':warm,'rate':rate,'frame_payload_bytes':framebytes,
                                   'interleaver_total_delay_frames':170 if step==240 else 510,'phase_cycle':phase_cycle,'superframe_markers':superframe_markers,
                                   'sample_rate':7560000,'shaping':'ideal symbol-spaced, pre-SRRC diagnostic only',
                                   'samples':samples.size,'payload_sha256':hashlib.sha256(transport).hexdigest()}

def shape(samples):
    # Independent textbook SRRC transmitter (alpha=.05, 64-symbol span).
    # No receiver resampler/kernel is imported or used to make the expected IQ.
    t=np.arange(-64,65,dtype=np.float64)/2;alpha=.05
    with np.errstate(divide='ignore',invalid='ignore'):
        taps=(np.sin(np.pi*t*(1-alpha))+4*alpha*t*np.cos(np.pi*t*(1+alpha)))/(np.pi*t*(1-(4*alpha*t)**2))
    taps[t==0]=1+alpha*(4/np.pi-1)
    taps[np.abs(t)==1/(4*alpha)]=alpha/np.sqrt(2)*((1+2/np.pi)*np.sin(np.pi/(4*alpha))+(1-2/np.pi)*np.cos(np.pi/(4*alpha)))
    up=np.zeros(samples.size*2,np.complex64);up[::2]=samples
    return np.convolve(up,taps.astype(np.float32),mode='same').astype('<c8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--pn',type=int,default=945);p.add_argument('--si',type=int,default=22)
    p.add_argument('--frames',type=int,default=1600);p.add_argument('--body',default='c3780');p.add_argument('--phase-cycle',action='store_true')
    p.add_argument('--symbol-spaced',action='store_true',help='Diagnostic pre-SRRC symbols; unsuitable as a full RF acceptance input')
    p.add_argument('--superframe-markers',action='store_true',help='Alternate normative odd/even first-superframe SI vectors')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();start=time.monotonic()
    samples,transport,info=generate(a.pn,a.si,a.frames,a.body,a.phase_cycle,a.superframe_markers)
    if not a.symbol_spaced:
        samples=shape(samples);info.update({'sample_rate':15120000,'shaping':'SRRC alpha .05, 2 samples per symbol, 64-symbol span','samples':samples.size})
    a.output.parent.mkdir(parents=True,exist_ok=True)
    samples.tofile(a.output);a.output.with_suffix('.input.ts').write_bytes(transport)
    info.update({'generation_seconds':time.monotonic()-start,'iq_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),
                 'tables_sha256':hashlib.sha256(Path(__file__).with_name('gb20600-tables.json').read_bytes()).hexdigest(),
                 'media_sha256':hashlib.sha256((ROOT/'tests/fixtures/television-programs/programs.ts').read_bytes()).hexdigest()})
    a.output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf8')
    if a.output.suffix=='.sigmf-data':
        metadata={'global':{'core:version':'1.0.0','core:datatype':'cf32_le','core:sample_rate':info['sample_rate'],
                            'core:description':'Offline synthetic DTMB multi-program test; not recorded broadcast'},
                  'captures':[{'core:sample_start':0,'core:frequency':538000000}],'annotations':[]}
        a.output.with_suffix('.sigmf-meta').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf8')
    print(json.dumps(info),flush=True)
