#include "test_process.hpp"
#include <nlohmann/json.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <set>
#include <tuple>

using namespace amc_test;
using Json=nlohmann::json;

namespace {
constexpr double rate=2000000,center=470500000,tau=2*std::numbers::pi;
enum class Kind { chirp,noise,tone,lora,longDown,truncated,wrongSync,fsk,periodicFsk };
struct Wave {
    Kind kind=Kind::lora;
    int bw=125000,sf=7,first=571,offset=173333,preamble=8;
    double amplitude=.18,noise=.006,duration=0,sfd=2.25;
    bool inverted=false;
    unsigned seed=2147;
    int symbol() const{return static_cast<int>(rate*(1<<sf)/bw);}
};

QByteArray generate(const Wave& w) {
    const auto nSymbol=w.symbol();
    const auto count=std::max(20*nSymbol,int(w.duration*rate));
    QByteArray bytes;
    bytes.reserve(2*count);
    std::mt19937 random(w.seed);
    // Use the same Gaussian transform on each standard library.
    const auto uniform=[&]{return (double(random())+.5)/4294967296.;};
    double phaseFsk=0;
    for(int n=0;n<count;++n) {
        const auto magnitude=w.noise*std::sqrt(-2*std::log(uniform()));
        std::complex<double> z=std::polar(magnitude,tau*uniform());
        const auto position=double(n-w.first)/nSymbol;
        const auto carrier=w.offset*double(n)/rate;
        const auto chirp=[&](double fraction){return double(w.bw)*nSymbol/rate*(fraction*fraction-fraction)/2;};
        if(w.kind==Kind::chirp && position>=0 && position<10)
            z+=std::polar(.18,tau*(chirp(position-std::floor(position))+carrier));
        if((w.kind==Kind::lora || w.kind==Kind::longDown || w.kind==Kind::truncated ||
            w.kind==Kind::wrongSync) && position>=0) {
            const auto relative=position-w.preamble;
            double shift=0,time=position;
            int direction=1;
            bool active=true;
            if(relative>=0 && relative<1)shift=w.kind==Kind::wrongSync?3:24;
            else if(relative>=1 && relative<2)shift=w.kind==Kind::wrongSync?5:32;
            else if(relative>=2 && relative<(w.kind==Kind::longDown?5:2+w.sfd)) {
                direction=-1;
                active=w.kind!=Kind::truncated || relative<4;
            } else if(relative>=2 && relative<7.25 && (w.kind==Kind::lora || w.kind==Kind::wrongSync)) {
                shift=56;
                time-=w.sfd-std::floor(w.sfd);
            } else if(relative>=0)active=false;
            if(active) {
                time+=shift/(1<<w.sf);
                z+=std::polar(w.amplitude,tau*((w.inverted?-1:1)*direction*chirp(time-std::floor(time))+carrier));
            }
        }
        if(w.kind==Kind::tone)z+=std::polar(.18,tau*carrier);
        if(w.kind==Kind::fsk || w.kind==Kind::periodicFsk) {
            const bool bit=w.kind==Kind::periodicFsk?n/(nSymbol/2)%2:(((std::uint64_t(n/500)*1103515245+12345)>>12)&1);
            phaseFsk+=tau*(w.offset+(bit?w.bw/4:-w.bw/4))/rate;
            z+=std::polar(.18,phaseFsk);
        }
        for(const auto value:{z.real(),z.imag()})
            bytes.append(static_cast<char>(std::clamp(std::lround(127.5+128*value),0L,255L)));
    }
    return bytes;
}

Json parse(const QByteArray& bytes) {return Json::parse(bytes.constData(),bytes.constData()+bytes.size());}
QString dataPath(const QString& metadata) {
    auto path=metadata;
    path.chop(4);
    return path+"data";
}
QString storeIq(const QString& root,const QString& id,const Json& metadata,const QByteArray& bytes) {
    const auto path=QDir(root).filePath(id+".sigmf-meta");
    write(path,QByteArray::fromStdString(metadata.dump()));
    write(dataPath(path),bytes);
    return path;
}
QString synthetic(const QString& root,const QString& id,const Wave& wave) {
    const Json meta={{"global",{{"core:datatype","cu8"},{"core:sample_rate",rate},{"core:version","1.2.6"}}},
                     {"captures",Json::array({{{"core:sample_start",0},{"core:frequency",center}}})}};
    return storeIq(root,id,meta,generate(wave));
}
std::vector<Json> matches(const Json& result) {
    std::vector<Json> found;
    for(const auto& event:result.at("events"))
        if(event.value("evidence_level","")=="lora_structure_match")found.push_back(event);
    return found;
}
void requireMatch(const std::vector<Json>& events,int sf,int bw,double hz,double tolerance,bool inverted=false) {
    const auto expected=[&](const Json& e){
        return e.at("sf_candidate")==sf && e.at("bandwidth_hz")==bw &&
            e.at("iq_inverted")==inverted && std::abs(e.at("center_frequency_hz").get<double>()-hz)<=tolerance;
    };
    if(!std::any_of(events.begin(),events.end(),expected)) {
        std::cerr<<"Expected SF"<<sf<<" BW "<<bw<<" at "<<std::fixed<<hz<<" Hz: "<<Json(events).dump()<<'\n';
        throw std::runtime_error("Expected LoRa structure not found");
    }
}
void requireFrequencies(const std::vector<Json>& events,double hz,double tolerance) {
    for(const auto& event:events)
        require(std::abs(event.at("center_frequency_hz").get<double>()-hz)<=tolerance,
                "LoRa result exceeded the fixture frequency tolerance");
}

struct Suite {
    const QString root,exe;
    const QDir fixtures;
    const ChildRunner runner;

