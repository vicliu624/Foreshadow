#include "signalscope/adapters/modulation/fm/live_fm_audio.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <thread>

namespace {
using namespace signalscope;
constexpr std::uint32_t rate=1750000;
constexpr double center=100800000,offset=155000,tau=2*std::numbers::pi;

Bytes recording(SampleFormat format) {
    Bytes result;
    result.reserve(rate/5*bytes_per_pair(format));
    double phase=0;
    for(std::uint32_t k=0;k<rate/5;++k) {
        const auto time=tau*k/rate;
        phase+=tau*(offset+15000*std::sin(1000*time)+6500*std::sin(19000*time))/rate;
        const auto signal=std::polar(.35,phase)+std::polar(.40,355000*time);
        for(const auto component:{signal.real(),signal.imag()}) {
            if(format==SampleFormat::cu8)
                result.push_back(static_cast<std::uint8_t>(std::lround(127.5+128*component)));
            else {
                const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(32768*component)));
                result.push_back(static_cast<std::uint8_t>(word));
                result.push_back(static_cast<std::uint8_t>(word>>8));
            }
        }
    }
    return result;
}

std::vector<std::int16_t> decode(const Bytes& bytes,SampleFormat format,std::size_t chunk) {
    FmDemodulator decoder({rate,format,offset,0,75000});
    const auto stride=bytes_per_pair(format);
    std::vector<std::int16_t> output;
    for(std::size_t first=0;first<bytes.size()/stride;first+=chunk) {
        const auto count=std::min(chunk,bytes.size()/stride-first);
        decoder.feed(std::span(bytes).subspan(first*stride,count*stride),9000+first,
                     [&](auto pcm){output.insert(output.end(),pcm.begin(),pcm.end());});
    }
    return output;
}

double amplitude(std::span<const std::int16_t> samples,double frequency) {
    std::complex<double> sum{};
    for(std::size_t n=0;n<samples.size();++n)
        sum+=double(samples[n])*std::polar(1.,-tau*frequency*n/fm_audio_rate);
    return 2*std::abs(sum)/(32767.*samples.size());
}

void verifyLiveAudio() {
    const auto bytes=recording(SampleFormat::cu8);
    const auto expected=decode(bytes,SampleFormat::cu8,rate/5);
    LiveFmAudio audio;
    audio.start({rate,SampleFormat::cu8,offset,0,75000},bytes.size());
    const auto offer=[&](std::uint64_t first){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do {
            const auto dropped=audio.status().dropped_blocks;
            audio.offer(bytes,first);
            if(audio.status().dropped_blocks==dropped)return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("Live FM test could not queue I/Q");
    };
    const auto check=[&](std::uint64_t first){
        offer(first);
        std::vector<std::int16_t> result(expected.size());
        std::size_t count=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(count<expected.size() && !audio.status().failed && std::chrono::steady_clock::now()<deadline) {
            count+=audio.read(std::span(result).subspan(count));
            if(count<expected.size())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(count!=expected.size() || result!=expected)throw std::runtime_error("Live FM PCM differs from offline output");
    };
    check(9000);
    audio.offer(Bytes(bytes.size()+2),0);
    if(audio.status().dropped_blocks==0)throw std::runtime_error("Live FM accepted an oversized block");
    check(9000+rate/5+37);
    if(audio.status().discontinuities!=1)throw std::runtime_error("Live FM did not reset across an I/Q gap");
    audio.stop();
    audio.offer(bytes,0);
    std::int16_t sample=0;
    if(audio.read(std::span(&sample,1)))throw std::runtime_error("Live FM left PCM after stopping");
}
void verifyShortReadBurst() {
    const auto bytes=recording(SampleFormat::cu8);
    const auto expected=decode(bytes,SampleFormat::cu8,rate/5);
    LiveFmAudio audio;audio.start({rate,SampleFormat::cu8,offset,0,75000},65536);
    // The entire burst fits the declared byte budget. Thousands of tiny reads
    // must not contend with the DSP consumer or exhaust callback-count slots.
    for(std::size_t at=0;at<bytes.size();) {
        const auto size=std::min<std::size_t>(146,bytes.size()-at);
        audio.offer(std::span(bytes).subspan(at,size),(1ULL<<40)+at/2);at+=size;
    }
    std::vector<std::int16_t> pcm(expected.size());std::size_t count=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(count<pcm.size() && std::chrono::steady_clock::now()<deadline) {
        count+=audio.read(std::span(pcm).subspan(count));
        if(count<pcm.size())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto status=audio.status();audio.stop();
    if(status.failed || status.dropped_pairs || status.discontinuities || count!=expected.size() || pcm!=expected)
        throw std::runtime_error("Live FM lost an in-budget short-read burst or changed its PCM timeline");
}
}

int main() {
    try {
        for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
            const auto bytes=recording(format);
            const auto single=decode(bytes,format,rate/5);
            const auto chunks=decode(bytes,format,10913);
            if(single!=chunks || single.size()<9400 || single.size()>9600)return 1;
            const auto stable=std::span(single).subspan(480,8640);
            const auto tone=amplitude(stable,1000),pilot=amplitude(stable,19000);
            if(tone<.17 || tone>.23 || pilot>.003) {
                std::cerr<<sample_datatype(format)<<" tone="<<tone<<" pilot="<<pilot<<'\n';
                return 2;
            }
        }
        FmDemodulator decoder({rate,SampleFormat::cu8,offset});
        const Bytes pair{128,128};
        decoder.feed(pair,100,[](auto){});
        try {decoder.feed(pair,102,[](auto){});return 3;}
        catch(const OperationError&) {}
        verifyLiveAudio();
        verifyShortReadBurst();
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 4;
    }
}
