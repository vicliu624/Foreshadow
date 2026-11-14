#include "signalscope/adapters/modulation/fsk.hpp"
#include <kiss_fft.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <deque>
#include <numbers>
#include <utility>

namespace signalscope {
namespace {
constexpr int n=512, hop=256;
constexpr int bins_per_band=8, band_count=n/bins_per_band;
struct Peak { double hz,power; std::uint64_t sample; };
struct EnvelopeFrame {
    std::uint64_t sample;
    std::array<float,band_count> power{};
};

Json broadcast_fm_events(std::span<const std::complex<float>> iq,std::uint64_t first,
                        const Config& cfg,double width,const std::array<float,n>& spectrum) {
    constexpr double low=65e6, high=110e6;
    constexpr int taps=81;
    constexpr double cutoff=100000;
    const double bin_hz=double(cfg.rate)/n;
    const int decimation=std::max(1,int(cfg.rate/350000));
    const double rate=double(cfg.rate)/decimation;
    const auto samples=std::min<std::size_t>(iq.size(),cfg.rate/50);
    const auto hz=[&](int i){return (i>=n/2?i-n:i)*bin_hz;};
    auto remaining=spectrum;
    Json events=Json::array();
    std::vector<float> background;
    for(int i=0;i<n;++i)if(std::abs(hz(i))<=width/2)background.push_back(spectrum[i]);
    if(background.empty())return events;
    auto floor=background.begin()+background.size()/4;std::nth_element(background.begin(),floor,background.end());
    const auto noise_floor=std::max(1.f,*floor);
    const auto scale=rate/(2*std::numbers::pi);
    for(int candidate=0;candidate<std::clamp(int(std::ceil(width/125000)),3,32);++candidate) {
        int bin=-1;
        for(int i=0;i<n;++i)
            if(std::abs(hz(i))<=width/2+bin_hz && cfg.frequency+hz(i)>=low &&
               cfg.frequency+hz(i)<=high && remaining[i]>(bin<0?0:remaining[bin]))bin=i;
        if(bin<0 || remaining[bin]<noise_floor*2)break;
        const double offset=hz(bin);
        for(int i=0;i<n;++i)if(std::abs(hz(i)-offset)<125000)remaining[i]=0;
        if(std::abs(offset)+cutoff>=cfg.rate/2.)continue;
        std::array<std::complex<float>,taps> filter{};
        for(int k=0;k<taps;++k) {
            const double x=k-taps/2;
            const double a=2*std::numbers::pi*cutoff*x/cfg.rate;
            const double window=.5+.5*std::cos(2*std::numbers::pi*x/(taps-1));
            filter[k]=std::polar(float(2*cutoff/cfg.rate*window),float(-2*std::numbers::pi*offset*x/cfg.rate))
                *float(x==0?1:std::sin(a)/a);
        }
        std::vector<double> deviation;
        deviation.reserve(samples/decimation);
        std::complex<float> previous{};
        const auto rotation=std::polar(1.f,float(-offset/scale));
        double power=0,mean=0;
        for(std::size_t i=0;i+taps<=samples;i+=decimation) {
            std::complex<float> z{};
            for(int k=0;k<taps;++k)z+=iq[i+k]*filter[k];
            power+=std::norm(z);
            if(i) {
                // Mixing is folded into the FIR taps. Remove the remaining carrier rotation.
                const auto phase=std::arg(z*std::conj(previous)*rotation);
                deviation.push_back(phase*scale);
                mean+=deviation.back();
            }
            previous=z;
        }
        if(deviation.size()<rate*.012)continue;
        mean/=deviation.size();
        const double center=offset+mean;
        if(std::abs(mean)>55000 || std::abs(center)>width/2)continue;
        double variance=0;
        for(auto& value:deviation) {value-=mean;variance+=value*value;}
        const double rms=std::sqrt(variance/deviation.size());
        if(rms<3500 || rms>60000)continue;
        for(std::size_t i=0;i<deviation.size();++i)
            deviation[i]*=.5-.5*std::cos(2*std::numbers::pi*i/(deviation.size()-1));
        const auto line_power=[&](double frequency) {
            const double coeff=2*std::cos(2*std::numbers::pi*frequency/rate);
            double prev=0,prev2=0;
            for(const auto value:deviation) {
                const double next=value+coeff*prev-prev2;
                prev2=prev;prev=next;
            }
            return std::max(0.,prev*prev+prev2*prev2-coeff*prev*prev2);
        };
        const double pilot=line_power(19000);
        const double neighbors=std::max({1.,line_power(18500),line_power(18750),
                                         line_power(19250),line_power(19500)});
        const double pilot_hz=4*std::sqrt(pilot)/(deviation.size()-1);
        if(pilot<neighbors*16 || pilot_hz<2200 || pilot_hz>11000 || pilot_hz<rms*.09)continue;
        if(std::any_of(events.begin(),events.end(),[&](const Json& event){return std::abs(event.at("center_frequency_hz").get<double>()-cfg.frequency-center)<20000;}))continue;
        events.push_back({{"evidence_level","broadcast_fm_stereo"},{"center_frequency_hz",cfg.frequency+center},
                {"pilot_frequency_hz",19000},{"pilot_contrast_db",10*std::log10(pilot/neighbors)},
                {"matched_component_dbfs",10*std::log10(power/(deviation.size()+1)/(128.*128))},
                {"first_observed_sample",first},{"confirmed_at_sample",first+samples}});
        for(int i=0;i<n;++i)if(std::abs(hz(i)-center)<125000)remaining[i]=0;
    }
    return events;
}

Json amplitude_event(const std::vector<Peak>& peaks,const std::deque<EnvelopeFrame>& envelope,
                     const Config& cfg,double width) {
    if(peaks.size()<16)return nullptr;
    std::array<unsigned,n> hits{};
    const double bin_hz=double(cfg.rate)/n;
    for(const auto& p:peaks) {
        const int bin=int(std::lround(p.hz/bin_hz))+n/2;
        if(bin>=0&&bin<n)++hits[bin];
    }
    const auto bin=std::max_element(hits.begin(),hits.end())-hits.begin();
    const double target=(bin-n/2)*bin_hz;
    const auto near=[&](const Peak& p){return std::abs(p.hz-target)<=bin_hz*.65;};
    const auto start=std::find_if(peaks.begin(),peaks.end(),near);
    if(start==peaks.end())return nullptr;
    const auto end=std::find_if(peaks.rbegin(),peaks.rend(),near).base();
    if(end-start<16)return nullptr;
    std::vector<double> powers;
    double frequency_sum=0;
    for(auto it=start;it!=end;++it) {
        if(near(*it)){powers.push_back(it->power);frequency_sum+=it->hz;}
    }
    if(powers.size()<16)return nullptr;
    auto ordered=powers;
    std::sort(ordered.begin(),ordered.end());
    const double high=ordered[ordered.size()*4/5],low=ordered[ordered.size()/5];
    if(high<1)return nullptr;
    for(auto it=start;it!=end;++it)
        if(!near(*it)&&it->power>high*.16)return nullptr;
    bool ask=low/high>=.045 && low/high<=.55;
    const auto frames=(end[-1].sample-start->sample)/hop+1;
    if(frames>2048)return nullptr;
    std::array<double,2048> levels{};
    for(auto it=start;it!=end;++it)if(near(*it))levels[(it->sample-start->sample)/hop]=it->power;
    const int band=((int(bin)+n/2)%n)/bins_per_band;
    const auto first_frame=std::lower_bound(envelope.begin(),envelope.end(),start->sample,
        [](const EnvelopeFrame& entry,std::uint64_t sample){return entry.sample<sample;});
    const auto validate_levels=[&](bool ask) -> unsigned {
        const double threshold=ask?(low+high)/2:high*.22;
        auto frame=first_frame;
        std::array<std::vector<float>,2> channel_levels;
        unsigned runs[2]{},counts[2]{},stable=0,stable_low=0;
        int previous=-1;
        unsigned length=0;
        for(std::size_t i=0;i<frames;++i) {
            const auto power=levels[i];
            const int state=power>threshold;
            const auto sample=start->sample+i*hop;
            while(frame!=envelope.end() && frame->sample<sample)++frame;
            if(frame!=envelope.end() && frame->sample==sample) {
                channel_levels[state].push_back(frame->power[(band+band_count-1)%band_count]+
                    frame->power[band]+frame->power[(band+1)%band_count]);
                ++frame;
            }
            if(state!=previous) {
                if(previous>=0 && length>=2)++runs[previous];
                previous=state;length=0;
            }
            ++length;++counts[state];
            const bool low_level=ask&&!state&&std::abs(power-low)<low*.45;
            stable_low+=low_level;
            if(state?std::abs(power-high)<high*.32:low_level||power<high*.06)++stable;
        }
        if(length>=2)++runs[previous];
        if(std::min(runs[0],runs[1])<3 || std::min(counts[0],counts[1])<frames*.15 ||
           stable<frames*.68 || (ask && stable_low<powers.size()*.18))return 0;
        // A missing peak can mean that energy moved to a neighboring bin. Require
        // the same high/low states to be visible in the surrounding channel power.
        const auto median=[](std::vector<float>& values) {
            const auto mid=values.begin()+values.size()/2;
            std::nth_element(values.begin(),mid,values.end());
            return *mid;
    };
    if(channel_levels[0].size()<.9*counts[0] || channel_levels[1].size()<.9*counts[1] ||
       median(channel_levels[1])<=1.5f*median(channel_levels[0]))return 0;
    return runs[0]+runs[1];
    };
    auto runs=validate_levels(ask);
    if(ask && !runs) {
        // Partial FFT windows on OOK edges can look like a second nonzero level.
        ask=false;
        runs=validate_levels(false);
    }
    if(!runs)return nullptr;
    const double center=frequency_sum/powers.size();
    if(std::abs(center)>width/2)return nullptr;
    return {{"evidence_level",ask?"ask_candidate":"ook_candidate"},{"center_frequency_hz",cfg.frequency+center},
            {"first_observed_sample",start->sample},{"confirmed_at_sample",end[-1].sample+n},
            {"amplitude_runs",runs},
            {"matched_component_dbfs",10*std::log10(high/std::pow(128.*(n-1)/2,2))}};
}
}
struct FskDetector::Impl {
    kiss_fft_cfg plan=kiss_fft_alloc(n,0,nullptr,nullptr);
    std::array<kiss_fft_cpx,n> input{},output{};
    std::array<float,n> window{},power{};
    std::array<bool,n> peak_bins{};
    std::uint32_t peak_rate=0;
    double peak_width=0;
    std::vector<Peak> amplitude_history;
    std::deque<EnvelopeFrame> envelope_history;
    std::uint64_t next_sample=0,last_amplitude_end=0,next_fm_scan=0;
    Json previous_fm=Json::array(),confirmed_fm=Json::array();
    const SampleFormat format;
    const NarrowbandSelection selection;
    const bool exclude_dc;
    Impl(SampleFormat input_format,NarrowbandSelection modes,bool exclude):format(input_format),selection(modes),exclude_dc(exclude) {
        if(!plan)throw std::bad_alloc();
        for(int i=0;i<n;++i)window[i]=float(.5-.5*std::cos(2*std::numbers::pi*i/(n-1)));
    }
    ~Impl(){kiss_fft_free(plan);}
};
FskDetector::FskDetector(SampleFormat format,NarrowbandSelection selection,bool exclude_dc)
    :impl_(std::make_unique<Impl>(format,selection,exclude_dc)){}
FskDetector::~FskDetector()=default;
FskDetector::Detections FskDetector::detectAll(std::span<const std::uint8_t> iq,std::uint64_t first,const Config& cfg,double width) {
    Json firstFm;
    auto candidate=detect(iq,first,cfg,width,firstFm);
    return {std::move(candidate),std::exchange(impl_->confirmed_fm,Json::array())};
}

namespace {
void appendBroadcastFmEvent(Json& events,Json event,std::uint32_t rate) {
    if(!events.empty() &&
       std::abs(events.back()["center_frequency_hz"].get<double>()-event["center_frequency_hz"].get<double>())<20000 &&
       event["first_observed_sample"].get<std::uint64_t>()<=events.back()["confirmed_at_sample"].get<std::uint64_t>()+rate) {
        event["first_observed_sample"]=events.back()["first_observed_sample"];
        events.back()=std::move(event);
    } else {
        events.push_back(std::move(event));
        if(events.size()>256)events.erase(events.begin());
    }
}
bool overlaps_confirmed_fm(const Json& event,const Json& station,std::uint32_t rate) {
    return event["evidence_level"]=="fsk_candidate" &&
        std::abs(event["center_frequency_hz"].get<double>()-station["center_frequency_hz"].get<double>())<18000 &&
        event["first_observed_sample"].get<std::uint64_t>()<=station["confirmed_at_sample"].get<std::uint64_t>()+rate/3 &&
        station["first_observed_sample"].get<std::uint64_t>()<=event["confirmed_at_sample"].get<std::uint64_t>()+rate/3;
}
}

void NarrowbandEvents::append(Json event,Json fm,std::uint32_t rate) {
    // Adjacent analysis channels overlap; merge the same RF/time observation
    // even when another channel published between the two copies.
    const auto merge=[&](Json& list,const Json& incoming,double tolerance) {
        if(incoming.is_null())return false;
        for(auto& previous:list) {
            if(previous.at("evidence_level")!=incoming.at("evidence_level") ||
               std::abs(previous.at("center_frequency_hz").get<double>()-incoming.at("center_frequency_hz").get<double>())>=tolerance)continue;
            const auto a=previous.at("first_observed_sample").get<std::uint64_t>(),b=previous.at("confirmed_at_sample").get<std::uint64_t>();
            const auto c=incoming.at("first_observed_sample").get<std::uint64_t>(),d=incoming.at("confirmed_at_sample").get<std::uint64_t>();
            if(c<=b+rate/25 && a<=d+rate/25) {
                previous["first_observed_sample"]=std::min(a,c);previous["confirmed_at_sample"]=std::max(b,d);return true;
            }
        }
        return false;
    };
    if(!fm.is_null() && merge(fm_,fm,20000))fm=nullptr;
    if(!event.is_null() && merge(events_,event,2500))event=nullptr;
    if(!fm.is_null()) {
        appendBroadcastFmEvent(fm_,std::move(fm),rate);
        const auto& station=fm_.back();
        const auto end=std::remove_if(events_.begin(),events_.end(),
            [&](const Json& candidate){return overlaps_confirmed_fm(candidate,station,rate);});
        events_.erase(end,events_.end());
    }
    if(event.is_null() || std::any_of(fm_.begin(),fm_.end(),
        [&](const Json& station){return overlaps_confirmed_fm(event,station,rate);}))return;
    if(!events_.empty()) {
        auto& last=events_.back();
        if(last["evidence_level"]==event["evidence_level"] &&
           std::abs(last["center_frequency_hz"].get<double>()-event["center_frequency_hz"].get<double>())<2500 &&
           event["first_observed_sample"].get<std::uint64_t>()<last["confirmed_at_sample"].get<std::uint64_t>()+rate/25) {
            last["confirmed_at_sample"]=event["confirmed_at_sample"];
            return;
        }
    }
    events_.push_back(std::move(event));
    if(events_.size()>16)events_.erase(events_.begin());
}

void NarrowbandEvents::publish(Json& status) const {
    status["recent_fsk_events"]=Json::array();
    status["recent_ask_events"]=Json::array();
    status["recent_fm_events"]=fm_;
    for(const auto& event:events_)
        status[event["evidence_level"]=="fsk_candidate"?"recent_fsk_events":"recent_ask_events"].push_back(event);
}

Json FskDetector::detect(std::span<const std::uint8_t> iq,std::uint64_t first,const Config& cfg,double search_width,
                         Json& broadcast_fm) {
    auto& w=*impl_;
    broadcast_fm=nullptr;w.confirmed_fm=Json::array();
    const bool amplitude=w.selection.ook || w.selection.ask;
    const bool track_peaks=w.selection.fsk || amplitude;
    const bool scan_fm=w.selection.broadcast_fm && overlaps_broadcast_fm(cfg.frequency,search_width)
        && first>=w.next_fm_scan;
    if(!track_peaks && !scan_fm)return nullptr;

    std::vector<Peak> peaks;
    const double bin_hz=double(cfg.rate)/n;
    if(track_peaks && (w.peak_rate!=cfg.rate || w.peak_width!=search_width)) {
        // The search band is the same for all FFT windows at this tuning.
        for(int i=0;i<n;++i) {
            const double offset=std::abs((i>=n/2?i-n:i)*bin_hz);
            w.peak_bins[i]=(!w.exclude_dc || offset>bin_hz) && offset<=search_width/2+15000;
        }
        w.peak_rate=cfg.rate;
        w.peak_width=search_width;
    }
    const auto stride=bytes_per_pair(w.format);
    const auto samples=std::min<std::size_t>(iq.size()/stride,65536);
    const auto& codec=sample_codec(w.format);
    std::vector<std::complex<float>> decoded;decoded.reserve(samples);
    for(std::size_t i=0;i<samples;++i)decoded.push_back(codec.decode(iq.data()+i*stride)*128.f);
    if(first!=w.next_sample) {
        w.amplitude_history.clear();
        w.envelope_history.clear();
    }
    w.next_sample=first+iq.size()/stride;
    const auto limit=w.next_sample>cfg.rate/7?w.next_sample-cfg.rate/7:0;
    while(!w.envelope_history.empty() && w.envelope_history.front().sample<limit)
        w.envelope_history.pop_front();
    const bool collect_spectrum=w.selection.fsk || scan_fm;
    std::array<float,n> average_power{};
    for(std::size_t at=0;at+n<=samples;at+=hop) {
        for(int i=0;i<n;++i) {
            const auto value=decoded[at+i]*w.window[i];
            w.input[i]={value.real(),value.imag()};
        }
        kiss_fft(w.plan,w.input.data(),w.output.data());
        if(!track_peaks) {
            for(int i=0;i<n;++i) {
                const auto z=w.output[i];
                const float power=z.r*z.r+z.i*z.i;
                average_power[i]+=power;
            }
            continue;
        }
        auto* envelope=amplitude?&w.envelope_history.emplace_back(EnvelopeFrame{first+at}):nullptr;
        float total=0,maximum=0;int bin=0;
        for(int i=0;i<n;++i) {
            const auto z=w.output[i];
            const float power=z.r*z.r+z.i*z.i;
            w.power[i]=power;total+=power;
            if(collect_spectrum)average_power[i]+=power;
            if(envelope)envelope->power[i/bins_per_band]+=power;
            if(w.peak_bins[i] && power>maximum) {
                maximum=power;bin=i;
            }
        }
        if(maximum<total*.075f || maximum<1.f)continue;
        const auto a=std::log(std::max(1.f,w.power[(bin+n-1)%n])),b=std::log(maximum);
        const auto c=std::log(std::max(1.f,w.power[(bin+1)%n]));
        const double sub=std::clamp(.5*(a-c)/std::min(-1e-6,double(a-2*b+c)),-.5,.5);
        peaks.push_back({((bin>=n/2?bin-n:bin)+sub)*bin_hz,maximum,at});
    }
    if(scan_fm) {
        w.next_fm_scan=first+cfg.rate/6;
        auto events=broadcast_fm_events(decoded,first,cfg,search_width,average_power);
        auto& previous=w.previous_fm;
        previous.erase(std::remove_if(previous.begin(),previous.end(),[&](const Json& event){return event.at("confirmed_at_sample").get<std::uint64_t>()+cfg.rate<first;}),previous.end());
        for(auto& event:events) {
            auto earlier=std::find_if(previous.begin(),previous.end(),[&](const Json& old){return std::abs(event.at("center_frequency_hz").get<double>()-old.at("center_frequency_hz").get<double>())<20000;});
            if(earlier!=previous.end()) {
                event["first_observed_sample"]=earlier->at("first_observed_sample");
                w.confirmed_fm.push_back(event);*earlier=event;
            } else previous.push_back(event);
        }
        if(!w.confirmed_fm.empty())broadcast_fm=w.confirmed_fm.front();
    }
    if(amplitude) {
        const auto keep=std::lower_bound(w.amplitude_history.begin(),w.amplitude_history.end(),limit,
                                         [](const Peak& peak,std::uint64_t value){return peak.sample<value;});
        w.amplitude_history.erase(w.amplitude_history.begin(),keep);
        for(auto peak:peaks) {
            peak.sample+=first;
            w.amplitude_history.push_back(peak);
        }
    }
    // Track short, contiguous narrowband activity. Require repeated visits to both frequency states.
    for(std::size_t begin=0;w.selection.fsk && begin<peaks.size();) {
        std::size_t end=begin+1;
        while(end<peaks.size() && peaks[end].sample-peaks[end-1].sample<=3*hop &&
              std::abs(peaks[end].hz-peaks[end-1].hz)<24000)++end;
        const auto size=end-begin;
        if(size>=16) {
            std::vector<double> frequencies;
            for(auto i=begin;i<end;++i)frequencies.push_back(peaks[i].hz);
            std::sort(frequencies.begin(),frequencies.end());
            const double low=frequencies[size/5],high=frequencies[4*size/5],separation=high-low,center=(low+high)/2;
            if(separation>=4000 && separation<=22000 && std::abs(center)<=search_width/2) {
                unsigned counts[2]{},switches=0; int last=-1;
                double power=0,low_power=0,high_power=0;
                for(auto i=begin;i<end;++i) {
                    const auto& peak=peaks[i];
                    const int state=peak.hz>center;
                    if(std::abs(peak.hz-(state?high:low))>std::max(650.,separation*.14))continue;
                    ++counts[state];power+=peak.power;
                    (state?high_power:low_power)+=peak.power;
                    if(last>=0 && last!=state)++switches;
                    last=state;
                }
                const auto accepted=counts[0]+counts[1];
                if(accepted>=size*.68 && std::min(counts[0],counts[1])>=size*.16 && switches>=5) {
                    const auto lo=low_power/counts[0],hi=high_power/counts[1];
                    double core=0,skirts=0;
                    for(int i=0;i<n;++i) {
                        const double distance=std::abs((i>=n/2?i-n:i)*bin_hz-center);
                        if(distance<=separation/2+1.5*bin_hz)core+=average_power[i];
                        else if(distance<=100000)skirts+=average_power[i];
                    }
                    // Broadcast FM may visit two apparent peaks, while occupying much wider skirts.
                    if(core>=skirts*12 && std::min(lo,hi)>=std::max(lo,hi)*.18) {
                        return {{"evidence_level","fsk_candidate"},{"center_frequency_hz",cfg.frequency+center},
                            {"tone_separation_hz",separation},{"first_observed_sample",first+peaks[begin].sample},
                            {"confirmed_at_sample",first+peaks[end-1].sample+n},{"state_switches",switches},
                            {"matched_component_dbfs",10*std::log10(power/accepted/std::pow(128.*(n-1)/2,2))}};
                    }
                }
            }
        }
        begin=end;
    }
    if(!amplitude)return nullptr;
    auto event=amplitude_event(w.amplitude_history,w.envelope_history,cfg,search_width);
    if(!event.is_null()) {
        if(event["evidence_level"]=="ook_candidate"?!w.selection.ook:!w.selection.ask)return nullptr;
        const auto end=event["confirmed_at_sample"].get<std::uint64_t>();
        if(end<=w.last_amplitude_end)return nullptr;
        w.last_amplitude_end=end;
    }
    return event;
}
}
