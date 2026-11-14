#include "signalscope/adapters/modulation/fm_audio.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void append(Bytes& out,std::complex<double> value,SampleFormat format) {
    for(const auto component:{value.real(),value.imag()}) {
        if(format==SampleFormat::cu8)out.push_back(static_cast<std::uint8_t>(std::lround(127.5+128*component)));
        else {const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(32768*component)));out.push_back(word&255);out.push_back(word>>8);}
    }
}
Bytes signal(std::uint32_t rate,double offset,double seconds,double amplitude,double noise,SampleFormat format,bool interference=false) {
    Bytes out;out.reserve(static_cast<std::size_t>(rate*seconds)*bytes_per_pair(format));std::mt19937 random(20261110);std::normal_distribution<double> gaussian(0,noise>0?noise:1.);
    double phase=0;
    for(std::uint32_t i=0;i<rate*seconds;++i) {
        phase+=tau*(offset+15000*std::sin(tau*1000*i/rate))/rate;
        auto value=std::polar(amplitude,phase)+(noise>0?std::complex<double>(gaussian(random),gaussian(random)):std::complex<double>{});
        if(interference)value+=std::polar(.2,tau*.25*i);
        append(out,value,format);
    }return out;
}
struct Result {std::vector<std::int16_t> pcm;FmSignalMetrics metrics;};
Result decode(const Bytes& input,std::uint32_t rate,double offset,SampleFormat format,std::size_t chunk) {
    FmDemodulator decoder({rate,format,offset,0,75000});Result result;const auto stride=bytes_per_pair(format);
    for(std::size_t at=0;at<input.size();) {
        const auto count=std::min(input.size()-at,chunk*stride);
        decoder.feed(std::span(input).subspan(at,count),(1ULL<<40)+at/stride,[&](auto pcm){result.pcm.insert(result.pcm.end(),pcm.begin(),pcm.end());});at+=count;
    }result.metrics=decoder.signalMetrics();return result;
}
double tone(std::span<const std::int16_t> pcm) {
    std::complex<double> sum{};for(std::size_t i=0;i<pcm.size();++i)sum+=double(pcm[i])*std::polar(1.,-tau*1000*i/fm_audio_rate);
    return 2*std::abs(sum)/(32767.*pcm.size());
}
}
int main(){try {
    for(const auto rate:{2560000u,10000000u})for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
        const double offset=-.3*rate;const auto bytes=signal(rate,offset,.08,.3,0,format,true);
        const auto whole=decode(bytes,rate,offset,format,bytes.size()/bytes_per_pair(format));const auto split=decode(bytes,rate,offset,format,448);
        require(whole.pcm==split.pcm,"High-rate FM changed PCM across short reads or an arbitrary 64-bit source origin");
        require(split.pcm.size()>3000,"High-rate FM did not produce enough audio");
        const auto measured=tone(std::span(split.pcm).subspan(480,2400));
        require(std::abs(measured-.2)<.035,"Wideband channel extraction lost the known FM audio tone or admitted a distant carrier");
        require(split.metrics.windows>0 && split.metrics.power_dbfs.has_value(),"Channelized FM omitted signal quality metrics");
    }
    constexpr auto rate=400000u;constexpr double offset=30000,amplitude=.1;
    const auto clean=decode(signal(rate,offset,.5,amplitude,0,SampleFormat::ci16_le),rate,offset,SampleFormat::ci16_le,8192);
    require(clean.metrics.snr_db && *clean.metrics.snr_db>30,"A clean constant-envelope FM signal had a low SNR estimate");
    double previous=-100;
    for(const auto target:{0.,10.,20.}) {
        const auto noise=amplitude/std::sqrt(2*std::pow(10.,target/10));
        const auto pureNoise=decode(signal(rate,offset,.5,0,noise,SampleFormat::ci16_le),rate,offset,SampleFormat::ci16_le,8192);
        const auto mixed=decode(signal(rate,offset,.5,amplitude,noise,SampleFormat::ci16_le),rate,offset,SampleFormat::ci16_le,8192);
        const auto reference=*clean.metrics.power_dbfs-*pureNoise.metrics.power_dbfs;
        require(mixed.metrics.snr_db && std::abs(*mixed.metrics.snr_db-reference)<3,"FM SNR estimate disagrees with independently measured filtered signal/noise power");
        require(*mixed.metrics.snr_db>previous+6,"FM SNR did not respond monotonically to a 10 dB change in injected noise");previous=*mixed.metrics.snr_db;
        require(!pureNoise.metrics.snr_db || *pureNoise.metrics.snr_db<3,"Noise alone acquired a strong-signal SNR estimate");
    }
    require(fmAudioConfigurationIssue({200000,SampleFormat::ci16_le,0})==FmAudioIssue::input_rate &&
            fmAudioConfigurationIssue({2000000,SampleFormat::ci16_le,950000})==FmAudioIssue::frequency,
            "FM configuration validation lost whole-channel bandwidth constraints");
    std::cout<<"High-rate FM, fractional clocks, short reads, 64-bit origins and estimated RF quality passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
