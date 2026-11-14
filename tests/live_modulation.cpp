#include "test_process.hpp"
#include <nlohmann/json.hpp>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <set>
#include <span>

namespace {
using namespace amc_test;
using Json=nlohmann::json;
using Sample=std::complex<double>;
constexpr double tau=2*std::numbers::pi;

Json parse(const QByteArray& bytes){return Json::parse(bytes.constData(),bytes.constData()+bytes.size());}

QByteArray quantize(std::span<const Sample> samples) {
    QByteArray data;
    data.reserve(samples.size()*2);
    for(const auto sample:samples)
        for(const auto component:{sample.real(),sample.imag()})
            data.append(static_cast<char>(std::clamp(std::lround(127.5+128*component),0L,255L)));
    return data;
}

std::vector<Sample> noise(std::size_t count,std::mt19937& random) {
    const auto uniform=[&]{return (double(random())+.5)/4294967296.;};
    std::vector<Sample> result(count);
    for(auto& sample:result)sample=std::polar((2./128)*std::sqrt(-2*std::log(uniform())),tau*uniform());
    return result;
}

void addFsk(std::span<Sample> samples,unsigned rate,std::size_t start,int offset,double amplitude,
            std::span<const int> bits) {
    const auto period=static_cast<std::size_t>(std::llround(rate/2400.));
    require(start+bits.size()*period<=samples.size(),"FSK fixture exceeds recording");
    double phase=0;
    for(std::size_t i=0;i<bits.size()*period;++i) {
        phase+=tau*(offset+(bits[i/period]?4800:-4800))/rate;
        samples[start+i]+=std::polar(amplitude,phase);
    }
}

struct Suite {
    QString root,exe;
    ChildRunner runner;
    unsigned rate;

    Json monitor(const QString& id,const QByteArray& bytes,double center,bool enabled=true,bool wide=false,
                 const QStringList& detectorOptions={}) const {
        const auto base=QDir(root).filePath(id);
        const Json metadata={{"global",{{"core:datatype",wide?"ci16_le":"cu8"},{"core:sample_rate",rate}}},
            {"captures",Json::array({{{"core:frequency",center},{"core:sample_start",0}}})}};
        write(base+".sigmf-meta",QByteArray::fromStdString(metadata.dump()));
        write(base+".sigmf-data",bytes);
        QStringList arguments{"monitor","--input",base+".sigmf-meta","--output",base+"-events",
                              "--threshold-db","11","--fsk",enabled?"1":"0"};
        const auto speed=qEnvironmentVariable("AMC_TEST_MONITOR_REPLAY_SPEED");
        if(!speed.isEmpty())arguments.append({"--replay-speed",speed});
        if(wide)arguments.append({"--block-bytes","131072"});
        arguments.append(detectorOptions);
        auto status=parse(runner.run(exe,arguments));
        const auto captured=status.at("source_pairs").get<std::uint64_t>();
        require(status.at("processed_pairs").get<std::uint64_t>()+status.at("analysis_dropped_pairs").get<std::uint64_t>()==captured &&
                status.at("recording_processed_pairs")==captured,
                "Accepted source samples must be accounted for independently in analysis and recording");
        if(status.contains("fsk_analysis")) {
            const auto& auxiliary=status.at("fsk_analysis");
            require(auxiliary.at("processed_pairs").get<std::uint64_t>()+auxiliary.at("skipped_pairs").get<std::uint64_t>()==captured &&
                    auxiliary.at("queue_high_water_bytes").get<std::uint64_t>()<=auxiliary.at("queue_capacity_bytes").get<std::uint64_t>(),
                    "Auxiliary sample loss or byte budget accounting is inconsistent");
        }
        return status;
    }

    static void requireFsk(const Json& status,double frequency) {
        const auto& events=status.at("recent_fsk_events");
        if(events.size()!=1 || std::abs(events[0].at("center_frequency_hz").get<double>()-frequency)>=2500) {
            std::cerr<<"Expected FSK center "<<frequency<<" Hz, events "<<events.dump()
                     <<", narrowband worker "<<status.at("fsk_analysis").dump()
                     <<", analysis dropped pairs "<<status.at("analysis_dropped_pairs").dump()<<'\n';
            throw std::runtime_error("Unexpected FSK result");
        }
    }