    Json detect(const QString& input,const QStringList& options={}) const {
        QStringList args{"detect","--input",input};
        args.append(options);
        return parse(runner.run(exe,args));
    }
    Json monitor(const QString& input,const QString& output,const QStringList& options={}) const {
        QStringList args{"monitor","--input",input,"--output",output};
        args.append(options);
        return parse(runner.run(exe,args));
    }
    QString fixture(const QString& name) const {
        const QDir dir(fixtures.filePath(name));
        const auto bytes=read(dir.filePath("capture.cu8"));
        const auto provenance=parse(read(dir.filePath("provenance.json")));
        const auto hash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
        require(hash.toStdString()==provenance.at("fixture_sha256").get<std::string>(),"RF fixture hash mismatch");
        return storeIq(root,name,parse(read(dir.filePath("capture.json"))),bytes);
    }

    void realRecordings() const {
        const QStringList low{"--threshold-db","11"};
        const auto sync=matches(detect(fixture("lora-weak-sync-pair"),low));
        const auto truth=parse(read(fixtures.filePath("lora-weak-sync-pair/provenance.json")));
        require(sync.size()==1,"Unexpected weak sync pair match count");
        requireMatch(sync,7,500000,truth.at("frequency_hz").get<double>(),5000);
        require(std::abs(sync.front().at("sfd_start_sample").get<double>()-
                         truth.at("sfd_start_sample").get<double>())<14,"Weak sync pair SFD timing changed");
        const auto dense=detect(fixture("lora-dense-wrong-sfd"),{"--threshold-db","11","--dense-short-symbols","1"});
        require(matches(dense).empty(),"Dense search accepted an invalid SFD");
        const auto& work=dense.at("search_work");
        require(std::any_of(work.begin(),work.end(),[](const Json& w){
            return w.at("sf")==7 && w.at("bandwidth_hz")==500000 && w.at("inverted")==false &&
                w.at("fine_stage_passes").at("direction").get<unsigned>()>0;
        }),"Dense negative fixture did not exercise direction verification");
        requireMatch(matches(detect(fixture("lora-sf7-weak-sfd"),low)),7,500000,478475172,5000);
        const auto edge=fixture("lora-edge-low-fine-sync");
        const auto high=fixture("lora-edge-high-sfd-transition");
        requireMatch(matches(detect(edge,low)),7,500000,478005003,5000);
        const auto highEvents=matches(detect(high,low));
        requireMatch(highEvents,7,500000,478994997,5000);
        require(std::any_of(highEvents.begin(),highEvents.end(),[](const Json& e){
            return e.at("sf_candidate")==7 && e.at("bandwidth_hz")==500000 &&
                std::abs(e.at("center_frequency_hz").get<double>()-478994997)<5000 &&
                e.at("sfd_end_method")=="joint_down_to_unknown_up";
        }),"High edge SFD transition evidence is missing");
        for(const auto* name:{"periodic-fsk-v3-false-1","periodic-fsk-v3-false-2"})
            require(matches(detect(fixture(name),low)).empty(),"Periodic FSK accepted as LoRa");

        const auto highBytes=read(dataPath(high));
        auto metadata=parse(read(high));
        metadata["global"]["core:datatype"]="ci16_le";
        QByteArray wideBytes;
        QDataStream words(&wideBytes,QIODevice::WriteOnly);
        words.setByteOrder(QDataStream::LittleEndian);
        for(const auto byte:highBytes)words<<qint16((2*static_cast<unsigned char>(byte)-255)*2);
        const auto wide=storeIq(root,"weak-ci16",metadata,wideBytes);
        const auto detected=detect(wide,low);
        require(detected.at("source_pairs")==highBytes.size()/2,"CI16 detector sample count changed");
        requireMatch(matches(detected),7,500000,478994997,5000);
        const auto wideStore=QDir(root).filePath("native-events");
        const auto status=monitor(wide,wideStore,{"--threshold-db","11","--bw","500000","--sf-min","7","--sf-max","7"});
        require(status.at("sample_datatype")=="ci16_le" && status.at("storage").at("completed")==1,
                "CI16 LoRa monitor did not save one event");
        const auto wideEvents=QDir(wideStore).entryList({"event-*"},QDir::Dirs);
        require(wideEvents.size()==1,"CI16 event store directory count changed");
        const auto saved=QDir(wideStore).filePath(wideEvents[0]+"/capture.sigmf-meta");
        const auto global=parse(read(saved)).at("global");
        const auto first=global.at("signalscope:result").at("stream_first_sample").get<qsizetype>();
        const auto count=global.at("signalscope:sample_count").get<qsizetype>();
        require(global.at("core:datatype")=="ci16_le" &&
                read(dataPath(saved))==wideBytes.mid(first*4,count*4),"CI16 LoRa event altered raw I/Q");

        const auto edgeStore=QDir(root).filePath("rf-edge-low-store");
        monitor(edge,edgeStore,low);
        const auto savedEdge=QDir(edgeStore).entryList({"event-*"},QDir::Dirs);
        const auto edgeBytes=read(dataPath(edge));
        require(savedEdge.size()==1 && read(QDir(edgeStore).filePath(savedEdge[0]+"/capture.sigmf-data"))==edgeBytes,
                "Edge recording changed or lost original I/Q");
        auto conjugated=edgeBytes;
        for(qsizetype i=1;i<conjugated.size();i+=2)conjugated[i]=static_cast<char>(255-static_cast<unsigned char>(conjugated[i]));
        const auto mirror=storeIq(root,"rf-edge-conjugated",parse(read(edge)),conjugated);
        requireMatch(matches(detect(mirror,low)),7,500000,478994997,5000,true);
    }

