#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/backend/dsp/fftw_planner.hpp"
#include "signalscope/backend/dsp/analysis_priority.hpp"
#include "signalscope/backend/dsp/parallel_channels.hpp"
#include <kiss_fft.h>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <thread>
#include <unordered_set>
#ifdef SIGNALSCOPE_FFTW
#include <fftw3.h>
#endif
#ifdef SIGNALSCOPE_VOLK
#include <volk/volk.h>
#endif

namespace signalscope {
std::vector<double> defaultLiveLoRaBandwidths(){return {125000,250000,500000};}
namespace {
struct Peak { int bin=0; double ratio_db=0, concentration=0, power=0,prominence_db=0,narrow_db=0; };
struct CachedUpPeak {
    std::uint64_t sample=std::numeric_limits<std::uint64_t>::max();
    Peak peak;
};
struct WeakSeed {std::uint64_t first=0;Peak a,b;bool active=false;};
struct Frame { std::uint64_t sample=0; Peak up,down; bool down_measured=false; };
struct Track { unsigned count=0; int bin=0; std::uint64_t first=0; bool emitted=false; };
struct Pending {
    std::uint64_t quarter_start=0,pre_start=0,down_start=0;
    double offset=0,up_db=0,down_db=0,power=0,component_power=0;
    bool active=false,needs_refinement=false;
    double down_component=0;
};
struct Projection {std::complex<double> value{};double energy=0;double ratio_db() const {return 10*std::log10(std::max(std::norm(value)/std::max(energy,1e-30),1e-30));}};
struct ProjectionKernel {double frequency=0;bool down=false;std::uint64_t used=0;std::vector<std::complex<float>> values;};
struct FineBudgetExceeded {};
std::size_t symbolSamples(const LoRaConfig& config,int sf,double bandwidth) {
    const auto raw=config.rate*std::ldexp(1.0,sf)/bandwidth;
    if(!std::isfinite(raw) || std::abs(raw-std::round(raw))>1e-6 || raw<16 || raw>262144)return 0;
    const auto count=static_cast<std::size_t>(std::llround(raw));
    return config.time_phases && count%config.time_phases==0?count:0;
}

class Search {
public:
    Search(const LoRaConfig& c,int sf,bool inverted) : c_(c),sf_(sf),inverted_(inverted) {
        n_=symbolSamples(c,sf,c.bandwidth);
        if(!n_)throw std::invalid_argument("LoRa symbol timing is incompatible with the sample rate, bandwidth or timing phases. Try 2 MS/s.");
        hop_=n_/c.time_phases;
        reference_.resize(n_);down_reference_.resize(n_);magnitudes_.resize(n_);
#ifndef SIGNALSCOPE_FFTW
        in_.resize(n_);out_.resize(n_);
#endif
        for(std::size_t i=0;i<n_;++i) {
            const double t=double(i)/c.rate, ts=double(n_)/c.rate;
            const double phase=2*std::numbers::pi*(-c.bandwidth*t/2+c.bandwidth*t*t/(2*ts));
            reference_[i]={static_cast<float>(std::cos(phase)),static_cast<float>(std::sin(phase))};
            down_reference_[i]=std::conj(reference_[i]);
        }
#ifdef SIGNALSCOPE_FFTW
        std::lock_guard lock(fftwPlannerMutex());
        fast_in_=fftwf_alloc_complex(n_); fast_out_=fftwf_alloc_complex(n_);
        if(!fast_in_ || !fast_out_) { fftwf_free(fast_in_); fftwf_free(fast_out_); throw std::bad_alloc(); }
        fast_plan_=fftwf_plan_dft_1d(static_cast<int>(n_),fast_in_,fast_out_,FFTW_FORWARD,FFTW_ESTIMATE);
        if(!fast_plan_) { fftwf_free(fast_in_); fftwf_free(fast_out_); throw std::bad_alloc(); }
#else
        fft_=kiss_fft_alloc(static_cast<int>(n_),0,nullptr,nullptr);
        if(!fft_) throw std::bad_alloc();
#endif
    }
    ~Search() {
#ifdef SIGNALSCOPE_FFTW
        std::lock_guard lock(fftwPlannerMutex());
        fftwf_destroy_plan(fast_plan_); fftwf_free(fast_in_); fftwf_free(fast_out_);
#else
        kiss_fft_free(fft_);
#endif
    }
    void reset() {
        samples_={};energies_={};for(auto& history:history_)history.clear();
        kernel_clock_=0;for(auto& kernel:projection_kernels_)kernel.used=0;for(auto& kernel:transition_kernels_)kernel.used=0;
        tracks_.fill({});pending_.fill({});armed_until_.fill(0);preamble_bin_.fill(0);preamble_peak_.fill({});preamble_first_.fill(0);
        preamble_confirmed_.fill(false);weak_seed_={};half_peak_={};have_half_=false;weak_candidates_=weak_accepted_=0;
        fine_attempts_=fine_matches_=projection_points_=fine_budget_exhaustions_=0;projection_limit_=std::numeric_limits<std::uint64_t>::max();
        last_refinement_offset_=0;last_refinement_down_=0;
        fine_stages_.fill(0);
        up_peaks_.fill({});cached_up_windows_=0;
        expected_=base_=next_=frame_=active_until_=next_probe_report_=0;fft_count_=fft_points_=cached_neighbors_=0;have_last_=false;
    }
    std::size_t symbol_samples() const {return n_;}
    Json work() const {return {{"bandwidth_hz",c_.bandwidth},{"sf",sf_},{"inverted",inverted_},
        {"last_refinement_offset_hz",last_refinement_offset_},{"last_refinement_down_sample",last_refinement_down_},
        {"fft_count",fft_count_},{"fft_points",fft_points_},{"cached_neighbors",cached_neighbors_},
        {"cached_up_windows",cached_up_windows_},
        {"fine_attempts",fine_attempts_},{"fine_matches",fine_matches_},{"projection_points",projection_points_},
        {"fine_budget_exhaustions",fine_budget_exhaustions_},
        {"fine_stage_passes",{{"preamble",fine_stages_[0]},{"neighbor",fine_stages_[1]},
            {"down_pair",fine_stages_[2]},{"quarter",fine_stages_[3]},
            {"sync",fine_stages_[4]},{"direction",fine_stages_[5]}}},
        {"weak_seed_candidates",weak_candidates_},{"weak_seed_accepted",weak_accepted_}};}
    void feed(std::span<const std::complex<float>> samples,std::span<const double> energies,std::uint64_t base,std::uint64_t first,std::uint64_t end,Json& events,bool collect_probes,bool collect_css) {
        if(first!=expected_)throw std::runtime_error("Detector input is discontinuous. Reset the detector before continuing");
        if(base!=base_)up_peaks_.fill({});
        samples_=samples;energies_=energies;base_=base;expected_=end;collect_css_=collect_css;verify_weak_seed();
        // 对最短符号可逐符号探测，长符号沿用配置的稀疏步长。
        const auto probe_stride = dense_short_probe() ? 1u : c_.probe_stride_symbols;
        while(next_+n_<=expected_) {
            const auto phase=frame_%c_.time_phases;
            const bool probe=((frame_/c_.time_phases)%probe_stride)==0;
            bool active=active_until_!=0 && next_<=active_until_;
            if(probe || active) {
                const auto peak=measure_up(next_);
                // 随机 FFT 最大峰在低门限下很常见；只有相邻符号的同频峰
                // 才启动/续期密集搜索。仍从原始历史回填，不漏掉种子之前的前导码。
                bool coherent_seed=false;
                if(trigger(peak) && next_>=base_+n_) {
                    const auto& history=history_[phase];
                    const bool cached=!history.empty() && history.back().sample+n_==next_;
                    if(cached) {
                        ++cached_neighbors_;
                        const auto& previous=history.back().up;
                        coherent_seed=strong(previous) && same(previous.bin,peak.bin);
                    } else {
                        const auto previous=measure_up(next_-n_);
                        coherent_seed=strong(previous) && same(previous.bin,peak.bin);
                    }
                }
                if(probe&&c_.time_phases==2) {
                    if(phase==0){half_peak_=peak;half_sample_=next_;have_half_=true;}
                    else if(have_half_&&half_sample_+hop_==next_&&!weak_seed_.active) {
                        const int difference=(peak.bin-half_peak_.bin+static_cast<int>(n_))%static_cast<int>(n_);
                        const int half=(1<<sf_)/2;
                        if(weak_peak(half_peak_)&&weak_peak(peak)&&std::min(std::abs(difference-half),std::abs(difference-(static_cast<int>(n_)-half)))<=2){weak_seed_={half_sample_,half_peak_,peak,true};++weak_candidates_;}
                        have_half_=false;verify_weak_seed();active=active_until_!=0&&next_<=active_until_;
                    }
                }
                // 为结构匹配预留结果空间，环境探针不能耗尽离线录制的事件上限。
                if(collect_probes && probe && trigger(peak) && next_>=next_probe_report_ && events.size()<256) {
                    // 只节流诊断输出；下面的种子跟踪和结构检测不跳样。
                    next_probe_report_=next_+std::max<std::uint64_t>(n_,c_.rate/4);
                    events.push_back({{"evidence_level","chirp_probe"},{"sf_candidate",sf_},{"bandwidth_hz",c_.bandwidth},
                        {"first_observed_sample",next_},{"confirmed_at_sample",next_+n_},
                        {"dechirp_peak_offset_hz",(inverted_?-1:1)*signed_bin(peak.bin)*double(c_.rate)/n_},
                        {"peak_to_mean_db",peak.ratio_db},{"iq_inverted",inverted_}});
                }
                if(coherent_seed || active) {
                    // 最后一个前导码后仅需覆盖两个同步符号、两个下 Chirp
                    // 和四分之一结尾；pending 验证独立于密集窗口继续运行。
                    if(coherent_seed)active_until_=std::max(active_until_,next_+5*n_);
                    auto& history=history_[phase];
                    if(history.empty() || history.back().sample+n_!=next_) {
                        const bool provisional=!preamble_confirmed_[phase]&&armed_until_[phase]>=next_;
                        history.clear();tracks_[phase]={};if(!provisional)armed_until_[phase]=0;
                        const auto back=std::min<std::uint64_t>((next_-base_)/n_,c_.repeat_symbols+4);
                        for(auto i=back;i>0;--i)process_frame(next_-i*n_,phase,events,nullptr);
                    }
                    process_frame(next_,phase,events,&peak);
                }
            }
            next_+=hop_;++frame_;verify_pending(events);
        }
        verify_pending(events);
    }
private:
    Peak measure_up(std::uint64_t sample) {
        // 仅缓存网格上的整符号峰。需要完整 FFT 频谱的调用仍直接测量。
        auto& entry=up_peaks_[(sample/hop_)%up_peaks_.size()];
        if(entry.sample==sample){++cached_up_windows_;return entry.peak;}
        entry={sample,measure(sample,false)};
        return entry.peak;
    }
    bool dense_short_probe() const {
        return c_.dense_short_symbols && n_*1000000ULL <= c_.rate*512ULL;
    }
    void process_frame(std::uint64_t sample,std::size_t phase,Json& events,const Peak* measured) {
            auto& history=history_[phase];
            auto& track=tracks_[phase];
            Frame current; current.sample=sample;
            current.up=measured?*measured:measure_up(sample);
            bool armed=armed_until_[phase]!=0 && sample<=armed_until_[phase];
            // 仅在已有前导码证据后运行反向检测，限制无信号时的开销。
            if(armed && (history.empty() || current.sample-history.back().sample==n_)) {
                current.down=measure(sample,true,preamble_bin_[phase],1<<sf_,true); current.down_measured=true;
            }
            if(current.down_measured && history.size()>=c_.repeat_symbols+3) {
                const auto& previous=history.back();
                // 已有连续前导码后，给下 Chirp 2 dB 泄漏容差；仍验证两个
                // 同频下 Chirp、同步网格、四分之一结尾及其结束。
                if(previous.down_measured && strong(current.down,2) && strong(previous.down,2) && same(current.down.bin,previous.down.bin)) {
                    const auto sync2=history[history.size()-2].up;
                    const auto sync1=history[history.size()-3].up;
                    const auto pre=history[history.size()-4].up;
                    bool valid=strong(sync1) && strong(sync2) && strong(pre);
                    for(unsigned i=0;i<c_.repeat_symbols;++i) {
                        const auto& item=history[history.size()-4-i].up;
                        valid=valid && strong(item) && same(item.bin,pre.bin);
                    }
                    // 只验证两个同步 Chirp 相对前导码的网格结构，不恢复同步字数值。
                    valid=valid && sync_grid(sync1.bin-pre.bin) && sync_grid(sync2.bin-pre.bin);
                    const double down_hz=signed_bin(current.down.bin)*double(c_.rate)/n_;
                    const auto schedule=[&](const Peak& reference,bool refine){
                        const double up_hz=signed_bin(reference.bin)*double(c_.rate)/n_;
                        const double offset=(up_hz+down_hz)/2;
                        const double shift=(up_hz-down_hz)*n_/(2*c_.bandwidth);
                        const auto down_start=static_cast<std::int64_t>(previous.sample)-static_cast<std::int64_t>(std::llround(shift));
                        if(std::abs(offset)>c_.search_width/2 || std::abs(shift)>double(n_)/2+2 || down_start<0)return false;
                        auto& pending=pending_[phase];
                        pending={static_cast<std::uint64_t>(down_start)+2*n_,preamble_first_[phase],
                            static_cast<std::uint64_t>(down_start),offset,reference.ratio_db,current.down.ratio_db,reference.power,reference.power*reference.concentration,true,refine};
                        pending.down_component=current.down.power*current.down.concentration;
                        return true;
                    };
                    if(!valid || !schedule(pre,false))schedule(preamble_peak_[phase],true);
                }
            }
            const auto& peak=current.up;
            if(!strong(peak)) track={};
            else {
                if(!track.count || !same(peak.bin,track.bin)) track={0,peak.bin,sample,false};
                if(track.count<c_.repeat_symbols+1024)++track.count;
                track.bin=peak.bin;
                if(track.count>=c_.repeat_symbols){armed_until_[phase]=sample+6*n_;preamble_bin_[phase]=peak.bin;preamble_peak_[phase]=peak;preamble_first_[phase]=track.first;preamble_confirmed_[phase]=true;}
                if(track.count>=c_.repeat_symbols && !track.emitted) {
                    const double offset=(inverted_?-1:1)*signed_bin(peak.bin)*double(c_.rate)/n_;
                    if(collect_css_ && std::abs(offset)<=c_.search_width/2+c_.bandwidth/2 && events.size()<512)
                        events.push_back({{"evidence_level","css_candidate"},{"sf_candidate",sf_},{"bandwidth_hz",c_.bandwidth},
                            {"first_observed_sample",track.first},{"confirmed_at_sample",sample+n_},
                            {"repeated_chirps",track.count},{"dechirp_peak_offset_hz",offset},
                            {"center_frequency_interval_hz",{c_.receiver_frequency+offset-c_.bandwidth/2,c_.receiver_frequency+offset+c_.bandwidth/2}},
                            {"peak_to_mean_db",peak.ratio_db},{"peak_concentration",peak.concentration},
                            {"fullband_power_dbfs",peak.power>0?Json(10*std::log10(peak.power)):Json(nullptr)},
                            {"iq_inverted",inverted_},{"preamble_direction",inverted_?"down":"up"},
                            {"score_type","uncalibrated_feature_score"},{"needs_lora_structure_validation",true}});
                    track.emitted=true;
                }
            }
            history.push_back(current);
            if(history.size()>c_.repeat_symbols+8) history.pop_front();
    }
    bool strong(const Peak& peak,double margin=0) const { return peak.ratio_db>=c_.peak_to_mean_db-margin && peak.prominence_db>=5; }
    // 稀疏探测使用较强种子，触发后回看历史并按较低结构门限验证。
    // 避免低门限下随机 FFT 峰不断延长全速搜索，拖慢连续采集。
    bool trigger(const Peak& peak) const { return strong(peak) && peak.ratio_db>=std::max(13.0,c_.peak_to_mean_db); }
    bool weak_peak(const Peak& peak) const {return peak.ratio_db>=std::max(6.,c_.peak_to_mean_db-2)&&peak.prominence_db>=5&&peak.narrow_db>=3.5;}
    void verify_weak_seed() {
        if(!weak_seed_.active)return;
        if(weak_seed_.first<base_){weak_seed_.active=false;return;}
        if(weak_seed_.first+2*n_>expected_)return;
        const auto third=measure_up(weak_seed_.first+n_);
        if(weak_peak(third)&&same(third.bin,weak_seed_.a.bin)) {
            ++weak_accepted_;active_until_=std::max(active_until_,weak_seed_.first+10*n_);
            for(std::size_t phase=0;phase<2;++phase) {
                if(preamble_confirmed_[phase]&&armed_until_[phase]>=weak_seed_.first)continue;
                const auto& peak=phase?weak_seed_.b:weak_seed_.a;
                const auto first=weak_seed_.first+phase*hop_;
                if(armed_until_[phase]<first||!same(preamble_bin_[phase],peak.bin))preamble_first_[phase]=first;
                else preamble_first_[phase]=std::min(preamble_first_[phase],first);
                armed_until_[phase]=weak_seed_.first+10*n_;preamble_bin_[phase]=peak.bin;preamble_peak_[phase]=peak;preamble_confirmed_[phase]=false;
            }
        }
        weak_seed_.active=false;
    }
    int signed_bin(int bin) const { return bin>static_cast<int>(n_/2)?bin-static_cast<int>(n_):bin; }
    bool same(int a,int b) const { const auto d=std::abs(a-b); return std::min(d,static_cast<int>(n_)-d)<=2; }
    bool sync_grid(int difference) const {
        const int bins=1<<sf_;
        const int shift=(difference%bins+bins)%bins;
        const int remainder=shift%8;
        return std::min(remainder,8-remainder)<=2;
    }
    Projection project(std::uint64_t first,bool down,double frequency,std::size_t length,std::size_t reference_offset=0) {
        if(length>projection_limit_-projection_points_)throw FineBudgetExceeded{};
        projection_points_+=length;
        const auto* data=samples_.data()+static_cast<std::size_t>(first-base_);
        ProjectionKernel* kernel=nullptr;
        for(auto& item:projection_kernels_)if(!item.values.empty()&&item.frequency==frequency&&item.down==down){kernel=&item;break;}
        if(!kernel) {
            kernel=&*std::min_element(projection_kernels_.begin(),projection_kernels_.end(),[](const auto& a,const auto& b){return a.used<b.used;});
            kernel->values.resize(n_);kernel->frequency=frequency;kernel->down=down;
            const auto step=std::polar(1.0,-2*std::numbers::pi*frequency/c_.rate);std::complex<double> oscillator(1,0);
            for(std::size_t i=0;i<n_;++i){std::complex<double> ref(reference_[i]);if(!down)ref=std::conj(ref);const auto weight=ref*oscillator;kernel->values[i]=static_cast<std::complex<float>>(inverted_?std::conj(weight):weight);oscillator*=step;}
        }
        kernel->used=++kernel_clock_;Projection result;
        const auto* weights=kernel->values.data()+reference_offset;
#ifdef SIGNALSCOPE_VOLK
        std::complex<float> sum;
        volk_32fc_x2_dot_prod_32fc(&sum,data,weights,static_cast<unsigned>(length));result.value=sum;
#else
        for(std::size_t i=0;i<length;++i)result.value+=std::complex<double>(data[i])*std::complex<double>(weights[i]);
#endif
        if(inverted_)result.value=std::conj(result.value);
        result.value*=std::polar(1.0,2*std::numbers::pi*frequency*reference_offset/c_.rate);
        const auto at=static_cast<std::size_t>(first-base_);result.energy=energies_[at+length]-energies_[at];
        return result;
    }
    double normalized_power(int bin) const {
        bin=(bin%static_cast<int>(n_)+static_cast<int>(n_))%static_cast<int>(n_);
        const auto raw=inverted_?(static_cast<int>(n_)-bin)%static_cast<int>(n_):bin;
        return magnitudes_[raw];
    }
    double refined_sync_score(std::uint64_t first,double frequency) {
        const auto dominant=measure(first,false);
        const auto dominant_power=normalized_power(dominant.bin);
        std::array<std::pair<double,double>,8> candidates{};
        const double bin_hz=double(c_.rate)/n_;
        for(int shift=-(1<<sf_);shift<=(1<<sf_);shift+=8) {
            const double f=frequency+shift*bin_hz;
            const int bin=static_cast<int>(std::llround(f/bin_hz));
            const double power=normalized_power(bin-1)+normalized_power(bin)+normalized_power(bin+1);
            auto weakest=std::min_element(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            if(power>weakest->first)*weakest={power,f};
        }
        double best=-300;
        for(const auto& candidate:candidates) {
            if(candidate.first<=0)continue;
            const auto value=project(first,false,candidate.second,n_);
            if(value.ratio_db()<c_.peak_to_mean_db-2 || std::norm(value.value)<dominant_power*.8)continue;
            const int bin=static_cast<int>(std::llround(candidate.second/bin_hz));
            std::array<double,64> background{};const int extent=(1<<sf_)/2;
            for(int i=0;i<32;++i){const int delta=4+(i+1)*(extent-4)/32;background[2*i]=normalized_power(bin+delta);background[2*i+1]=normalized_power(bin-delta);}
            auto upper=background.begin()+48;std::nth_element(background.begin(),upper,background.end());
            if(10*std::log10(std::norm(value.value)/std::max(*upper,1e-30))>=5)best=std::max(best,value.ratio_db());
        }
        return best;
    }
    bool transition_matches(std::uint64_t start,double frequency) {
        const double bin_hz=double(c_.rate)/n_;
        const int symbols=1<<sf_;
        if(dense_short_probe()) {
            // 若预期结束后的八分之一符号仍有足够强的同相下扫频，
            // 保留它作为 SFD 过长的独立证据。
            const auto tail=project(start+2*n_+n_/4,true,frequency,n_/8,n_/4);
            if(tail.ratio_db()>=c_.peak_to_mean_db-4) {
                const double phase=2*std::numbers::pi*frequency/bin_hz;
                const auto first=project(start,true,frequency,n_).value;
                const auto second=project(start+n_,true,frequency,n_).value;
                const auto prediction=(first*std::polar(1.0,phase)+second)
                    *std::polar(double(n_/8)/(2*n_),phase*(1+double(n_/4)/n_));
                if(std::norm(prediction)>0 &&
                   std::real(tail.value*std::conj(prediction))>=.8*std::norm(prediction))return false;
            }
        }
        double wanted=0,other=0;
        for(int eighths:{15,16,17,18,19,20,24}) {
            const auto length=static_cast<std::size_t>(eighths)*n_/8;
            std::complex<double> down{};
            for(std::size_t at=0;at<length;at+=n_)
                down+=project(start+at,true,frequency,std::min(n_,length-at)).value*std::polar(1.0,-2*std::numbers::pi*frequency*at/c_.rate);
            const auto first=start+length;
            measure(first,false);
            std::vector<std::complex<float>> corrected(n_);
            const auto step=std::polar(1.0,-2*std::numbers::pi*frequency/c_.rate);
            std::complex<double> oscillator(1,0);
            for(std::size_t i=0;i<n_;++i) {
                auto value=std::complex<double>(samples_[first-base_+i]);if(inverted_)value=std::conj(value);
                corrected[i]=static_cast<std::complex<float>>(value*oscillator);oscillator*=step;
            }
            std::array<std::pair<double,int>,8> peaks{};
            for(std::size_t bin=0;bin<n_;++bin) {
                auto weakest=std::min_element(peaks.begin(),peaks.end());
                if(normalized_power(static_cast<int>(bin))>weakest->first)*weakest={normalized_power(static_cast<int>(bin)),static_cast<int>(bin)};
            }
            std::vector<int> shifts;
            for(const auto& peak:peaks)for(int delta=-1;delta<=1;++delta) {
                int shift=(static_cast<int>(std::llround(signed_bin(peak.second)-frequency/bin_hz))+delta)%symbols;
                if(shift<0)shift+=symbols;
                if(std::find(shifts.begin(),shifts.end(),shift)==shifts.end())shifts.push_back(shift);
            }
            double up=0;
            for(int shift:shifts) {
                if(n_>projection_limit_-projection_points_)throw FineBudgetExceeded{};
                projection_points_+=n_;
                const auto numerator=n_*static_cast<std::size_t>(shift);
                const auto offset=numerator/symbols,remainder=numerator%symbols;
                ProjectionKernel* kernel=nullptr;
                for(auto& item:transition_kernels_)if(!item.values.empty()&&item.frequency==double(remainder)){kernel=&item;break;}
                if(!kernel) {
                    kernel=&*std::min_element(transition_kernels_.begin(),transition_kernels_.end(),[](const auto& a,const auto& b){return a.used<b.used;});
                    kernel->frequency=double(remainder);kernel->values.resize(n_);
                    for(std::size_t i=0;i<n_;++i) {
                        const double position=(double(i)+double(remainder)/symbols)/n_;
                        const double phase=2*std::numbers::pi*c_.bandwidth*double(n_)/c_.rate*(-position/2+position*position/2);
                        kernel->values[i]=static_cast<std::complex<float>>(std::polar(1.0,-phase));
                    }
                }
                kernel->used=++kernel_clock_;
                const auto dot=[](const std::complex<float>* a,const std::complex<float>* b,std::size_t count) {
                    if(!count)return std::complex<double>{};
#ifdef SIGNALSCOPE_VOLK
                    std::complex<float> result;volk_32fc_x2_dot_prod_32fc(&result,a,b,static_cast<unsigned>(count));return std::complex<double>(result);
#else
                    std::complex<double> result{};for(std::size_t i=0;i<count;++i)result+=std::complex<double>(a[i])*std::complex<double>(b[i]);return result;
#endif
                };
                const auto split=n_-offset;
                const auto sum=dot(corrected.data(),kernel->values.data()+offset,split)+dot(corrected.data()+split,kernel->values.data(),offset);
                up=std::max(up,std::norm(sum)/n_);
            }
            const double score=std::norm(down)/length+up;
            if(eighths==18)wanted=score;else other=std::max(other,score);
        }
        return wanted>1.005*other;
    }
    bool refine_alignment(Pending& pending,double& quarter,double& after,bool& transition) {
        ++fine_attempts_;
        last_refinement_offset_=pending.offset;last_refinement_down_=pending.down_start;
        const auto radius=static_cast<std::uint64_t>(std::ceil(double(c_.rate)/c_.bandwidth));
        const auto prefix=(c_.repeat_symbols+2)*n_;
        if(pending.down_start<base_+prefix+radius || pending.down_start+4*n_+radius>expected_)return false;
        const auto first=pending.down_start-prefix;
        const auto final_limit=projection_points_+64ULL*1024*1024;
        // 为最终转换验证预留预算，不能被时频候选遍历耗尽。
        projection_limit_=projection_points_+48ULL*1024*1024;
        struct LimitGuard {std::uint64_t& value;~LimitGuard(){value=std::numeric_limits<std::uint64_t>::max();}} guard{projection_limit_};
        bool found=false;double best=-1;Pending selected;double selected_quarter=0,selected_after=0;
        try {
        std::complex<double> covariance{};auto previous=project(first,false,pending.offset,n_).value;
        for(unsigned i=1;i<c_.repeat_symbols;++i){const auto next=project(first+i*n_,false,pending.offset,n_).value;covariance+=next*std::conj(previous);previous=next;}
        if(std::norm(covariance)<1e-30)return false;
        const double fraction=std::arg(covariance)/(2*std::numbers::pi),bin_hz=double(c_.rate)/n_;
        const double refined_bin=std::round(pending.offset/bin_hz-fraction)+fraction;
        const double preamble_ratio=std::pow(10.,c_.peak_to_mean_db/10);
        for(int carrier:{0,-1,1}) {
            const double frequency=(refined_bin+carrier)*bin_hz;
            if(std::abs(frequency)>c_.search_width/2)continue;
            for(int index=0;index<=2*static_cast<int>(radius);++index) {
                const int adjustment=index==0?0:(index%2?-(index+1)/2:index/2);
                const auto down=static_cast<std::uint64_t>(static_cast<std::int64_t>(pending.down_start)+adjustment);
                const auto pre=down-prefix;
                double energy=0,magnitude=0;std::complex<double> coherent{};bool valid=true;
                // 同一前导码内的噪声会压低个别符号，保留逐符号下限，
                // 再用整体峰比和相干性检查连续前导码。
                for(unsigned i=0;i<c_.repeat_symbols;++i) {
                    const auto value=project(pre+i*n_,false,frequency,n_);
                    if(value.ratio_db()<c_.peak_to_mean_db-3){valid=false;break;}
                    energy+=value.energy;
                    magnitude+=std::norm(value.value);
                    coherent+=value.value*std::polar(1.0,-2*std::numbers::pi*frequency*n_/c_.rate*i);
                }
                if(!valid || magnitude<energy*preamble_ratio ||
                   std::norm(coherent)<.8*c_.repeat_symbols*magnitude)continue;
                ++fine_stages_[0];
                const double score=std::norm(coherent)/std::max(energy,1e-30);
                // 最终只保留相干得分最高的有效候选；较差候选无需生成同步参考。
                if(score<=best)continue;
                double neighbor=0;
                for(int side:{-4,4}){std::complex<double> sum{};const double f=frequency+side*bin_hz;for(unsigned i=0;i<c_.repeat_symbols;++i)sum+=project(pre+i*n_,false,f,n_).value*std::polar(1.0,-2*std::numbers::pi*f*n_/c_.rate*i);neighbor=std::max(neighbor,std::norm(sum));}
                if(std::norm(coherent)<std::pow(10.,.5)*std::max(neighbor,1e-30))continue;
                ++fine_stages_[1];
                const auto down1=project(down,true,frequency,n_),down2=project(down+n_,true,frequency,n_);
                const double d1=down1.ratio_db(),d2=down2.ratio_db();
                if(std::min(d1,d2)<c_.peak_to_mean_db-2)continue;
                ++fine_stages_[2];
                const auto quarter_value=project(down+2*n_,true,frequency,n_/4);
                const auto q=quarter_value.ratio_db();
                const auto a=project(down+2*n_+n_/4,true,frequency,n_/4,n_/4).ratio_db();
                if(q<c_.peak_to_mean_db-4) {
                    // 用两个完整下 Chirp 推算短段相位，弱短段须有同相证据。
                    if(q<c_.peak_to_mean_db-6)continue;
                    const auto rotation=std::polar(1.0,2*std::numbers::pi*frequency/bin_hz);
                    const auto predicted=(down1.value*rotation+down2.value)*rotation/8.0;
                    const double projected=std::real(quarter_value.value*std::conj(predicted));
                    if(projected<.866*std::abs(quarter_value.value)*std::abs(predicted) ||
                       projected<.35*std::norm(predicted))continue;
                }
                const double down_component=(std::norm(down1.value)+std::norm(down2.value))/(2*double(n_)*n_);
                const double quarter_component=std::norm(quarter_value.value)/std::pow(double(n_/4),2);
                if(quarter_component<down_component/16)continue;
                ++fine_stages_[3];
                const double first_sync=refined_sync_score(down-2*n_,frequency);
                if(first_sync<c_.peak_to_mean_db-2)continue;
                const double second_sync=refined_sync_score(down-n_,frequency);
                // 两个符号各自须有网格峰，再比较两者平均的线性峰比。
                if(second_sync<c_.peak_to_mean_db-2 ||
                   std::pow(10.,first_sync/10)+std::pow(10.,second_sync/10)<2*preamble_ratio)continue;
                ++fine_stages_[4];
                const bool use_transition=true;
                selected=pending;selected.down_start=down;selected.quarter_start=down+2*n_;selected.offset=frequency;
                selected.pre_start=std::min(selected.pre_start,pre);selected.up_db=10*std::log10(magnitude/energy);selected.down_db=std::min(d1,d2);
                selected.power=energy/(c_.repeat_symbols*double(n_));selected.component_power=std::norm(coherent)/std::pow(c_.repeat_symbols*double(n_),2);
                selected.down_component=down_component;
                best=score;selected_quarter=q;selected_after=a;transition=use_transition;found=true;
            }
        }
        }catch(const FineBudgetExceeded&){++fine_budget_exhaustions_;}
        projection_limit_=final_limit;
        if(found) {
            try {
                // 相同反向投影在前导码与 SFD 间必须真实增强，排除持续周期调制。
                double baseline=0;
                for(unsigned i=0;i<c_.repeat_symbols;++i)
                    baseline+=std::pow(10.,project(selected.down_start-prefix+i*n_,true,selected.offset,n_).ratio_db()/10);
                const double direction_gain=selected.down_db-10*std::log10(baseline/c_.repeat_symbols);
                if(direction_gain>=5)++fine_stages_[5];
                found=direction_gain>=5 && transition_matches(selected.down_start,selected.offset);
            }
            catch(const FineBudgetExceeded&){++fine_budget_exhaustions_;found=false;}
        }
        if(found){pending=selected;quarter=selected_quarter;after=selected_after;++fine_matches_;}
        return found;
    }
    void verify_pending(Json& events) {
        for(auto& p:pending_) {
            const auto fine_radius=static_cast<std::uint64_t>(std::ceil(double(c_.rate)/c_.bandwidth));
            if(!p.active || p.down_start+4*n_+fine_radius>expected_) continue;
            if(p.quarter_start<base_) { p.active=false; continue; }
            // 最终判定统一验证精细对齐，避免其他 BW/SF 的粗匹配升格。
            double quarter_db=0,after_db=0;bool fine=false,transition=false;
            fine=refine_alignment(p,quarter_db,after_db,transition);
            if(!fine && p.down_start>=n_) {
                // A repeated downchirp can seed the coarse candidate on the
                // second SFD symbol. Validate the preceding symbol boundary
                // with the same preamble, sync, down-pair and termination checks.
                auto previous=p;previous.down_start-=n_;previous.quarter_start-=n_;
                if(refine_alignment(previous,quarter_db,after_db,transition)){p=previous;fine=true;}
            }
            if(fine && events.size()<1024) {
                Json event={{"evidence_level","lora_structure_match"},{"sf_candidate",sf_},{"bandwidth_hz",c_.bandwidth},
                    {"first_observed_sample",p.pre_start},{"confirmed_at_sample",p.down_start+4*n_+fine_radius},
                    {"sfd_start_sample",p.down_start},{"sfd_end_sample",p.quarter_start+n_/4},
                    {"center_frequency_hz",c_.receiver_frequency+(inverted_?-p.offset:p.offset)},{"frequency_resolution_hz",double(c_.rate)/n_},
                    {"iq_inverted",inverted_},{"preamble_direction",inverted_?"down":"up"},
                    {"preamble_peak_to_mean_db",p.up_db},{"downchirp_peak_to_mean_db",p.down_db},
                    {"downchirp_threshold_db",c_.peak_to_mean_db-2},
                    {"fullband_power_dbfs",p.power>0?Json(10*std::log10(p.power)):Json(nullptr)},
                    {"matched_component_dbfs",p.component_power>0?Json(10*std::log10(p.component_power)):Json(nullptr)},
                    {"power_note","Dechirp peak component estimate; timing/leakage may underestimate signal power; not calibrated RF dBm"},
                    {"quarter_down_peak_to_mean_db",quarter_db},{"after_sfd_peak_to_mean_db",after_db},
                    {"fine_synchronization",fine},
                    {"sfd_end_method",transition?"joint_down_to_unknown_up":"quarter_projection_drop"},
                    {"sfd_metric_frequency_window",fine?"phase_refined_carrier_projection":"predicted_center_plus_minus_two_bins"},
                    {"score_type","uncalibrated_structure_score"},{"needs_lora_structure_validation",false},
                    {"evidence",{"Repeated upchirps","Two sync chirps on the symbol grid","Two complete downchirps","Quarter downchirp and termination"}},
                    {"protocol","unknown"}};
                const double center=c_.receiver_frequency+(inverted_?-p.offset:p.offset);
                const bool duplicate=have_last_ && std::abs(double(last_down_start_)-p.down_start)<n_/4 && std::abs(last_center_-center)<4*double(c_.rate)/n_;
                if(!duplicate) {events.push_back(std::move(event));have_last_=true;last_down_start_=p.down_start;last_center_=center;}
            }
            p.active=false;
        }
    }
    Peak measure(std::uint64_t first,bool down,int target=-1,int radius=0,bool center_constraint=false) {
        ++fft_count_;fft_points_+=n_;
        if(first<base_ || first+n_>expected_) throw std::logic_error("Detector buffer index out of bounds");
        const auto* samples=samples_.data()+static_cast<std::size_t>(first-base_);
        const bool effective_down=down!=inverted_;
        const auto* ref=effective_down?down_reference_.data():reference_.data();
#ifdef SIGNALSCOPE_VOLK
        volk_32fc_x2_multiply_conjugate_32fc(reinterpret_cast<lv_32fc_t*>(fast_in_),samples,ref,static_cast<unsigned>(n_));
#else
        for(std::size_t i=0;i<n_;++i) {
            const float re=samples[i].real(),im=samples[i].imag(),cr=ref[i].real(),ci=-ref[i].imag();
#ifdef SIGNALSCOPE_FFTW
            fast_in_[i][0]=re*cr-im*ci;
            fast_in_[i][1]=re*ci+im*cr;
#else
            in_[i]={re*cr-im*ci,re*ci+im*cr};
#endif
        }
#endif
#ifdef SIGNALSCOPE_FFTW
        fftwf_execute(fast_plan_);
#else
        kiss_fft(fft_,in_.data(),out_.data());
#endif
        const auto energy_at=static_cast<std::size_t>(first-base_);
        const double total=double(n_)*(energies_[energy_at+n_]-energies_[energy_at]);
        double maximum=0; int index=0;
#ifdef SIGNALSCOPE_VOLK
        volk_32fc_magnitude_squared_32f(magnitudes_.data(),reinterpret_cast<const lv_32fc_t*>(fast_out_),static_cast<unsigned>(n_));
        std::uint32_t maximum_index=0;
        volk_32f_index_max_32u(&maximum_index,magnitudes_.data(),static_cast<unsigned>(n_));
        index=static_cast<int>(maximum_index);maximum=magnitudes_[maximum_index];
#else
        for(std::size_t i=0;i<n_;++i) {
#ifdef SIGNALSCOPE_FFTW
            const double value=double(fast_out_[i][0])*fast_out_[i][0]+double(fast_out_[i][1])*fast_out_[i][1];
#else
            const double value=double(out_[i].r)*out_[i].r+double(out_[i].i)*out_[i].i;
#endif
            magnitudes_[i]=static_cast<float>(value);
            if(value>maximum) { maximum=value; index=static_cast<int>(i); }
        }
#endif
        if(target>=0) {
            maximum=0;
            for(int delta=-radius;delta<=radius;++delta){
                const auto candidate=target+delta;
                const int normalized=candidate<0?candidate+static_cast<int>(n_):candidate>=static_cast<int>(n_)?candidate-static_cast<int>(n_):candidate;
                // 上下峰推断的中心须落入搜索窗口，预留两 bin 的量化余量。
                if(center_constraint&&std::abs((signed_bin(normalized)+signed_bin(target))*double(c_.rate)/n_/2)>c_.search_width/2+2*double(c_.rate)/n_)continue;
                const int raw=inverted_&&normalized?static_cast<int>(n_)-normalized:normalized;
                if(magnitudes_[raw]>maximum){maximum=magnitudes_[raw];index=raw;}
            }
        }
        const auto ratio=total>0&&maximum>0?10*std::log10(maximum*n_/total):0;
        const auto concentration=total>0?maximum/total:0;
        const auto power=total/(double(n_)*n_);
        // Every strong/weak consumer first requires at least threshold - 2 dB.
        // The FFT spectrum remains available for fine synchronization, but its
        // neighborhood statistics cannot change a decision below that bound.
        if(ratio<c_.peak_to_mean_db-2) {
            if(inverted_&&index)index=static_cast<int>(n_)-index;
            return {index,ratio,concentration,power,0,0};
        }
        std::array<float,64> neighbors{};const int extent=(1<<sf_)/2;
        for(int i=0;i<32;++i){
            const int delta=4+(i+1)*(extent-4)/32;
            const auto right=index+delta,left=index-delta;
            neighbors[i*2]=magnitudes_[right>=static_cast<int>(n_)?right-static_cast<int>(n_):right];
            neighbors[i*2+1]=magnitudes_[left<0?left+static_cast<int>(n_):left];
        }
        auto upper=neighbors.begin()+48;std::nth_element(neighbors.begin(),upper,neighbors.end());
        const double prominence=maximum>0?10*std::log10(maximum/std::max<double>(*upper,1e-30)):0;
        const double near=std::max<double>(magnitudes_[index+4>=static_cast<int>(n_)?index+4-static_cast<int>(n_):index+4],
            magnitudes_[index<4?index-4+static_cast<int>(n_):index-4]);
        const double narrow=maximum>0?10*std::log10(maximum/std::max(near,1e-30)):0;
        if(inverted_&&index)index=static_cast<int>(n_)-index;
        return {index,ratio,concentration,power,prominence,narrow};
    }
    LoRaConfig c_; int sf_; bool inverted_;bool collect_css_=true;std::size_t n_=0,hop_=0;
    std::uint64_t fft_count_=0,fft_points_=0,cached_neighbors_=0;
    std::array<CachedUpPeak,64> up_peaks_{};
    std::uint64_t cached_up_windows_=0;
    std::uint64_t fine_attempts_=0,fine_matches_=0,projection_points_=0,fine_budget_exhaustions_=0;
    double last_refinement_offset_=0;std::uint64_t last_refinement_down_=0;
    // 前五项按时频候选计数，方向检查按通过同步后的最佳候选计数。
    std::array<std::uint64_t,6> fine_stages_{};
    std::uint64_t projection_limit_=std::numeric_limits<std::uint64_t>::max();
    std::uint64_t weak_candidates_=0,weak_accepted_=0,half_sample_=0;Peak half_peak_;bool have_half_=false;WeakSeed weak_seed_;
    std::uint64_t next_probe_report_=0;
    kiss_fft_cfg fft_=nullptr;
#ifdef SIGNALSCOPE_FFTW
    fftwf_complex* fast_in_=nullptr;
    fftwf_complex* fast_out_=nullptr;
    fftwf_plan fast_plan_=nullptr;
#endif
    std::vector<std::complex<float>> reference_,down_reference_;
    std::span<const std::complex<float>> samples_;
    std::span<const double> energies_;
    std::array<ProjectionKernel,4> projection_kernels_,transition_kernels_;std::uint64_t kernel_clock_=0;
    std::vector<float> magnitudes_;
#ifndef SIGNALSCOPE_FFTW
    std::vector<kiss_fft_cpx> in_,out_;
#endif
    std::array<Track,8> tracks_{};
    std::array<std::deque<Frame>,8> history_{};
    std::array<Pending,8> pending_{};
    std::array<std::uint64_t,8> armed_until_{};
    std::array<int,8> preamble_bin_{};
    std::array<Peak,8> preamble_peak_{};
    std::array<std::uint64_t,8> preamble_first_{};
    std::array<bool,8> preamble_confirmed_{};
    std::uint64_t expected_=0,base_=0,next_=0,frame_=0,active_until_=0;
    bool have_last_=false;std::uint64_t last_down_start_=0;double last_center_=0;
};
}
bool loraTimingSupported(const LoRaConfig& config) {
    if(config.sf_min<5 || config.sf_max>12 || config.sf_min>config.sf_max)return false;
    const auto widths=config.bandwidth_candidates.empty()?std::span(&config.bandwidth,1):
        std::span(config.bandwidth_candidates);
    for(const auto width:widths)
        for(int sf=config.sf_min;sf<=config.sf_max;++sf)
            if(!symbolSamples(config,sf,width))return false;
    return true;
}

class LoRaDetector::Impl {
public:
    Impl(const LoRaConfig& c,SampleFormat input_format,bool inline_processing) : config(c),format(input_format) {
        if(c.sf_min<5 || c.sf_max>12 || c.sf_min>c.sf_max || !std::isfinite(c.bandwidth) || c.bandwidth<=0 ||
           c.bandwidth>c.rate || !std::isfinite(c.search_width) || c.search_width<=0 || c.search_width+c.bandwidth>c.rate ||
           !std::isfinite(c.peak_to_mean_db) || c.peak_to_mean_db<6 || c.peak_to_mean_db>60 || c.repeat_symbols<3 || c.repeat_symbols>32 ||
           (c.time_phases!=1 && c.time_phases!=2 && c.time_phases!=4 && c.time_phases!=8) || c.workers<1 || c.workers>32 || (!c.normal_iq && !c.inverted_iq) || c.probe_stride_symbols<1 || c.probe_stride_symbols>4)
            throw std::invalid_argument("Invalid LoRa search configuration");
        const auto widths=c.bandwidth_candidates.empty()?std::vector<double>{c.bandwidth}:c.bandwidth_candidates;
        if(widths.size()>4)throw std::invalid_argument("Too many candidate bandwidths");
        for(std::size_t i=0;i<widths.size();++i) {
            const auto width=widths[i];
            if(!std::isfinite(width) || width<=0 || width+c.search_width>c.rate || std::find(widths.begin(),widths.begin()+static_cast<std::ptrdiff_t>(i),width)!=widths.begin()+static_cast<std::ptrdiff_t>(i))
                throw std::invalid_argument("Candidate bandwidth is invalid, duplicated or outside the sampled band");
            auto local=c;local.bandwidth=width;local.bandwidth_candidates.clear();
            for(int sf=c.sf_min;sf<=c.sf_max;++sf) {
                if(c.normal_iq)searches.push_back(std::make_unique<Search>(local,sf,false));
                if(c.inverted_iq)searches.push_back(std::make_unique<Search>(local,sf,true));
            }
        }
        // 大 FFT 优先领取，避免固定分组让一个工作线程拖住整块输入。
        std::stable_sort(searches.begin(),searches.end(),[](const auto& a,const auto& b){return a->symbol_samples()>b->symbol_samples();});
        for(const auto& search:searches)history_samples=std::max<std::uint64_t>(history_samples,search->symbol_samples()*(c.repeat_symbols+6));
        const auto count=inline_processing?0:std::min<std::size_t>(c.workers,searches.size());
        buckets.resize(count,Json::array());
        try {
            for(std::size_t index=0;index<count;++index) workers.emplace_back([this,index]{
                lowerAnalysisPriority();
                std::uint64_t seen=0;
                for(;;) {
                    std::span<const std::complex<float>> block;std::span<const double> energies; std::uint64_t first=0,base=0,end=0;
                    {
                        std::unique_lock lock(job_mutex);
                        job_ready.wait(lock,[&]{return stopping || generation!=seen;});
                        if(stopping) return;
                        seen=generation;block=job;energies=job_energies;first=job_first;base=raw_base;end=job_end;
                    }
                    try {
                        buckets[index]=Json::array();
                        for(auto search=next_search.fetch_add(1,std::memory_order_relaxed);search<searches.size();search=next_search.fetch_add(1,std::memory_order_relaxed))
                            // 主线程等待全部任务完成后才修改累计 events。
                            searches[search]->feed(block,energies,base,first,end,buckets[index],events.size()<256,events.size()<512);
                    } catch(...) {std::lock_guard lock(job_mutex); if(!job_error) job_error=std::current_exception();}
                    {std::lock_guard lock(job_mutex);++arrived;}
                    job_done.notify_one();
                }
            });
        } catch(...) {shutdown();throw;}
    }
    ~Impl() {shutdown();}
    void shutdown() {
        {std::lock_guard lock(job_mutex);stopping=true;}
        job_ready.notify_all();workers.clear();
    }
    void prepare(std::span<const std::uint8_t> block,std::uint64_t first,std::span<const std::complex<float>> floating={}) {
        const auto append=[&](std::complex<float> value) {
            raw_samples.push_back(value);
            raw_energies.push_back(raw_energies.back()+std::norm(value));
        };
        const auto count=floating.empty()?block.size()/bytes_per_pair(format):floating.size();
        if(!floating.empty()) {
            raw_samples.insert(raw_samples.end(),floating.begin(),floating.end());
            const auto first_energy=raw_energies.size();auto energy=raw_energies.back();
            raw_energies.resize(first_energy+floating.size());
            for(std::size_t i=0;i<floating.size();++i){energy+=std::norm(floating[i]);raw_energies[first_energy+i]=energy;}
        }
        else {
            const auto& codec=sample_codec(format);const auto stride=codec.pair_bytes;
            for(std::size_t i=0;i<block.size();i+=stride)append(codec.decode(block.data()+i));
        }
        job=raw_samples;job_energies=raw_energies;job_first=first;job_end=first+count;
    }
    void complete() {
        for(auto& bucket:buckets) {
            for(auto& event:bucket) {
                const auto level=event.value("evidence_level",std::string());
                const std::size_t limit=level=="chirp_probe"?256:level=="css_candidate"?512:1024;
                if(events.size()<limit)events.push_back(std::move(event));
                else ++omitted_events;
            }
            bucket.clear();
        }
        const auto keep_from=job_end>history_samples?job_end-history_samples:0;
        // 按大块压缩共享历史，避免每个候选模型、每个小块都搬移整个历史。
        if(keep_from>raw_base && keep_from-raw_base>=history_samples) {
            const auto removed=static_cast<std::ptrdiff_t>(keep_from-raw_base);
            raw_samples.erase(raw_samples.begin(),raw_samples.begin()+removed);raw_energies.erase(raw_energies.begin(),raw_energies.begin()+removed);raw_base=keep_from;
            // 有界地重置累计基准，保持长时间运行时的小窗口差分精度。
            if(raw_energies.front()>1000000){const auto baseline=raw_energies.front();for(auto& value:raw_energies)value-=baseline;}
        }
    }
    void process(std::span<const std::uint8_t> block,std::uint64_t first,std::span<const std::complex<float>> floating={}) {
        prepare(block,first,floating);
        if(workers.empty()) {
            for(auto& search:searches)search->feed(job,job_energies,raw_base,job_first,job_end,events,events.size()<256,events.size()<512);
        } else {
            std::unique_lock lock(job_mutex);arrived=0;next_search.store(0,std::memory_order_relaxed);job_error=nullptr;++generation;
            job_ready.notify_all();job_done.wait(lock,[&]{return arrived==workers.size();});
            if(job_error) std::rethrow_exception(job_error);
        }
        complete();
    }
    LoRaConfig config;
    const SampleFormat format;
    std::uint64_t omitted_events=0;
    std::vector<std::unique_ptr<Search>> searches;
    Json events=Json::array(); std::uint64_t pairs=0;
    std::mutex job_mutex;std::condition_variable job_ready,job_done;
    std::span<const std::complex<float>> job;std::uint64_t job_first=0,job_end=0,generation=0;
    std::span<const double> job_energies;
    std::vector<std::complex<float>> raw_samples;std::uint64_t raw_base=0,history_samples=0;
    std::vector<double> raw_energies{0};
    std::size_t arrived=0;bool stopping=false;std::exception_ptr job_error;
    std::atomic<std::size_t> next_search=0;
    std::vector<Json> buckets;std::vector<std::jthread> workers;
};
LoRaDetector::LoRaDetector(const LoRaConfig& c,SampleFormat format,bool inline_processing) : impl_(std::make_unique<Impl>(c,format,inline_processing)) {}
LoRaDetector::~LoRaDetector()=default;
void LoRaDetector::feed(std::span<const std::uint8_t> bytes,std::uint64_t first) {
    const auto stride=bytes_per_pair(impl_->format);
    if(bytes.size()%stride) throw std::invalid_argument("Detector input contains an incomplete I/Q pair");
    if(first!=impl_->pairs) throw std::runtime_error("Detector input is discontinuous");
    // 限制内部临时缓冲，与上游调用块长度解耦。
    const auto block_bytes=131072*stride;
    for(std::size_t offset=0;offset<bytes.size();offset+=block_bytes) {
        const auto part=bytes.subspan(offset,std::min(block_bytes,bytes.size()-offset));
        impl_->process(part,impl_->pairs);
        impl_->pairs+=part.size()/stride;
    }
}
Json LoRaDetector::take_events() { Json result=std::move(impl_->events); impl_->events=Json::array(); return result; }
void LoRaDetector::feedFloat(std::span<const std::complex<float>> values,std::uint64_t first) {
    if(first!=impl_->pairs)throw std::runtime_error("Detector input is discontinuous");
    for(std::size_t at=0;at<values.size();at+=131072) {
        const auto part=values.subspan(at,std::min<std::size_t>(131072,values.size()-at));
        impl_->process({},impl_->pairs,part);impl_->pairs+=part.size();
    }
}
void feedLoRaChannels(std::span<const LoRaFloatBlock> inputs,ParallelChannels& executor) {
    // Validate the entire batch before changing any detector state.
    std::size_t maximum=0,search_count=0;
    std::unordered_set<LoRaDetector*> unique;
    for(const auto& input:inputs) {
        if(!input.detector || !input.detector->impl_->workers.empty())throw std::invalid_argument("Shared LoRa dispatch requires inline detectors");
        if(input.first_sample!=input.detector->impl_->pairs)throw std::runtime_error("Detector input is discontinuous");
        if(!unique.insert(input.detector).second)throw std::invalid_argument("Duplicate detector in LoRa batch");
        maximum=std::max(maximum,input.samples.size());
        search_count+=input.detector->impl_->searches.size();
    }
    struct Job {LoRaDetector::Impl* state;std::size_t search;};
    std::vector<Job> jobs;jobs.reserve(search_count);
    for(std::size_t at=0;at<maximum;at+=131072) {
        jobs.clear();
        for(const auto& input:inputs) {
            if(at>=input.samples.size())continue;
            auto& state=*input.detector->impl_;
            const auto part=input.samples.subspan(at,std::min<std::size_t>(131072,input.samples.size()-at));
            state.prepare({},state.pairs,part);state.buckets.resize(state.searches.size());
            for(std::size_t search=0;search<state.searches.size();++search)jobs.push_back({&state,search});
        }
        // Each search owns its history, FFT buffers and result bucket. A busy
        // frequency lane can therefore use idle workers from quieter lanes.
        executor.run(jobs.size(),[&](std::size_t index){
            auto& [state,search]=jobs[index];auto& bucket=state->buckets[search];bucket=Json::array();
            state->searches[search]->feed(state->job,state->job_energies,state->raw_base,state->job_first,state->job_end,
                bucket,state->events.size()<256,state->events.size()<512);
        });
        for(const auto& input:inputs)if(at<input.samples.size()) {
            auto& state=*input.detector->impl_;state.complete();state.pairs=state.job_end;
        }
    }
}
void LoRaDetector::reset() {for(auto& search:impl_->searches)search->reset();impl_->pairs=0;impl_->omitted_events=0;impl_->events=Json::array();impl_->raw_samples.clear();impl_->raw_energies.assign(1,0);impl_->raw_base=0;}
Json LoRaDetector::results() const {
    Json work=Json::array();for(const auto& search:impl_->searches)work.push_back(search->work());
    return {{"algorithm","dechirp-fft-structure-v3.1"},{"source_pairs",impl_->pairs},{"events",impl_->events},
        {"search_work",std::move(work)},
        {"search_scheduler","dynamic_largest_first"},
        {"probe_reporting_interval_seconds",.25},{"probe_output_is_sampled",true},
        {"result_limit",1024},{"omitted_events",impl_->omitted_events},{"results_at_limit",impl_->events.size()==1024},{"time_phases",impl_->config.time_phases},
        {"workers",impl_->workers.size()},{"probe_stride_symbols",impl_->config.probe_stride_symbols},
        {"dense_short_symbols",impl_->config.dense_short_symbols},
        {"bandwidth_candidates_hz",impl_->config.bandwidth_candidates.empty()?std::vector<double>{impl_->config.bandwidth}:impl_->config.bandwidth_candidates},
#ifdef SIGNALSCOPE_FFTW
        {"fft_backend","fftw3f"},
#else
        {"fft_backend","kissfft"},
#endif
#ifdef SIGNALSCOPE_VOLK
        {"vector_backend","volk"},
#else
        {"vector_backend","scalar"},
#endif
        {"limitations",{"Scores are uncalibrated. Sync word and payload were not recovered","Each reference tracks its strongest peak. Overlapping weak signals can be masked","Sensitivity and false-alarm rate have not been established","Short real-time checks do not establish 24-hour stability"}}};
}
}
