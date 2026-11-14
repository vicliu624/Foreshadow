#include "signalscope/adapters/modulation/fm_audio.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include <cmath>
#include <limits>
#include <numbers>

namespace signalscope {
FmAudioIssue fmAudioConfigurationIssue(const FmAudioConfig& c) noexcept {
    if(c.input_rate<fm_minimum_input_rate || c.input_rate>fm_maximum_input_rate)return FmAudioIssue::input_rate;
    if(!std::isfinite(c.offset_hz) || std::abs(c.offset_hz)+fm_channel_guard_hz>c.input_rate/2.)return FmAudioIssue::frequency;
    if(c.deemphasis_us!=0 && c.deemphasis_us!=50 && c.deemphasis_us!=75)return FmAudioIssue::deemphasis;
    if(c.deviation_hz!=50000 && c.deviation_hz!=75000)return FmAudioIssue::deviation;
    return FmAudioIssue::none;
}
namespace {
using Complex=std::complex<float>;
constexpr double tau=2*std::numbers::pi;

template<class T> class LowPass {
public:
    LowPass(double rate,double cutoff,std::size_t radius)
        :taps_(2*radius+1),samples_(taps_.size()*2) {
        double sum=0;
        for(std::size_t k=0;k<taps_.size();++k) {
            const auto x=double(k)-radius;
            const auto a=tau*cutoff*x/rate;
            const auto window=.42+.5*std::cos(std::numbers::pi*x/radius)+.08*std::cos(tau*x/radius);
            taps_[k]=static_cast<float>((x==0?1.:std::sin(a)/a)*window);
            sum+=taps_[k];
        }
        for(auto& tap:taps_)tap/=static_cast<float>(sum);
    }
    void push(T sample) {
        samples_[next_]=sample;
        samples_[next_+taps_.size()]=sample;
        next_=(next_+1)%taps_.size();
        if(filled_<taps_.size())++filled_;
    }
    bool ready() const{return filled_==taps_.size();}
    T value(std::size_t back=0) const {
        const auto start=(next_+taps_.size()-back)%taps_.size();
        T sum{};
        for(std::size_t k=0;k<taps_.size();++k)sum+=samples_[start+k]*taps_[k];
        return sum;
    }
private:
    std::vector<float> taps_;
    std::vector<T> samples_;
    std::size_t next_=0,filled_=0;
};

FmAudioConfig checked(FmAudioConfig config) {
    if(fmAudioConfigurationIssue(config)!=FmAudioIssue::none)
        throw std::invalid_argument("Invalid FM channel, input rate, de-emphasis or deviation");
    return config;
}

class WaveWriter {
public:
    explicit WaveWriter(const std::filesystem::path& path):stream_(path,std::ios::binary) {
        if(!stream_)throw OperationError(ErrorCode::recording_write,"Could not create WAV output");
        header(0);
    }
    void append(std::span<const std::int16_t> samples) {
        Bytes bytes;
        bytes.reserve(samples.size()*2);
        for(const auto value:samples) {
            const auto word=static_cast<std::uint16_t>(value);
            bytes.push_back(static_cast<std::uint8_t>(word));
            bytes.push_back(static_cast<std::uint8_t>(word>>8));
        }
        stream_.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!stream_)throw OperationError(ErrorCode::recording_write,"Could not write WAV samples");
        frames_+=samples.size();
    }
    std::uint64_t frames() const{return frames_;}
    void finish() {
        stream_.seekp(0);
        header(static_cast<std::uint32_t>(frames_*2));
        stream_.flush();
        if(!stream_)throw OperationError(ErrorCode::recording_write,"Could not finalize WAV output");
    }
private:
    void integer(std::uint32_t value,unsigned count) {
        for(unsigned k=0;k<count;++k)stream_.put(static_cast<char>(value>>(8*k)));
    }
    void header(std::uint32_t bytes) {
        stream_.write("RIFF",4);integer(36+bytes,4);stream_.write("WAVEfmt ",8);integer(16,4);
        integer(1,2);integer(1,2);integer(fm_audio_rate,4);integer(fm_audio_rate*2,4);
        integer(2,2);integer(16,2);stream_.write("data",4);integer(bytes,4);
    }
    std::ofstream stream_;
    std::uint64_t frames_=0;
};
}