    void syntheticCases(const QString& captureTest) const {
        const QStringList low{"--threshold-db","11"};
        const std::array kinds{Kind::chirp,Kind::noise,Kind::tone,Kind::lora,Kind::longDown,
                               Kind::truncated,Kind::wrongSync,Kind::fsk,Kind::periodicFsk};
        for(const auto kind:kinds) {
            const auto id=QString("wave-%1").arg(static_cast<int>(kind));
            Wave wave{kind};
            if(kind==Kind::fsk || kind==Kind::periodicFsk)wave.duration=.4;
            const auto path=synthetic(root,id,wave);
            const QStringList sfRange{"--sf-min","7","--sf-max","8"};
            const auto result=detect(path,sfRange);
            require(result.at("source_pairs")==QFileInfo(dataPath(path)).size()/2,"Detector sample count changed");
            if(kind==Kind::chirp || kind==Kind::noise || kind==Kind::tone) {
                bool interval=false;
                for(const auto& event:result.at("events")) {
                    if(event.at("evidence_level")=="chirp_probe")continue;
                    require(kind==Kind::chirp && event.at("evidence_level")=="css_candidate" &&
                            event.at("needs_lora_structure_validation")==true,"Unexpected CSS evidence");
                    const auto& range=event.at("center_frequency_interval_hz");
                    interval|=event.at("sf_candidate")==7 && range[0].get<double>()<=470673333 && range[1].get<double>()>=470673333;
                }
                require(kind!=Kind::chirp || interval,"Generic chirp frequency interval missed the source");
            }
            const auto found=matches(result);
            require(found.empty()==(kind!=Kind::lora),"Unexpected LoRa structure at default threshold");
            if(kind==Kind::lora) {
                requireMatch(found,7,125000,470673333,rate/wave.symbol()*2);
                requireFrequencies(found,470673333,rate/wave.symbol()*2);
                for(const auto& event:found)
                    require(std::abs(event.at("sfd_start_sample").get<double>()-wave.first-10*wave.symbol())<32,
                            "Synthetic SFD timing changed");
            }
            const auto lowMatches=matches(detect(path,low));
            require(lowMatches.empty()==(kind!=Kind::lora),"Unexpected LoRa structure at low threshold");
            if(kind==Kind::lora) {
                const auto keySet=[](const std::vector<Json>& events) {
                    std::set<std::tuple<int,int,bool,std::uint64_t>> keys;
                    for(const auto& e:events)keys.emplace(e.at("sf_candidate").get<int>(),e.at("bandwidth_hz").get<int>(),
                        e.at("iq_inverted").get<bool>(),e.at("sfd_start_sample").get<std::uint64_t>());
                    return keys;
                };
                const auto expected=keySet(lowMatches);
                for(const auto workers:{"1","2","4"})
                    require(keySet(matches(detect(path,{"--threshold-db","11","--workers",workers})))==expected,
                            "LoRa result depends on scheduler worker count");
            }
            if(kind==Kind::lora || kind==Kind::chirp || kind==Kind::noise || kind==Kind::longDown) {
                const auto output=QDir(root).filePath(id+"-store");
                const auto status=monitor(path,output,{"--sf-min","7","--sf-max","8","--post-seconds","0.001"});
                const auto& storage=status.at("storage");
                require(status.at("analysis_dropped_pairs")==0 && status.at("processed_pairs")==status.at("source_pairs") &&
                        storage.at("ring_bytes")<=storage.at("ring_limit_bytes"),"Monitor lost samples or exceeded ring capacity");
                const auto directories=QDir(output).entryList({"event-*"},QDir::Dirs);
                require(directories.isEmpty()==(kind!=Kind::lora),"Unexpected recording for synthetic signal");
                for(const auto& name:directories) {
                    const auto saved=parse(read(QDir(output).filePath(name+"/capture.sigmf-meta")));
                    require(saved.at("global").at("signalscope:source")=="automatic-lora-event:recorded" &&
                            !saved.at("captures")[0].contains("core:datetime"),"Replay event claims RF capture time");
                }
            }
        }
        const std::array offsets{-410003,113337,0,487001,-173333};
        for(int sf=8;sf<=12;++sf) {
            Wave wave;wave.sf=sf;wave.offset=offsets[sf-8];wave.first=int(wave.symbol()*.37)+17;wave.seed+=sf;
            const auto label=QString::number(sf);
            const auto events=matches(detect(synthetic(root,"sf-"+label,wave),{"--sf-min",label,"--sf-max",label}));
            requireMatch(events,sf,wave.bw,center+wave.offset,2*rate/wave.symbol());
            requireFrequencies(events,center+wave.offset,2*rate/wave.symbol());
        }
        for(const int sf:{7,12}) {
            Wave wave;wave.sf=sf;wave.offset=313337;wave.inverted=true;wave.first=int(wave.symbol()*.31)+9;
            const auto label=QString::number(sf);
            const auto events=matches(detect(synthetic(root,"inverted-"+label,wave),{"--sf-min",label,"--sf-max",label}));
            requireMatch(events,sf,wave.bw,center+wave.offset,2*rate/wave.symbol(),true);
            requireFrequencies(events,center+wave.offset,2*rate/wave.symbol());
            for(const auto& event:events)require(event.at("iq_inverted")==true,"Inverted fixture produced non-inverted evidence");
        }
        Wave weak;
        weak.sf=12;weak.offset=-217333;weak.first=int(weak.symbol()*.23)+11;
        weak.noise=.04;weak.amplitude=std::sqrt(2*weak.noise*weak.noise*weak.bw/rate*.1);
        require(!matches(detect(synthetic(root,"weak-sf12",weak),{"--sf-min","12","--sf-max","12"})).empty(),
                "Weak SF12 preamble missed");
        const std::array combinations{std::tuple{250000,7,false},std::tuple{250000,11,false},
                                      std::tuple{500000,7,true},std::tuple{500000,12,false}};
        for(const auto [bw,sf,inverted]:combinations) {
            Wave wave;wave.bw=bw;wave.sf=sf;wave.inverted=inverted;wave.offset=-231337;
            wave.first=int(wave.symbol()*.19)+7;
            const auto events=matches(detect(synthetic(root,QString("auto-%1-%2").arg(bw).arg(sf),wave)));
            require(!events.empty(),"Unknown bandwidth and SF were not detected");
            for(const auto& event:events)
                require(event.at("bandwidth_hz")==bw && event.at("sf_candidate")==sf && event.at("iq_inverted")==inverted,
                        "Unexpected bandwidth, SF or polarity candidate");
        }
        for(const int bw:{125000,250000,500000})for(const auto fraction:{.07,.31,.67}) {
            Wave wave;wave.bw=bw;wave.offset=-173333;wave.preamble=6;wave.first=int(wave.symbol()*fraction);
            const auto events=matches(detect(synthetic(root,QString("pre6-%1-%2").arg(bw).arg(fraction),wave),low));
            require(std::any_of(events.begin(),events.end(),[&](const Json& e){return e.at("sf_candidate")==7 && e.at("bandwidth_hz")==bw;}),
                    "Short preamble missed");
        }
        Wave longPreamble;
        longPreamble.preamble=24;longPreamble.duration=.1;
        const QStringList sf7{"--bw","125000","--sf-min","7","--sf-max","7"};
        const auto longEvents=matches(detect(synthetic(root,"long-preamble",longPreamble),sf7));
        require(std::any_of(longEvents.begin(),longEvents.end(),[&](const Json& e){
            return e.at("first_observed_sample").get<int>()<=longPreamble.first+2*longPreamble.symbol();
        }),"Long preamble lost its earliest observation");
        const auto history=synthetic(root,"energy-history",Wave{});
        const auto original=read(dataPath(history));
        constexpr int prefix=1300000;
        write(dataPath(history),QByteArray(2*prefix,'\xff')+original);
        const auto historyEvents=matches(detect(history,sf7));
        require(historyEvents.size()==1,"Energy history produced an unexpected match count");
        const auto& event=historyEvents.front();
        const auto power=event.at("fullband_power_dbfs").get<double>();
        require(std::abs(event.at("sfd_start_sample").get<double>()-prefix-571-10*Wave{}.symbol())<32 &&
                power>-16 && power<-14,"Energy history changed event timing or power");
        for(const auto length:{1.875,2.,2.125,2.375,2.5,3.})for(const auto fraction:{.07,.67}) {
            Wave wave;wave.sfd=length;wave.first=int(wave.symbol()*fraction);
            require(matches(detect(synthetic(root,QString("bad-sfd-%1-%2").arg(length).arg(fraction),wave),low)).empty(),
                    "Detector accepted an incorrect SFD length");
        }
        for(const auto kind:{Kind::truncated,Kind::wrongSync})for(const auto fraction:{.07,.31,.67}) {
            Wave wave{kind};wave.first=int(wave.symbol()*fraction);
            require(matches(detect(synthetic(root,QString("bad-%1-%2").arg(static_cast<int>(kind)).arg(fraction),wave),low)).empty(),
                    "Detector accepted truncated or incorrect synchronization");
        }
        Wave backpressure;backpressure.duration=.4;
        const auto input=synthetic(root,"backpressure",backpressure);
        runner.run(captureTest,{input,QDir(root).filePath("capture-backpressure")});
    }
};
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=4)return 2;
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(),"Could not create LoRa test directory");
        const Suite suite{temporary.path(),args[1],QDir(args[3]),ChildRunner(temporary.path())};
        suite.realRecordings();
        suite.syntheticCases(args[2]);
        std::cout<<"LoRa RF fixtures, synthetic matrix and capture backpressure passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