    void mixed(const QByteArray& original,double center,std::span<const int> bits,std::mt19937& random) const {
        auto data=original;
        auto fsk=noise(rate/4,random);
        const auto start=rate/20;
        addFsk(fsk,rate,start,125000,.20,bits);
        data+=quantize(fsk);
        for(const auto [offset,low]:{std::pair{225000,0.},std::pair{175000,.08}}) {
            auto am=noise(rate/3,random);
            const auto period=static_cast<std::size_t>(std::llround(rate*.0025));
            for(std::size_t k=0;k<24*period;++k)
                am[start+k]+=std::polar(k/period%2?low:.20,tau*offset*k/rate);
            data+=quantize(am);
        }
        const auto off=monitor("mixed-off",data,center,false);
        const auto on=monitor("mixed-on",data,center);
        require(on.at("recent_lora_events").size()==1 && off.at("recent_lora_events")==on.at("recent_lora_events") &&
                off.at("storage").at("completed")==1 && on.at("storage").at("completed")==1 &&
                on.at("analysis_dropped_pairs")==0 && on.at("analysis_gaps")==0,
                "Narrowband analysis changed LoRa events or lost samples");
        requireFsk(on,center+125000);
        const auto& amplitude=on.at("recent_ask_events");
        require(amplitude.size()==2,"Expected one OOK event and one ASK event");
        for(const auto [kind,offset]:{std::pair{"ook_candidate",225000},std::pair{"ask_candidate",175000}})
            require(std::any_of(amplitude.begin(),amplitude.end(),[&](const Json& event){
                return event.at("evidence_level")==kind &&
                    std::abs(event.at("center_frequency_hz").get<double>()-center-offset)<2500;
            }),"Amplitude event modulation or frequency changed");

        QByteArray wideData;
        QDataStream words(&wideData,QIODevice::WriteOnly);
        words.setByteOrder(QDataStream::LittleEndian);
        for(const auto byte:data)words<<qint16(static_cast<unsigned char>(byte)*256-32640);
        const auto wide=monitor("mixed-native",wideData,center,true,true);
        require(wide.at("sample_datatype")=="ci16_le" && wide.at("recent_lora_events")==on.at("recent_lora_events"),
                "CI16 monitor changed LoRa results");
        const auto& wideFsk=wide.at("recent_fsk_events");
        require(std::any_of(wideFsk.begin(),wideFsk.end(),[&](const Json& event){
            return std::abs(event.at("center_frequency_hz").get<double>()-center-125000)<2500;
        }),"CI16 FSK event missed");
        std::set<std::string> kinds;
        for(const auto& event:wide.at("recent_ask_events"))kinds.insert(event.at("evidence_level").get<std::string>());
        if(kinds!=std::set<std::string>{"ook_candidate","ask_candidate"}) {
            std::cerr<<"CI16 amplitude events "<<wide.at("recent_ask_events").dump()
                     <<", narrowband worker "<<wide.at("fsk_analysis").dump()
                     <<", analysis dropped pairs "<<wide.at("analysis_dropped_pairs").dump()
                     <<", analysis gaps "<<wide.at("analysis_gaps").dump()<<'\n';
            throw std::runtime_error("CI16 amplitude event types changed");
        }
    }

    void broadcast(std::span<const int> bits) const {
        constexpr double center=100800000;
        std::vector<Sample> iq(rate/2);
        double stereoPhase=0,monoPhase=0;
        for(std::size_t i=0;i<iq.size();++i) {
            const auto t=tau*i/rate;
            const auto audio=17000*std::sin(1900*t)+11000*std::sin(4200*t);
            stereoPhase+=tau*(155000+audio+6700*std::cos(19000*t))/rate;
            monoPhase+=tau*(-220000+audio)/rate;
            iq[i]=std::polar(.22,stereoPhase)+std::polar(.28,350000*t)+std::polar(.14,monoPhase);
        }
        const auto stereoIq=quantize(iq);
        const auto stereo=monitor("stereo",stereoIq,center);
        const auto& fm=stereo.at("recent_fm_events");
        require(fm.size()==1 && std::abs(fm[0].at("center_frequency_hz").get<double>()-center-155000)<2500 &&
                fm[0].at("pilot_frequency_hz")==19000,"Broadcast stereo pilot or frequency not found");
        const auto fmOnly=monitor("stereo-fm-only",stereoIq,center,false,false,{"--fm","1","--lora","0"});
        const auto& single=fmOnly.at("recent_fm_events");
        require(single.size()==1 && single[0].at("pilot_frequency_hz")==19000 &&
                std::abs(single[0].at("center_frequency_hz").get<double>()-center-155000)<2500 &&
                fmOnly.at("recent_fsk_events").empty() &&
                fmOnly.at("recent_ask_events").empty(),
                "Standalone FM selection changed the result or enabled other narrowband detectors");

        // Frequency excursions at constant amplitude must not masquerade as AM.
        for(const bool stepped:{false,true}) {
            double phase=0;
            for(std::size_t i=0;i<iq.size();++i) {
                const auto t=tau*i/rate;
                const auto shape=std::cos(250*t);
                const auto sweep=stepped?double(shape>0):std::pow(.5+.5*shape,.25);
                phase+=tau*(5600*sweep+6300*std::cos(19000*t))/rate;
                iq[i]=std::polar(.34,phase);
            }
            const auto result=monitor(stepped?"fm-stepped":"fm-smooth",quantize(iq),center);
            require(!result.at("recent_fm_events").empty() && result.at("recent_ask_events").empty(),
                    "Constant-amplitude FM produced an OOK or ASK candidate");
        }

        double phase=0;
        for(std::size_t i=0;i<iq.size();++i) {
            const auto t=tau*i/rate;
            phase+=tau*(5500*std::tanh(5*std::sin(150*t))+6500*std::cos(19000*t))/rate;
            iq[i]=std::polar(.30,phase);
        }
        addFsk(iq,rate,rate/5,275000,.42,bits);
        const auto conflict=monitor("fm-fsk",quantize(iq),center);
        require(!conflict.at("recent_fm_events").empty(),"FM structure was lost during the FSK conflict");
        requireFsk(conflict,center+275000);
    }
};
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=3)return 2;
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(),"Could not create mixed-modulation test directory");
        const QDir fixture(args[2]);
        const auto metadata=parse(read(fixture.filePath("capture.json")));
        const auto rate=metadata.at("global").at("core:sample_rate").get<unsigned>();
        const auto center=metadata.at("captures")[0].at("core:frequency").get<double>();
        std::mt19937 random(581);
        std::vector<int> bits(252);
        for(std::size_t i=0;i<bits.size();++i)bits[i]=i<32?i%2:random()&1;
        const Suite suite{temporary.path(),args[1],ChildRunner(temporary.path()),rate};
        suite.mixed(read(fixture.filePath("capture.cu8")),center,bits,random);
        suite.broadcast(bits);
        std::cout<<"Shared I/Q modulation pipeline and broadcast FM conflict regressions passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