struct FmDemodulator::Impl {
    explicit Impl(FmAudioConfig options):cfg(checked(options)),stride(bytes_per_pair(cfg.format)),
        decimation(std::max(1u,cfg.input_rate/300000)),channelRate(double(cfg.input_rate)/decimation),
        mixerStep(std::polar(1.f,static_cast<float>(-tau*cfg.offset_hz/cfg.input_rate))),
        radio(cfg.input_rate,fm_channel_bandwidth_hz/2,std::size_t(std::ceil(cfg.input_rate/12000.))),
        audio(channelRate,16000,std::size_t(std::ceil(channelRate/2000.))),
        dcWeight(1-std::exp(-tau*30/channelRate)),
        deemphasisWeight(cfg.deemphasis_us?1-std::exp(-1e6/(channelRate*cfg.deemphasis_us)):1.) {
        if(cfg.input_rate>2000000) {
            channelizer=std::make_unique<Channelizer>(cfg.input_rate,400000,cfg.format,
                std::vector<AnalysisChannel>{{cfg.offset_hz,fm_channel_bandwidth_hz}});
            auto local=cfg;local.input_rate=400000;
            local.offset_hz-=channelizer->channels().front().offset_hz;
            decoder=std::make_unique<FmDemodulator>(local);
        }
    }

    void feed(std::span<const std::uint8_t> bytes,std::uint64_t first,const PcmSink& sink) {
        if(bytes.size()%stride || (started && first!=next))
            throw OperationError(ErrorCode::recording_invalid,"FM input must contain contiguous, complete I/Q pairs");
        started=true;
        next=first+bytes.size()/stride;
        if(channelizer) {
            channelizer->feed(bytes,[&](const ChannelizedBlock& block){
                decoder->impl_->feedFloat(block.samples,block.first_sample,sink);
            });
            return;
        }
        const auto& codec=sample_codec(cfg.format);
        process(bytes.size()/stride,[&](std::size_t i){return codec.decode(bytes.data()+i*stride);},sink);
    }
    void feedFloat(std::span<const Complex> samples,std::uint64_t first,const PcmSink& sink) {
        if(started && first!=next)throw OperationError(ErrorCode::recording_invalid,"FM float input must be contiguous");
        started=true;next=first+samples.size();
        process(samples.size(),[&](std::size_t i){return samples[i];},sink);
    }
    template<class Read>void process(std::size_t count,const Read& read,const PcmSink& sink) {
        std::vector<std::int16_t> pcm;
        pcm.reserve(count*fm_audio_rate/cfg.input_rate+1);
        for(std::size_t k=0;k<count;++k) {
            const auto value=read(k);
            radio.push(value*mixer);
            mixer*=mixerStep;
            if(++inputSamples%4096==0)mixer/=std::abs(mixer);
            if(inputSamples%decimation || !radio.ready())continue;
            const auto filtered=radio.value();
            const double energy=std::norm(filtered);
            moment2+=energy;moment4+=energy*energy;
            if(++momentSamples>=std::max<std::uint64_t>(256,std::llround(channelRate/20))) {
                const auto m2=moment2/momentSamples,m4=moment4/momentSamples;
                smooth2=metrics.windows?.8*smooth2+.2*m2:m2;
                smooth4=metrics.windows?.8*smooth4+.2*m4:m4;
                ++metrics.windows;
                metrics.power_dbfs=10*std::log10(std::max(smooth2,1e-30));
                const auto squared=2*smooth2*smooth2-smooth4;
                metrics.snr_db.reset();
                if(squared>0 && smooth2>1e-20) {
                    const auto signal=std::min(smooth2,std::sqrt(squared));
                    const auto noise=std::max(smooth2-signal,smooth2*1e-6);
                    metrics.snr_db=10*std::log10(std::max(signal/noise,1e-6));
                }
                moment2=moment4=0;momentSamples=0;
            }
            if(!havePrevious){previous=filtered;havePrevious=true;continue;}
            const auto discriminator=std::arg(filtered*std::conj(previous))*channelRate/(tau*cfg.deviation_hz);
            previous=filtered;
            dc+=dcWeight*(discriminator-dc);
            deemphasized+=deemphasisWeight*(discriminator-dc-deemphasized);
            audio.push(static_cast<float>(deemphasized));
            if(!audio.ready())continue;
            // Interpolate between adjacent filtered channel-rate samples.
            phase+=std::uint64_t(fm_audio_rate)*decimation;
            if(phase<cfg.input_rate)continue;
            phase-=cfg.input_rate;
            const auto back=double(phase)/(fm_audio_rate*decimation);
            const auto valueOut=(1-back)*audio.value()+back*audio.value(1);
            pcm.push_back(static_cast<std::int16_t>(std::lround(std::clamp(valueOut,-1.,1.)*32767)));
        }
        if(!pcm.empty())sink(pcm);
    }

