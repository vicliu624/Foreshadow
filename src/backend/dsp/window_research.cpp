#include "signalscope/backend/dsp/window_research.hpp"
#include <kiss_fft.h>
#include <bit>
#include <limits>
#include <numbers>

namespace signalscope {
namespace {
struct Piece {std::uint64_t first;std::span<const std::complex<float>> samples;bool discontinuous;};
struct Run {std::size_t first_piece,last_piece;std::uint64_t first,end;};
double weight(ResearchWindow window,std::size_t i,std::size_t size) {
    const auto phase=2*std::numbers::pi*i/size;
    switch(window) {
    case ResearchWindow::rectangular:return 1;
    case ResearchWindow::hann:return .5-.5*std::cos(phase);
    case ResearchWindow::hamming:return .54-.46*std::cos(phase);
    case ResearchWindow::blackman:return .42-.5*std::cos(phase)+.08*std::cos(2*phase);
    }
    throw std::invalid_argument("Unknown research window function");
}
const char* window_name(ResearchWindow window) {
    switch(window) {
    case ResearchWindow::rectangular:return "rectangular";
    case ResearchWindow::hann:return "hann";
    case ResearchWindow::hamming:return "hamming";
    case ResearchWindow::blackman:return "blackman";
    }
    throw std::invalid_argument("Unknown research window function");
}
struct Fft {
    kiss_fft_cfg plan=nullptr;
    explicit Fft(std::size_t size):plan(kiss_fft_alloc(static_cast<int>(size),0,nullptr,nullptr)){if(!plan)throw std::bad_alloc();}
    ~Fft(){kiss_fft_free(plan);}
};
}
std::size_t WindowResearch::payload_bytes() const {
    return frame_first_samples.capacity()*sizeof(std::uint64_t)+spectral_power.capacity()*sizeof(float)+
        average_spectral_power.capacity()*sizeof(double)+frequency_histogram.counts.capacity()*sizeof(std::uint64_t)+correlation.capacity()*sizeof(CorrelationPoint);
}
WindowResearch research_signal_window(std::shared_ptr<const WaveformAnalysis> analysis,SignalSampleRange range,
    const WindowResearchOptions& options,std::stop_token stop) {
    if(!analysis || (!analysis->source && !analysis->processed) || !analysis->sample_count() || !range.count || range.first>UINT64_MAX-range.count ||
       range.first<analysis->first_sample() || range.first+range.count>analysis->end_sample())throw std::invalid_argument("Invalid research selection");
    if(options.fft_size<16 || options.fft_size>65536 || !std::has_single_bit(options.fft_size) || !options.hop_samples ||
       options.hop_samples>options.fft_size*8 || !options.maximum_frames || options.maximum_frames>65536 ||
       !options.frequency_histogram_bins || options.frequency_histogram_bins>4096 || options.correlation_lags>65536 ||
       (options.scaling!=SpectralScaling::power_per_bin && options.scaling!=SpectralScaling::power_density))
        throw std::invalid_argument("Invalid complete window research configuration");
    const auto name=window_name(options.window);
    const auto fft=options.fft_size,hop=options.hop_samples;
    // Query the actual FFT plan allocation before admission, in addition to
    // the input/output arrays, coefficients and borrowed span metadata.
    std::size_t plan_bytes=0;(void)kiss_fft_alloc(static_cast<int>(fft),0,nullptr,&plan_bytes);
    const auto source_segments=analysis->processed?analysis->processed->segments.size():analysis->source->segments.size();
    const auto fft_workspace=std::size_t(fft)*(2*sizeof(kiss_fft_cpx)+sizeof(double))+plan_bytes;
    if(fft_workspace>options.workspace_payload_bytes || source_segments>(options.workspace_payload_bytes-fft_workspace)/(sizeof(Piece)+sizeof(Run)))
        throw std::length_error("Research FFT and span workspace exceed their budget");
    const auto workspace=fft_workspace+source_segments*(sizeof(Piece)+sizeof(Run));
    if(workspace>options.workspace_payload_bytes)throw std::length_error("Research FFT workspace exceeds its budget");
    WindowResearch result;result.analysis=std::move(analysis);result.range=range;result.options=options;
    const auto rate=result.analysis->sample_rate();const bool density=options.scaling==SpectralScaling::power_density;
    result.method={{"id","window-research"},{"version",1},{"window",name},{"window_periodic",true},{"fft_size",fft},{"hop_samples",hop},
        {"sidedness","two-sided-complex"},{"bin_order","fftshift"},{"frequency_bin_width_hz",double(rate)/fft},
        {"scaling",density?"power-density":"power-per-bin"},{"unit",density?"normalized-power/Hz":"normalized-power/bin"},
        {"db_reference_power",1},{"detrend","none"},{"frame_timestamp","first+(N-1)/2"},{"boundary_policy","complete-observed-support"},
        {"statistics_scope","all-observed-selected-samples"},{"spectral_average_scope","complete-selected-frames"},
        {"correlation","mean-of-available-pairs-no-dc-removal"},{"source_clock",result.analysis->processed?Json{{"source_origin",std::to_string(result.analysis->processed->clock.source_origin)},
            {"source_rate_hz",result.analysis->processed->clock.source_rate},{"rate_hz",rate}}:Json{{"rate_hz",rate},{"identity",true}}}};
    if(stop.stop_requested())return result;
    std::vector<Piece> pieces;std::vector<Run> runs;pieces.reserve(source_segments);runs.reserve(source_segments);const auto end=range.first+range.count;
    if(fft_workspace+pieces.capacity()*sizeof(Piece)+runs.capacity()*sizeof(Run)>options.workspace_payload_bytes)
        throw std::length_error("Allocated research span workspace exceeds its budget");
    const auto append=[&](const auto& segments) {
        for(const auto& segment:segments) {
            const auto first=std::max(range.first,segment.first_sample),last=std::min(end,segment.first_sample+segment.samples.size());if(first>=last)continue;
            const auto position=pieces.size();pieces.push_back({first,std::span(segment.samples).subspan(static_cast<std::size_t>(first-segment.first_sample),static_cast<std::size_t>(last-first)),segment.discontinuous_before && first==segment.first_sample});
            if(!runs.empty() && runs.back().end==first && !pieces.back().discontinuous){runs.back().last_piece=position;runs.back().end=last;}
            else runs.push_back({position,position,first,last});
        }
    };
    if(result.analysis->processed)append(result.analysis->processed->segments);else append(result.analysis->source->segments);
    std::uint64_t frame_count=0,pair_count=0,observed=0;
    for(const auto& run:runs) {
        const auto count=run.end-run.first;observed+=count;
        if(count>=fft)frame_count+=1+(count-fft)/hop;
        const auto lags=std::min<std::uint64_t>(options.correlation_lags,count);
        // Sum(count-lag), lag in [0,lags), without an overflowing product.
        if(lags && count>UINT64_MAX/lags)throw std::length_error("Research correlation exceeds its operation budget");
        const auto pairs=count*lags-lags*(lags-1)/2;
        if(pairs>options.maximum_correlation_pairs-pair_count)throw std::length_error("Research correlation exceeds its operation budget");
        pair_count+=pairs;
    }
    if(frame_count>options.maximum_frames)throw std::length_error("Research frame count exceeds its budget");
    const auto fixed=std::size_t(fft)*sizeof(double)+std::size_t(options.frequency_histogram_bins)*sizeof(std::uint64_t)+std::size_t(options.correlation_lags)*sizeof(CorrelationPoint);
    if(fixed>options.result_payload_bytes || frame_count>(options.result_payload_bytes-fixed)/(std::size_t(fft)*sizeof(float)+sizeof(std::uint64_t)))throw std::length_error("Research matrix exceeds its result budget");
    result.frame_first_samples.resize(static_cast<std::size_t>(frame_count));result.spectral_power.resize(static_cast<std::size_t>(frame_count)*fft);result.average_spectral_power.resize(fft);
    result.frequency_histogram.counts.resize(options.frequency_histogram_bins);result.correlation.resize(options.correlation_lags);
    if(result.payload_bytes()>options.result_payload_bytes)throw std::length_error("Allocated research results exceed their budget");
    auto& stats=result.statistics;stats.observed_samples=observed;stats.missing_clock_positions=range.count-observed;
    stats.minimum_amplitude=observed?std::numeric_limits<double>::infinity():0;
    const auto& frequency_trace=result.analysis->trace("frequency");validate_analysis_trace(*result.analysis,frequency_trace);const auto& frequencies=frequency_trace.values;
    double mean_amplitude=0,amplitude_m2=0,frequency_m2=0,minimum_frequency=std::numeric_limits<double>::infinity(),maximum_frequency=-minimum_frequency;
    std::uint64_t sample_number=0;
    for(const auto& piece:pieces)for(std::size_t i=0;i<piece.samples.size();++i) {
        if((sample_number&4095)==0 && stop.stop_requested())return WindowResearch{};
        const std::complex<double> sample=piece.samples[i];const auto amplitude=std::abs(sample);++sample_number;
        stats.mean_iq+=(sample-stats.mean_iq)/double(sample_number);stats.mean_power+=(std::norm(sample)-stats.mean_power)/double(sample_number);
        stats.minimum_amplitude=std::min(stats.minimum_amplitude,amplitude);stats.maximum_amplitude=std::max(stats.maximum_amplitude,amplitude);
        const auto delta=amplitude-mean_amplitude;mean_amplitude+=delta/sample_number;amplitude_m2+=delta*(amplitude-mean_amplitude);
        const auto at=analysis_trace_value_index(*result.analysis,frequency_trace,piece.first+i);
        // A derivative at the first selected point requires a predecessor
        // outside this selection; exclude it from selected-window statistics.
        const auto frequency=(!at || (i==0 && piece.first==range.first))?std::numeric_limits<double>::quiet_NaN():frequencies[*at];
        if(std::isfinite(frequency)) {
            ++stats.valid_frequency_samples;const auto df=frequency-stats.mean_frequency_hz;stats.mean_frequency_hz+=df/stats.valid_frequency_samples;frequency_m2+=df*(frequency-stats.mean_frequency_hz);
            minimum_frequency=std::min(minimum_frequency,frequency);maximum_frequency=std::max(maximum_frequency,frequency);
        }
    }
    stats.amplitude_standard_deviation=observed?std::sqrt(std::max(0.,amplitude_m2/observed)):0;
    stats.frequency_standard_deviation_hz=stats.valid_frequency_samples?std::sqrt(std::max(0.,frequency_m2/stats.valid_frequency_samples)):0;
    if(stats.valid_frequency_samples) {
        auto& histogram=result.frequency_histogram;const auto padding=maximum_frequency==minimum_frequency?std::max(1e-9,double(rate)*1e-9):0;
        histogram.low_hz=minimum_frequency-padding;histogram.high_hz=maximum_frequency+padding;
        for(const auto& piece:pieces)for(std::size_t i=0;i<piece.samples.size();++i) {
            if((i&4095)==0 && stop.stop_requested())return WindowResearch{};
            if(i==0 && piece.first==range.first)continue;
            const auto at=analysis_trace_value_index(*result.analysis,frequency_trace,piece.first+i);if(!at)continue;const auto frequency=frequencies[*at];
            if(std::isfinite(frequency)) {
                const auto bin=std::min<std::size_t>(histogram.counts.size()-1,static_cast<std::size_t>(std::clamp((frequency-histogram.low_hz)/(histogram.high_hz-histogram.low_hz),0.,1.)*histogram.counts.size()));++histogram.counts[bin];
            }
        }
    }
    // Runs are borrowed spans. Locate within a run without flattening a large
    // selection or crossing an unknown adjacent discontinuity.
    const auto value_at=[&](const Run& run,std::uint64_t sample) {
        const auto begin=pieces.begin()+static_cast<std::ptrdiff_t>(run.first_piece),finish=pieces.begin()+static_cast<std::ptrdiff_t>(run.last_piece+1);
        const auto it=std::upper_bound(begin,finish,sample,[](auto position,const Piece& piece){return position<piece.first;});
        const auto& piece=*std::prev(it);return std::complex<double>(piece.samples[static_cast<std::size_t>(sample-piece.first)]);
    };
    for(std::uint32_t lag=0;lag<options.correlation_lags;++lag) {
        if(stop.stop_requested())return WindowResearch{};
        auto& point=result.correlation[lag];point.lag_samples=lag;std::complex<double> sum{};
        for(const auto& run:runs)if(lag<run.end-run.first)for(auto sample=run.first;sample<run.end-lag;++sample){
            if((point.pairs&4095)==0 && stop.stop_requested())return WindowResearch{};
            sum+=value_at(run,sample+lag)*std::conj(value_at(run,sample));++point.pairs;
        }
        if(point.pairs)point.value=sum/double(point.pairs);
    }
    std::vector<double> window(fft);double window_energy=0,window_sum=0;
    for(std::size_t i=0;i<fft;++i){window[i]=weight(options.window,i,fft);window_energy+=window[i]*window[i];window_sum+=window[i];}
    result.method["window_energy"]=window_energy;result.method["window_coherent_gain"]=window_sum/fft;
    result.method["equivalent_noise_bandwidth_hz"]=rate*window_energy/(window_sum*window_sum);
    result.method["fft_normalization"]=density?"abs(DFT(x*w))^2/(Fs*sum(w^2))":"abs(DFT(x*w))^2/(N*sum(w^2))";
    if(!frame_count)return result;
    Fft transform(fft);std::vector<kiss_fft_cpx> input(fft),output(fft);std::size_t frame=0;
    if(plan_bytes+(input.capacity()+output.capacity())*sizeof(kiss_fft_cpx)+window.capacity()*sizeof(double)+pieces.capacity()*sizeof(Piece)+runs.capacity()*sizeof(Run)>options.workspace_payload_bytes)
        throw std::length_error("Allocated research FFT workspace exceeds its budget");
    const auto normalization=window_energy*(density?double(rate):double(fft));
    for(const auto& run:runs)if(run.end-run.first>=fft) {
        const auto last=run.end-fft;for(auto first=run.first;;) {
            if(stop.stop_requested())return WindowResearch{};
            result.frame_first_samples[frame]=first;
            for(std::size_t i=0;i<fft;++i){const auto value=value_at(run,first+i)*window[i];input[i]={static_cast<float>(value.real()),static_cast<float>(value.imag())};}
            kiss_fft(transform.plan,input.data(),output.data());
            for(std::size_t bin=0;bin<fft;++bin) {
                const auto value=output[(bin+fft/2)%fft];const auto power=(double(value.r)*value.r+double(value.i)*value.i)/normalization;
                result.spectral_power[frame*fft+bin]=static_cast<float>(power);result.average_spectral_power[bin]+=power/double(frame_count);
            }
            ++frame;if(last-first<hop)break;first+=hop;
        }
    }
    return result;
}
}
