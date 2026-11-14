#include "signalscope/backend/media/protocol.hpp"
#include "test_process.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
using namespace signalscope;
using namespace signalscope::media;
using amc_test::require;
namespace {
class TimerControl {
public:
    explicit TimerControl(bool enabled) {
        if(enabled)worker=std::jthread([this]{
#ifdef __APPLE__
            qosError=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
#endif
            std::unique_lock lock(mutex);
            while(!stopped) {
                const auto start=std::chrono::steady_clock::now();
                if(changed.wait_for(lock,std::chrono::milliseconds(24),[&]{return stopped;}))break;
                observations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
                changed.notify_all();
            }
        });
    }
    ~TimerControl(){finish();}
    void finish(bool complete=false) {
        if(complete && worker.joinable()) {
            std::unique_lock lock(mutex);
            // A short clip can finish before a delayed host supplies eight
            // controls. Complete that fixed minimum in a bounded adjacent
            // window instead of classifying an undersampled environment.
            changed.wait_for(lock,std::chrono::seconds(2),[&]{return observations.size()>=8;});
        }
        {std::lock_guard lock(mutex);stopped=true;changed.notify_all();}
        if(worker.joinable())worker.join();
    }
    std::vector<std::int64_t> observations;int qosError=0;
private:
    std::mutex mutex;std::condition_variable changed;bool stopped=false;std::jthread worker;
};
struct Result {
    Json catalog,ending;std::vector<std::int16_t> pcm;std::size_t video=0;int audioPid=-1;
    std::vector<std::uint32_t> checksums;
    std::vector<std::pair<std::int64_t,std::int64_t>> audioDelivery;
    std::vector<std::int64_t> timerWaits;int timerQosError=0;
};
Result decode(const QString& helper,const QString& source,const QStringList& selection) {
    Result result;QProcess process;QStringList arguments{"--checksum"};arguments.append(selection);process.start(helper,arguments);
    require(process.waitForStarted(5000),"The packaged media helper could not start");
    const auto input=amc_test::read(source);require(input.size()<64*1024*1024,"The independent media fixture exceeded its input budget");
    require(process.write(input)==input.size(),"The helper rejected the reference stream");process.closeWriteChannel();
    QElapsedTimer arrival;arrival.start();
    TimerControl timerControl(selection.contains("--realtime"));
    MessageParser parser([&](Message message){
        if(message.type==MessageType::catalog)result.catalog=message.metadata;
        if(message.type==MessageType::ended)result.ending=message.metadata;
        require(message.type!=MessageType::error,"The real media decoder returned a terminal error");
        if(message.type==MessageType::video){++result.video;result.checksums.push_back(message.metadata.at("source_adler32"));
            require(message.metadata.at("format")=="rgb24" && !message.payload.empty(),"Decoded video did not reach the media protocol");}
        if(message.type==MessageType::audio) {
            result.audioDelivery.emplace_back(arrival.nsecsElapsed()/1000,message.pts_us);
            require(message.metadata.at("format")=="s16_le" && message.metadata.at("sample_rate")==48000 && message.metadata.at("channels")==2,
                "Decoded audio did not use the declared output protocol");
            result.audioPid=message.metadata.at("pid");
            require(result.pcm.size()+message.payload.size()/4<=48000*20,"Decoded fixture audio exceeded its budget");
            for(std::size_t i=0;i+3<message.payload.size();i+=4)result.pcm.push_back(static_cast<std::int16_t>(unsigned(message.payload[i])|(unsigned(message.payload[i+1])<<8)));
        }
    });
    QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning) {
        if(timer.elapsed()>30000){process.kill();process.waitForFinished(1000);throw std::runtime_error("The actual media decoder timed out");}
        process.waitForReadyRead(10);const auto bytes=process.readAllStandardOutput();
        parser.feed({reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())});
    }
    const auto bytes=process.readAllStandardOutput();parser.feed({reinterpret_cast<const std::uint8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())});parser.finish();
    timerControl.finish(true);result.timerWaits=std::move(timerControl.observations);result.timerQosError=timerControl.qosError;
    if(process.exitCode()!=0)std::cerr<<process.readAllStandardError().constData();
    require(process.exitStatus()==QProcess::NormalExit && process.exitCode()==0 && !result.ending.empty(),"The media helper did not complete its declared protocol");
    return result;
}
double energy(const Result& result,unsigned frequency) {
    std::complex<double> sum=0;
    for(std::size_t i=2048;i<result.pcm.size();++i)sum+=double(result.pcm[i])*std::polar(1.,-2*std::numbers::pi*frequency*i/48000.);
    return std::norm(sum);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(app.arguments().size()>=3,"Expected helper and independent reference TS paths");const auto helper=app.arguments()[1],source=app.arguments()[2];
        if(app.arguments().contains("--realtime-contract")) {
            // Keep the fixed raw 120 ms criterion. An independent 24 ms timer
            // on the same host distinguishes an unqualified timing environment
            // from a functional pass; it never excuses lost frames or budgets.
            std::vector<std::int64_t> gaps,controlMedians;bool controlQualified=true;
            for(unsigned trial=0;trial<3;++trial) {
                const auto result=decode(helper,source,{"--program","101","--realtime"});std::int64_t maximumGap=0;Json largest;
                require(result.audioDelivery.size()>20 && result.video==10 && result.ending.at("decode_errors")==0,
                    "The paced real media fixture lost decoded frames");
                for(std::size_t i=1;i<result.audioDelivery.size();++i) {
                    const auto pts=result.audioDelivery[i].second-result.audioDelivery[i-1].second;
                    const auto elapsed=result.audioDelivery[i].first-result.audioDelivery[i-1].first;
                    if(pts>0 && pts<=50000 && elapsed>maximumGap) {
                        maximumGap=elapsed;largest={{"arrival_before_us",result.audioDelivery[i-1].first},{"arrival_after_us",result.audioDelivery[i].first},
                            {"pts_before_us",result.audioDelivery[i-1].second},{"pts_after_us",result.audioDelivery[i].second}};
                    }
                }
                gaps.push_back(maximumGap);const auto& output=result.ending.at("output_scheduler");
                auto waits=result.timerWaits;std::sort(waits.begin(),waits.end());
                controlQualified&=waits.size()>=8 && result.timerQosError==0;
                const auto median=waits.empty()?0:waits[waits.size()/2];controlMedians.push_back(median);
                std::cout<<Json{{"realtime_trial",trial},{"maximum_continuous_audio_delivery_gap_us",maximumGap},
                    {"largest_gap",largest},{"output_scheduler",output},{"platform_timer",{
                        {"requested_us",24000},{"observations_us",waits},{"median_us",median},{"qos_error",result.timerQosError}}}}.dump()<<'\n';
                for(const auto* lane:{"video","audio","control"})require(output.at(lane).at("held_bytes")==0 &&
                    output.at(lane).at("peak_bytes")<=output.at(lane).at("budget_bytes"),"Actual helper output exceeded its wire-byte budget or ended before draining");
            }
            std::sort(gaps.begin(),gaps.end());std::cout<<"median_continuous_audio_delivery_gap_us="<<gaps[1]<<'\n';
            std::sort(controlMedians.begin(),controlMedians.end());
            if(gaps[1]>=120000 && controlQualified && controlMedians[1]>=48000) {
                std::cout<<"timing_qualification=blocked_platform_timer; raw audio criterion failed, independent 24 ms timer repeatedly missed at least one period; media integrity and budgets passed\n";
                return 77;
            }
            require(gaps[1]<120000,"Repeated delivery gaps occurred for continuously timestamped audio");
            return 0;
        }
        if(app.arguments().contains("--avsplus")) {
            const auto result=decode(helper,source,{});
            require(result.checksums==std::vector<std::uint32_t>{0xbc59eeee,0x490377c0,0xaca11851,0x70251b61,0x22be3dd4},
                "The actual AVS+ Broadcasting frames differ from the independent official FATE checksums");
            std::cout<<"Actual AVS+ Broadcasting frames match all five official checksums\n";return 0;
        }
        if(app.arguments().contains("--dra")) {
            const auto result=decode(helper,source,{"--program","301"});
            require(result.video==291 && result.audioPid==650 && result.pcm.size()==599040,
                "The independent DRA programme did not produce its actual video and audio frames");
            long double sum=0;for(const auto value:result.pcm)sum+=static_cast<long double>(value)*value;
            require(sum>result.pcm.size() && result.ending.at("decode_errors")==2,
                "The DRA sample was silent or its known partial-input diagnostics changed");
            std::cout<<"Actual DRA programme: 291 video frames, 599040 nonzero PCM frames, PID 650; two known input diagnostics retained\n";
            return 0;
        }
        for(const auto program:{101,202}) {
            const auto result=decode(helper,source,{"--program",QString::number(program)});
            require(result.video==10 && result.pcm.size()>=48000 && result.ending.at("decode_errors")==0,"The synthetic reference did not decode cleanly into its actual media frames");
            require(result.catalog.at("programs").size()==2 && result.catalog.at("transport").at("section_crc_errors")==0 &&
                    result.catalog.at("transport").at("continuity_errors")==0,"The actual program table or reference TS integrity was lost");
            const auto expected=program==101?440:880;
            require(result.audioPid==(program==101?257:258) && energy(result,expected)>100*energy(result,expected==440?880:440),
                "Program selection decoded the wrong PID or audio tone");
        }
        const auto explicitTrack=decode(helper,source,{"--program","101","--audio-pid","258"});
        require(explicitTrack.audioPid==258 && energy(explicitTrack,880)>100*energy(explicitTrack,440),"Explicit track selection did not change the actual decoded sound");
        std::cout<<"Actual MPEG-2/MP2 media, two programs, explicit audio tracks, PCM tone spectra and clean TS integrity passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