    const FmAudioConfig cfg;
    const std::size_t stride;
    const unsigned decimation;
    const double channelRate;
    const Complex mixerStep;
    LowPass<Complex> radio;
    LowPass<float> audio;
    const double dcWeight,deemphasisWeight;
    Complex mixer{1,0},previous{};
    std::uint64_t next=0,inputSamples=0,phase=0;
    double dc=0,deemphasized=0;
    bool started=false,havePrevious=false;
    double moment2=0,moment4=0,smooth2=0,smooth4=0;
    std::uint64_t momentSamples=0;
    FmSignalMetrics metrics;
    std::unique_ptr<Channelizer> channelizer;
    std::unique_ptr<FmDemodulator> decoder;
};

FmDemodulator::FmDemodulator(const FmAudioConfig& options):impl_(std::make_unique<Impl>(options)) {}
FmDemodulator::~FmDemodulator()=default;
void FmDemodulator::feed(std::span<const std::uint8_t> bytes,std::uint64_t first,const PcmSink& sink) {
    impl_->feed(bytes,first,sink);
}
FmSignalMetrics FmDemodulator::signalMetrics() const {
    return impl_->decoder?impl_->decoder->signalMetrics():impl_->metrics;
}

Json exportFmAudio(const ReplayInfo& info,std::uint64_t first,std::uint64_t count,double frequency,
                   double deemphasisUs,double deviationHz,const std::filesystem::path& directory,
                   const std::function<bool()>& cancelled) {
    constexpr auto maxFrames=(std::numeric_limits<std::uint32_t>::max()-36ULL)/2;
    FmDemodulator decoder({info.rate,info.format,frequency-info.frequency,deemphasisUs,deviationHz});
    if(!count || first>info.pairs || count>info.pairs-first || double(count)/info.rate*fm_audio_rate>maxFrames)
        throw std::invalid_argument("FM export range is empty, outside the recording or too large for WAV");
    if(!std::filesystem::create_directory(directory))
        throw OperationError(ErrorCode::recording_write,"FM output directory already exists");
    WaveWriter writer(directory/"audio.wav");
    try {
        replay(info,first,count,0,65536,[&](auto bytes,auto sample){
            decoder.feed(bytes,sample,[&](auto pcm){writer.append(pcm);});
        },cancelled);
    } catch(...) {
        // Preserve the readable prefix if input fails after audio was written.
        try {writer.finish();} catch(...) {}
        throw;
    }
    writer.finish();
    return {{"format","wav_pcm_s16le"},{"channels",1},{"sample_rate_hz",fm_audio_rate},
            {"audio_frames",writer.frames()},{"center_frequency_hz",frequency},
            {"first_input_sample",first},{"requested_input_pairs",count},
            {"deemphasis_us",deemphasisUs},{"deviation_hz",deviationHz},
            {"status",cancelled()?"cancelled":"complete"}};
}
}
