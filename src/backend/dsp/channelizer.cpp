#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/dsp/fftw_planner.hpp"
#include <kiss_fft.h>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#ifdef SIGNALSCOPE_DSP_FFTW
#include <fftw3.h>
#endif

namespace signalscope {
namespace {
using Complex=std::complex<float>;
constexpr double tau=2*std::numbers::pi;
class Transform {
public:
    Transform(std::size_t size,bool inverse=false) {
#ifdef SIGNALSCOPE_DSP_FFTW
        std::lock_guard lock(fftwPlannerMutex());
        input_=fftwf_alloc_complex(size);output_=fftwf_alloc_complex(size);
        if(input_ && output_)plan_=fftwf_plan_dft_1d(static_cast<int>(size),input_,output_,inverse?FFTW_BACKWARD:FFTW_FORWARD,FFTW_ESTIMATE);
#else
        input_.resize(size);output_.resize(size);plan_=kiss_fft_alloc(static_cast<int>(size),inverse,0,0);
#endif
        if(!plan_) {
#ifdef SIGNALSCOPE_DSP_FFTW
            fftwf_free(input_);fftwf_free(output_);
#endif
            throw std::bad_alloc();
        }
    }
    ~Transform(){
#ifdef SIGNALSCOPE_DSP_FFTW
        std::lock_guard lock(fftwPlannerMutex());
        if(plan_)fftwf_destroy_plan(plan_);
        fftwf_free(input_);fftwf_free(output_);
#else
        kiss_fft_free(plan_);
#endif
    }
    void set(std::size_t i,Complex value){
#ifdef SIGNALSCOPE_DSP_FFTW
        input_[i][0]=value.real();input_[i][1]=value.imag();
#else
        input_[i]={value.real(),value.imag()};
#endif
    }
    Complex at(std::size_t i) const {
#ifdef SIGNALSCOPE_DSP_FFTW
        return {output_[i][0],output_[i][1]};
#else
        return {output_[i].r,output_[i].i};
#endif
    }
    void run(){
#ifdef SIGNALSCOPE_DSP_FFTW
        fftwf_execute(plan_);
#else
        kiss_fft(plan_,input_.data(),output_.data());
#endif
    }
private:
#ifdef SIGNALSCOPE_DSP_FFTW
    fftwf_complex *input_=nullptr,*output_=nullptr;fftwf_plan plan_=nullptr;
#else
    std::vector<kiss_fft_cpx> input_,output_;kiss_fft_cfg plan_=nullptr;
#endif
};

class FractionalConverter {
public:
    FractionalConverter(std::uint32_t input,unsigned decimation,std::uint32_t output)
        :numerator_(input),denominator_(std::uint64_t(decimation)*output),bypass_(numerator_==denominator_) {
        coefficients_.resize(256*16);
        for(int phase=0;phase<256;++phase) {
            double sum=0;
            for(int tap=0;tap<16;++tap) {
                const auto x=tap-7-phase/256.;
                const auto sinc=std::abs(x)<1e-12?1.:std::sin(std::numbers::pi*x)/(std::numbers::pi*x);
                const auto window=.42+.5*std::cos(std::numbers::pi*x/8)+.08*std::cos(tau*x/8);
                coefficients_[phase*16+tap]=static_cast<float>(sinc*window);sum+=sinc*window;
            }
            for(int tap=0;tap<16;++tap)coefficients_[phase*16+tap]/=static_cast<float>(sum);
        }
        reset();
    }
    void reset(){samples_.assign(8,{});base_=-8;whole_=0;remainder_=0;produced_=0;}
    std::vector<Complex> feed(std::span<const Complex> input,bool final=false,std::uint64_t limit=UINT64_MAX) {
        std::vector<Complex> result;
        if(bypass_) {
            const auto count=std::min<std::uint64_t>(input.size(),limit>produced_?limit-produced_:0);
            result.assign(input.begin(),input.begin()+static_cast<std::ptrdiff_t>(count));produced_+=count;return result;
        }
        samples_.insert(samples_.end(),input.begin(),input.end());
        if(final)samples_.insert(samples_.end(),8,{});
        while(whole_+8<base_+static_cast<std::int64_t>(samples_.size()) && produced_<limit) {
            const auto phase=static_cast<std::size_t>(remainder_*256/denominator_);
            Complex value{};
            for(int tap=0;tap<16;++tap)
                value+=samples_[static_cast<std::size_t>(whole_-7+tap-base_)]*coefficients_[phase*16+tap];
            result.push_back(value);++produced_;
            remainder_+=numerator_;whole_+=static_cast<std::int64_t>(remainder_/denominator_);remainder_%=denominator_;
        }
        const auto remove=std::max<std::int64_t>(0,whole_-8-base_);
        if(remove>0 && remove<=static_cast<std::int64_t>(samples_.size())) {
            samples_.erase(samples_.begin(),samples_.begin()+remove);base_+=remove;
        }
        return result;
    }
private:
    std::uint64_t numerator_,denominator_,remainder_=0,produced_=0;
    bool bypass_;
    std::int64_t base_=-8,whole_=0;
    std::vector<Complex> samples_;
    std::vector<float> coefficients_;
};
}

std::vector<AnalysisChannel> planAnalysisChannels(std::uint32_t input,std::uint32_t output,double width,double signal,double edge_signal) {
    if(edge_signal==0)edge_signal=signal;
    if(!input || !output || !std::isfinite(width) || width<=0 || width>input ||
       !std::isfinite(signal) || signal<=0 || signal>=input || !std::isfinite(edge_signal) || edge_signal<=0 || edge_signal>signal)
        throw std::invalid_argument("Invalid channelizer coverage request");
    const auto core=.8*std::min(double(input),double(output))-signal;
    if(core<=0)throw std::invalid_argument("Analysis channel rate is too small for the requested signal bandwidth");
    const auto covered=std::min(width,double(input)-edge_signal);
    if(covered<=core)return {{0,covered}};
    const auto count=static_cast<std::size_t>(1+std::ceil((covered-core)/(core*.9)));
    if(count>1024)throw std::invalid_argument("Channelizer plan exceeds its bounded channel count");
    std::vector<AnalysisChannel> channels;
    const auto step=std::max(0.,(covered-core)/(count-1));
    for(std::size_t i=0;i<count;++i)channels.push_back({-(covered-core)/2+i*step,std::min(core,covered)});
    return channels;
}

class Channelizer::Impl {
public:
    struct SpectralRun {std::size_t output,source,response,count;};
    Impl(std::uint32_t input,std::uint32_t output,SampleFormat f,std::vector<AnalysisChannel> requested)
        :input_rate(input),out_rate(output),format(f),decimation(std::max(1u,input/output)),
         n(2048*decimation),history_size(96*decimation),hop(n-history_size),forward(n),inverse(2048,true),channels(std::move(requested)) {
        if(!input || !output || input>100000000 || output>100000000 || channels.empty() || channels.size()>1024)
            throw std::invalid_argument("Invalid channelizer configuration");
        const auto cutoff=.44*std::min(double(output),double(input))/input;
        std::vector<float> taps(history_size+1);double sum=0;
        for(std::size_t i=0;i<taps.size();++i) {
            const auto x=double(i)-history_size/2.;
            const auto a=tau*cutoff*x;
            const auto window=.42+.5*std::cos(tau*x/history_size)+.08*std::cos(2*tau*x/history_size);
            taps[i]=static_cast<float>(2*cutoff*(x==0?1:std::sin(a)/a)*window);sum+=taps[i];
        }
        for(std::size_t i=0;i<n;++i)forward.set(i,{i<taps.size()?static_cast<float>(taps[i]/sum):0,0});
        forward.run();response.resize(n);for(std::size_t i=0;i<n;++i)response[i]=forward.at(i);
        for(auto& channel:channels) {
            if(!std::isfinite(channel.offset_hz) || std::abs(channel.offset_hz)>input/2. || channel.search_width_hz<=0)
                throw std::invalid_argument("Invalid analysis channel");
            const auto bin=static_cast<std::int64_t>(std::llround(channel.offset_hz*n/input));
            channel.offset_hz=double(bin)*input/n;bins.push_back(bin);
            // RF-edge clipping and FFT wrap points depend only on the plan.
            // Resolve them once so the streaming loop walks contiguous bins.
            std::vector<SpectralRun> runs;
            const auto signed_n=static_cast<std::int64_t>(n);
            const auto shift=static_cast<std::size_t>((bin%signed_n+signed_n)%signed_n);
            for(std::size_t alias=0;alias<decimation;++alias) {
                for(std::size_t i=0;i<2048;++i) {
                    const auto k=i+alias*2048;
                    const auto signed_k=static_cast<std::int64_t>(k)-(k>=n/2?signed_n:0);
                    const auto source_bin=signed_k+bin;
                    if(source_bin< -signed_n/2 || source_bin>=signed_n/2)continue;
                    const auto wrapped=k+shift;
                    const auto source=wrapped>=n?wrapped-n:wrapped;
                    if(!runs.empty() && runs.back().output+runs.back().count==i &&
                       runs.back().source+runs.back().count==source &&
                       runs.back().response+runs.back().count==k)++runs.back().count;
                    else runs.push_back({i,source,k,1});
                }
            }
            spectral_runs.push_back(std::move(runs));
            converters.emplace_back(input,decimation,output);
        }
        reset();
    }
    void reset(){history.assign(history_size,{});pending.clear();input_pairs=frame_first=0;output_first.assign(channels.size(),0);finished=false;for(auto& c:converters)c.reset();}
    void process(std::span<const Complex> fresh,const ChannelizedSink& sink,std::uint64_t end) {
        for(std::size_t i=0;i<history_size;++i)forward.set(i,history[i]);
        for(std::size_t i=0;i<hop;++i)forward.set(history_size+i,i<fresh.size()?fresh[i]:Complex{});
        forward.run();
        for(std::size_t channel=0;channel<channels.size();++channel) {
            std::array<Complex,2048> folded{};
            // Preserve alias summation order and linear RF-edge clipping.
            for(const auto& run:spectral_runs[channel])
                for(std::size_t i=0;i<run.count;++i)
                    folded[run.output+i]+=forward.at(run.source+i)*response[run.response+i];
            for(std::size_t i=0;i<2048;++i)inverse.set(i,folded[i]/static_cast<float>(decimation));
            inverse.run();
            const auto phase_index=static_cast<std::int64_t>(frame_first%n)-static_cast<std::int64_t>(history_size);
            const auto rotation=std::polar(1.f,static_cast<float>(std::remainder(-tau*bins[channel]*phase_index/n,tau)));
            std::vector<Complex> coarse;coarse.reserve(hop/decimation);
            for(std::size_t i=96;i<2048 && frame_first+(i-96)*decimation<end;++i)
                coarse.push_back(inverse.at(i)*(rotation/2048.f));
            const auto limit=static_cast<std::uint64_t>(std::ceil((input_pairs+history_size/2.)*out_rate/input_rate));
            const bool final_frame=finished && frame_first+hop>=end;
            const auto result=converters[channel].feed(coarse,final_frame,final_frame?limit:UINT64_MAX);
            if(!result.empty()){sink({result,channel,output_first[channel]});output_first[channel]+=result.size();}
        }
        for(std::size_t i=0;i<history_size;++i) {
            const auto index=hop+i;
            history[i]=index<history_size?history[index]:index-history_size<fresh.size()?fresh[index-history_size]:Complex{};
        }
        frame_first+=hop;
    }
    std::uint32_t input_rate,out_rate;SampleFormat format;unsigned decimation;
    std::size_t n,history_size,hop;Transform forward,inverse;
    std::vector<AnalysisChannel> channels;std::vector<std::int64_t> bins;
    std::vector<std::vector<SpectralRun>> spectral_runs;
    std::vector<FractionalConverter> converters;std::vector<Complex> response,history,pending;
    std::vector<std::uint64_t> output_first;
    std::uint64_t input_pairs=0,frame_first=0;bool finished=false;
};
Channelizer::Channelizer(std::uint32_t input,std::uint32_t output,SampleFormat format,std::vector<AnalysisChannel> channels) {
    if(!input || !output || input>100000000 || output>100000000 || input/output>1024 || channels.empty() || channels.size()>1024)
        throw std::invalid_argument("Invalid channelizer rates or channel count");
    impl_=std::make_unique<Impl>(input,output,format,std::move(channels));
}
Channelizer::~Channelizer()=default;
void Channelizer::feed(std::span<const std::uint8_t> bytes,const ChannelizedSink& sink) {
    auto& s=*impl_;const auto& codec=sample_codec(s.format);const auto stride=codec.pair_bytes;
    if(s.finished || bytes.size()%stride)throw std::invalid_argument("Invalid channelizer input block");
    for(std::size_t i=0;i<bytes.size();i+=stride) {
        const auto* p=bytes.data()+i;
        s.pending.push_back(codec.decode(p));
    }
    s.input_pairs+=bytes.size()/stride;std::size_t consumed=0;
    while(s.pending.size()-consumed>=s.hop){s.process(std::span(s.pending).subspan(consumed,s.hop),sink,s.input_pairs);consumed+=s.hop;}
    if(consumed)s.pending.erase(s.pending.begin(),s.pending.begin()+static_cast<std::ptrdiff_t>(consumed));
}
void Channelizer::feed(std::span<const Complex> samples,const ChannelizedSink& sink) {
    auto& s=*impl_;
    if(s.finished)throw std::invalid_argument("Invalid channelizer input block");
    s.pending.insert(s.pending.end(),samples.begin(),samples.end());s.input_pairs+=samples.size();
    std::size_t consumed=0;
    while(s.pending.size()-consumed>=s.hop){s.process(std::span(s.pending).subspan(consumed,s.hop),sink,s.input_pairs);consumed+=s.hop;}
    if(consumed)s.pending.erase(s.pending.begin(),s.pending.begin()+static_cast<std::ptrdiff_t>(consumed));
}
void Channelizer::finish(const ChannelizedSink& sink) {
    auto& s=*impl_;if(s.finished)return;s.finished=true;
    if(!s.input_pairs)return;
    const auto end=s.input_pairs+s.history_size/2;
    while(s.frame_first<end) {s.process(s.pending,sink,end);s.pending.clear();}
}
void Channelizer::reset(){impl_->reset();}
std::span<const AnalysisChannel> Channelizer::channels() const {return impl_->channels;}
double Channelizer::delay_input_samples() const {return impl_->history_size/2.;}
std::uint32_t Channelizer::output_rate() const {return impl_->out_rate;}
}
