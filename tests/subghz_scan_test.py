"""验证同一宽带录制中的同时突发、载波反例及指定信道入口。"""
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from classify_subghz import Channelizer

RATE = 3_000_000

def check_channelizer():
    rate = 1_750_000
    time = np.arange(10001)/rate
    target = 175123.45
    baseband = 1834.7
    tone = .3*np.exp(2j*np.pi*(target+baseband)*time)
    interference = .65*np.exp(-2j*np.pi*170000*time)
    channelizer = Channelizer(tone+interference, rate, 40000)
    extracted, channel_rate = channelizer.extract(target)
    expected = .3*np.exp(2j*np.pi*baseband*np.arange(len(extracted))/channel_rate)
    assert np.sqrt(np.mean(abs(extracted[20:-20]-expected[20:-20])**2)) < .002


def main():
    check_channelizer()
    center = 433_920_000
    rng = np.random.default_rng(261105)
    time = np.arange(606208)/RATE
    iq = .002*(rng.normal(size=len(time))+1j*rng.normal(size=len(time)))
    with TemporaryDirectory() as fixtures:
        for mode, offset in (('ook', -282000), ('fsk', 145000)):
            directory = Path(fixtures)/mode
            subprocess.run([sys.argv[1], '--mode', mode, '--output', str(directory)], check=True)
            waveform = np.fromfile(directory/'waveform.iq', dtype='<i2').reshape(-1, 2)
            iq += (waveform[:, 0]+1j*waveform[:, 1])/32768*np.exp(2j*np.pi*(offset-75000)*time)
    tones = np.array([-6750, -2250, 2250, 6750])
    symbols = np.tile([0, 2, 1, 3], 20)
    frequency = 610000+np.repeat(tones[symbols], RATE//2400)
    burst = .16*np.exp(2j*np.pi*np.cumsum(frequency)/RATE)
    for start in (60000, 240000, 420000):
        iq[start:start+len(burst)] += burst
    iq += .06*np.exp(-2j*np.pi*105000*time)
    iq[260000:345000] += .09*np.exp(2j*np.pi*370000*time[260000:345000])

    with TemporaryDirectory() as temporary:
        meta = Path(temporary)/'multi.sigmf-meta'
        meta.write_text(json.dumps({
            'global': {'core:datatype': 'ci16_le', 'core:sample_rate': RATE},
            'captures': [{'core:frequency': center, 'core:sample_start': 0}],
        }), encoding='utf-8')
        np.rint(np.column_stack((iq.real, iq.imag))*32768).clip(-32768, 32767).astype('<i2').tofile(
            meta.with_suffix('.sigmf-data'))
        command = [sys.executable, str(ROOT/'tools/classify_subghz.py'), '--input', str(meta)]
        result = json.loads(subprocess.check_output(command+['--scan']))
        expected = [(center-282000, ['OOK']*3), (center+145000, ['2-FSK/GFSK']*3),
                    (center+370000, ['unknown']), (center+610000, ['4-FSK']*3)]
        assert result['discovered_channels'] == len(expected), result
        for channel, (frequency, labels) in zip(result['channels'], expected):
            assert abs(channel['channel_hz']-frequency) < 2000, channel
            assert [event['modulation_candidate'] for event in channel['events']] == labels, channel
        manual = json.loads(subprocess.check_output(command+['--channel-hz', str(center+145000)]))
        assert [event['modulation_candidate'] for event in manual['events']] == ['2-FSK/GFSK']*3
        section = json.loads(subprocess.check_output(command+[
            '--channel-hz', str(center+145000), '--start-seconds', '.055', '--duration-seconds', '.14']))
        assert section['analysis_start_seconds'] == .055 and section['analysis_duration_seconds'] == .14
        assert [event['modulation_candidate'] for event in section['events']] == ['2-FSK/GFSK']*2
        assert all(.07 < event['start_seconds'] < .15 for event in section['events']), section
        print('Four channels verified, including overlapping OOK, 2-FSK and 4-FSK bursts.')


if __name__ == '__main__':
    main()
