#include "detector.hpp"
#include "pn.hpp"
#include "../common/snapshot_fft.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <numbers>

namespace signalscope::dtmb {
namespace {
using Complex=std::complex<float>;
using Sum=std::complex<double>;
constexpr double tau=2*std::numbers::pi,windowSeconds=.012,channelHz=8e6,occupiedHz=symbolRate*1.05;
constexpr double minimumCoherence=.16;
const TextCatalog& catalog(){static const auto value=TextCatalog::resource("dtmb-detection");return value;}
Json text(const char* key){return {{"message_key",key}};}

// Fractional timing is searched at four samples/chip. A phase table avoids
// trigonometric work in the inner loop; input already fits its Nyquist band.
std::vector<Complex> interpolate(std::span<const Complex> input,double rate) {
    static const auto weights=[] {
        std::array<std::array<float,20>,1024> table{};
        for(std::size_t phase=0;phase<table.size();++phase) {
            double sum=0;
            for(int tap=0;tap<20;++tap) {
                const auto x=phase/1024.-(tap-9);
                const auto sinc=std::abs(x)<1e-12?1.:std::sin(std::numbers::pi*x)/(std::numbers::pi*x);
                table[phase][tap]=static_cast<float>(sinc*(.5+.5*std::cos(std::numbers::pi*x/11)));sum+=table[phase][tap];
            }
            for(auto& weight:table[phase])weight/=static_cast<float>(sum);
        }
        return table;
    }();
    const auto count=static_cast<std::size_t>(input.size()*4.*symbolRate/rate);
    if(count<40)return {};
    std::vector<Complex> result(count-40);
    for(std::size_t i=0;i<result.size();++i) {
        const auto position=i*rate/(4.*symbolRate);const auto whole=static_cast<std::ptrdiff_t>(position);
        const auto phase=std::min<std::size_t>(1023,static_cast<std::size_t>((position-whole)*1024));
        Complex value{};
        for(int tap=0;tap<20;++tap) {
            const auto index=std::clamp<std::ptrdiff_t>(whole+tap-9,0,static_cast<std::ptrdiff_t>(input.size()-1));
            value+=input[index]*weights[phase][tap];
        }
        result[i]=value;
    }
    return result;
}
struct Hit {double chip,coherence,offset;};
struct Match {HeaderMode mode;std::vector<Hit> frames;};
double quality(Sum sum,double energy,std::size_t count){return std::norm(sum)/std::max(1e-30,count*energy);}
std::size_t cyclic(std::ptrdiff_t value,std::size_t period){return static_cast<std::size_t>((value%static_cast<std::ptrdiff_t>(period)+period)%period);}

std::vector<Match> acquire(std::span<const Complex> input,double rate) {
    const auto samples=interpolate(input,rate);
    if(samples.size()<4*(4*(945+bodySymbols)+2048))return {};
    const auto fftSize=std::bit_ceil(samples.size()/4+256);
    snapshot::Fft forward(fftSize),inverse(fftSize,true);
    std::array<std::vector<float>,3> references;
    std::array<std::vector<Complex>,3> spectra;
    for(std::size_t modeIndex=0;modeIndex<3;++modeIndex) {
        const auto mode=headerModes[modeIndex];auto pn=pnSequence(mode.order,mode.taps,mode.initial,mode.period);
        auto& reference=references[modeIndex];reference.resize(mode.period);
        for(std::size_t i=0;i<mode.period;++i)reference[i]=pn[i]*pn[(i+1)%mode.period];
        for(std::size_t i=0;i<fftSize;++i)forward.set(i,{i<mode.match_symbols?reference[mode.match_symbols-1-i]:0.f,0});
        forward.run();spectra[modeIndex].resize(fftSize);
        for(std::size_t i=0;i<fftSize;++i)spectra[modeIndex][i]=forward.value(i);
    }
    std::array<std::vector<Hit>,3> hits;
    for(unsigned phase=0;phase<4;++phase) {
        const auto count=(samples.size()-phase)/4-1;
        std::vector<Complex> differences(count);std::vector<double> energy(count+1);
        for(std::size_t i=0;i<count;++i) {
            differences[i]=samples[phase+4*(i+1)]*std::conj(samples[phase+4*i]);
            energy[i+1]=energy[i]+std::norm(differences[i]);
        }
        for(std::size_t i=0;i<fftSize;++i)forward.set(i,i<count?differences[i]:Complex{});
        forward.run();
        for(std::size_t modeIndex=0;modeIndex<3;++modeIndex) {
            const auto mode=headerModes[modeIndex];const auto& reference=references[modeIndex];
            const auto length=mode.symbols-1,match=mode.match_symbols;
            for(std::size_t i=0;i<fftSize;++i)inverse.set(i,forward.value(i)*spectra[modeIndex][i]);
            inverse.run();
            std::vector<double> scores(count-match+1);
            for(std::size_t i=0;i<scores.size();++i)
                scores[i]=quality(Sum(inverse.value(i+match-1))/double(fftSize),energy[i+match]-energy[i],match);
            std::vector<std::size_t> peaks;
            for(std::size_t i=mode.symbols;i+mode.symbols+1024<count;++i)
                if(scores[i]>=minimumCoherence && scores[i]>=scores[i-1] && scores[i]>scores[i+1])peaks.push_back(i);
            // Bounded acquisition work for adversarial repetitive waveforms.
            if(peaks.size()>512) {
                std::partial_sort(peaks.begin(),peaks.begin()+512,peaks.end(),[&](auto a,auto b){return scores[a]>scores[b];});peaks.resize(512);
            }
            for(const auto anchor:peaks) {
                auto first=anchor;Sum bestSum{};double best=0;
                if(mode.symbols==595) {
                    for(std::size_t k=0;k<length;++k)bestSum+=Sum(differences[first+k])*double(reference[k]);
                    best=quality(bestSum,energy[first+length]-energy[first],length);
                }else {
                    const auto low=anchor-mode.symbols+match,high=anchor+mode.symbols;
                    std::vector<Sum> sums(high-low+1);
                    for(auto i=low;i<high;++i)sums[i-low+1]=sums[i-low]+Sum(differences[i])*double(reference[cyclic(static_cast<std::ptrdiff_t>(i)-anchor,mode.period)]);
                    for(auto start=low;start+length<=high;++start) {
                        const auto sum=sums[start-low+length]-sums[start-low];
                        const auto q=quality(sum,energy[start+length]-energy[start],length);
                        if(q>best){best=q;first=start;bestSum=sum;}
                    }
                }
                if(best<minimumCoherence)continue;
                // A continuous m-sequence is not a DTMB frame: the expected
                // 3780-symbol body must interrupt the PN train.
                const auto body=first+mode.symbols+512;Sum outside{};
                if(body+256>=count)continue;
                for(std::size_t k=0;k<256;++k)outside+=Sum(differences[body+k])*double(reference[cyclic(static_cast<std::ptrdiff_t>(body+k)-anchor,mode.period)]);
                if(quality(outside,energy[body+256]-energy[body],256)>std::max(.08,best*.4))continue;
                hits[modeIndex].push_back({first+phase/4.,best,std::arg(bestSum)*symbolRate/tau});
            }
        }
    }
    std::vector<Match> result;
    for(std::size_t modeIndex=0;modeIndex<3;++modeIndex) {
        const auto mode=headerModes[modeIndex];auto& found=hits[modeIndex];std::vector<Hit> unique;
        std::sort(found.begin(),found.end(),[](const Hit& a,const Hit& b){return a.coherence>b.coherence;});
        for(const auto hit:found)if(std::none_of(unique.begin(),unique.end(),[&](const Hit& other){return std::abs(hit.chip-other.chip)<mode.symbols/2.;}))unique.push_back(hit);
        std::sort(unique.begin(),unique.end(),[](const Hit& a,const Hit& b){return a.chip<b.chip;});
        std::vector<Hit> best;
        const auto period=mode.symbols+bodySymbols;
        for(const auto start:unique) {
            std::vector<Hit> chain{start};
            for(const auto hit:unique) {
                const auto distance=hit.chip-chain.back().chip;const auto frames=std::llround(distance/period);
                if(frames>=1 && frames<=2 && std::abs(distance-frames*period)<12 && std::abs(hit.offset-chain.back().offset)<100000)chain.push_back(hit);
            }
            if(chain.size()>best.size())best=std::move(chain);
        }
        if(best.size()>=4)result.push_back({mode,std::move(best)});
    }
    return result;
}
double medianOffset(const Match& match) {
    std::vector<double> values;for(const auto& hit:match.frames)values.push_back(hit.offset);
    const auto middle=values.begin()+values.size()/2;std::nth_element(values.begin(),middle,values.end());return *middle;
}
void detect(const SnapshotInput& input,Json& events) {
    if(input.rate<8000000 || input.samples.size()<input.rate*.004)return;
    const auto samples=input.samples.first(std::min(input.samples.size(),static_cast<std::size_t>(input.rate*windowSeconds)));
    struct Band {std::vector<Complex> samples;double offset,delay;};std::vector<Band> bands;
    if(input.rate<=12000000)bands.push_back({{samples.begin(),samples.end()},0,0});
    else {
        // Complete 8 MHz channels at arbitrary RF centers, including those
        // between grids. The bank is derived from input coverage, not a country
        // channel list. 12 MS/s filtering leaves room for 1 MHz residual CFO.
        const auto radius=std::min(input.searchWidth/2.,input.rate/2.-channelHz/2);
        const auto steps=std::max(1,static_cast<int>(std::ceil(2*radius/2e6)));
        std::vector<AnalysisChannel> channels;
        for(int i=0;i<=steps;++i)channels.push_back({-radius+2*radius*i/steps,2e6});
        Channelizer converter(input.rate,12000000,SampleFormat::cf32_le,std::move(channels));
        for(const auto& channel:converter.channels())bands.push_back({{},channel.offset_hz,converter.delay_input_samples()});
        const auto sink=[&](const ChannelizedBlock& block){auto& output=bands[block.channel].samples;output.insert(output.end(),block.samples.begin(),block.samples.end());};
        converter.feed(samples,sink);converter.finish(sink);
    }
    struct Candidate {Match match;double offset,first,delay;};std::vector<Candidate> candidates;
    for(const auto& band:bands)for(auto match:acquire(band.samples,input.rate<=12000000?input.rate:12000000)) {
        const auto offset=band.offset+medianOffset(match);
        if(std::abs(offset)>input.searchWidth/2 || std::abs(offset)+occupiedHz/2>input.rate/2.)continue;
        const auto first=match.frames.front().chip*input.rate/symbolRate-band.delay;
        auto duplicate=std::find_if(candidates.begin(),candidates.end(),[&](const Candidate& other){return std::abs(other.offset-offset)<200000;});
        Candidate candidate{std::move(match),offset,first,band.delay};
        if(duplicate==candidates.end())candidates.push_back(std::move(candidate));
        else if(candidate.match.frames.size()>duplicate->match.frames.size())*duplicate=std::move(candidate);
    }
    for(const auto& candidate:candidates) {
        const auto& match=candidate.match;
        double coherence=0;for(const auto& hit:match.frames)coherence+=hit.coherence;coherence/=match.frames.size();
        const auto relative=static_cast<std::uint64_t>(std::max(0.,std::round(candidate.first)));
        const auto end=static_cast<std::uint64_t>(std::max(0.,std::round((match.frames.back().chip+match.mode.symbols)*input.rate/symbolRate-candidate.delay)));
        if(input.first>UINT64_MAX-end)continue;
        Json event={{"modulation_id","dtmb"},{"modulation","DTMB"},{"evidence_level","dtmb_frame_structure"},
            {"display_label_key","dtmb.event"},{"center_frequency_hz",input.center+candidate.offset},
            {"first_observed_sample",input.first+relative},{"last_observed_sample",input.first+end},
            {"header_symbols",match.mode.symbols},{"frame_symbols",match.mode.symbols+bodySymbols},
            {"symbol_rate",symbolRate},{"checked_frames",match.frames.size()},{"pn_coherence",coherence},
            {"estimated_frequency_offset_hz",candidate.offset},{"payload_decoded",false},
            {"features",Json::array({
                {{"key","header_symbols"},{"label_key","dtmb.header"},{"value",match.mode.symbols}},
                {{"key","checked_frames"},{"label_key","dtmb.frames"},{"value",match.frames.size()}},
                {{"key","pn_coherence"},{"label_key","dtmb.coherence"},{"value",coherence},{"decimals",3}}})}};
        // display_label is a generic event field; resolve through the provider's
        // resource catalog, just like feature labels and assessment messages.
        Json display={{"label_key","dtmb.event"}};catalog().resolve(display);event["display_label"]=display.at("label");
        catalog().resolve(event);events.push_back(std::move(event));
    }
}
}
SnapshotExtension extension() {
    ModulationDescriptor descriptor{"dtmb","dtmb",DetectorStage::snapshot,
        [](DetectorSelection& selection)->bool&{return selection.snapshot.extensions["dtmb"];},
        [](const DetectorContext& context) {
            const auto filter=context.receiver.if_bandwidth_hz;
            Json result={{"label_key","dtmb.label"},{"covered",context.receiver.rate>=8000000 && context.maximum_span_hz>=channelHz && (filter<=0 || filter>=channelHz)},
                {"coverage_reason",text("dtmb.outside")},{"parsing",text("dtmb.parsing")},{"effect",text("dtmb.effect")},
                {"parsing_summary",text("dtmb.summary")},{"impact_summary",text("dtmb.impact")}};
            catalog().resolve(result);return result;
        }};
    return {std::move(descriptor),detect,{SnapshotTrigger::periodic,windowSeconds,1.,8000000}};
}
}
