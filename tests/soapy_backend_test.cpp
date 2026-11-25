#include "test_process.hpp"
#include "signalscope/adapters/receivers/receiver.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include "resource_count.hpp"

using namespace amc_test;

void checkStreamRecovery(const QString& plugin) {
    using namespace signalscope;
    qputenv("SOAPY_SDR_PLUGIN_PATH",QFile::encodeName(plugin));
    const auto& receiver=receiver_by_name("sdrplay");
    Config cfg;
    cfg.device_serial="AMC_TEST";
    cfg.seconds=.005;
    cfg.settle_seconds=0;
    cfg.auto_gain=true;
    const auto stride=bytes_per_pair(receiver.format);
    std::uint64_t accepted=0;
    const auto receive=[&](auto bytes,auto first) {
        require(first==accepted,"Recovered SDRplay stream did not start from zero");
        accepted+=bytes.size()/stride;
    };
    const auto neverCancel=[] {return false;};
    for(const auto fault:{471'000'000u,472'000'000u,473'000'000u}) {
        cfg.frequency=fault;
        accepted=0;
        bool failed=false;
        try {receiver.stream(cfg,receive,neverCancel,{});}
        catch(const SourceError& error) {
            failed=fault!=473'000'000 && accepted==4096 && error.details.at("accepted_pairs")==accepted &&
                error.details.at("reason_code")=="stream_discontinuity";
        }
        catch(const OperationError& error) {
            failed=fault==473'000'000 && error.code()==ErrorCode::receiver_stream && accepted==0;
        }
        require(failed,"SDRplay failure did not preserve the accepted sample boundary");
        accepted=0;
        cfg.frequency=470'000'000;
        bool ready=false;
        const auto report=receiver.stream(cfg,[&](auto bytes,auto first){
            require(ready,"SDRplay started streaming before reporting its configuration");
            receive(bytes,first);
        },neverCancel,[&](const ReceiverReadback& readback){
            require(!ready && readback.sample_rate_hz==cfg.rate && readback.center_frequency_hz==cfg.frequency &&
                    readback.filter_bandwidth_hz==1'536'000,"SDRplay live configuration readback is incorrect");
            ready=true;
        });
        require(report.at("status")=="complete" && accepted==10'000,
                "The SDRplay device could not be reopened after a stream failure");
    }
    cfg.frequency=474'000'000;
    cfg.seconds=.02;
    cfg.settle_seconds=.0005;
    accepted=0;
    std::vector<std::size_t> blocks;
    const auto coalesced=receiver.stream(cfg,[&](auto bytes,auto first){
        require(first==accepted,"Coalesced blocks lost absolute sample positions");
        const auto count=bytes.size()/stride;
        blocks.push_back(count);
        for(std::size_t i=0;i<count;++i)
            require(read_ci16_le(bytes.data()+i*stride)==(1000+first+i)%16384,
                    "Coalescing changed sample order after settling");
        accepted+=count;
    },neverCancel,{});
    require(coalesced.at("status")=="complete" && accepted==40'000 &&
            blocks==std::vector<std::size_t>{16384,16384,7232},
            "Short SDRplay reads did not form complete analysis blocks plus the final tail");
    cfg.frequency=470'000'000;cfg.seconds=0;cfg.settle_seconds=0;accepted=0;
    const auto started=std::chrono::steady_clock::now();
    const auto stopped=receiver.stream(cfg,receive,[&]{return std::chrono::steady_clock::now()-started>std::chrono::milliseconds(150);},{});
    require(stopped.at("status")=="cancelled" && accepted>0 && stopped.at("accepted_pairs")==accepted,
            "Cancelling a continuous service stream lost the accepted prefix or failed to release it");
    const auto baseline=openResourceCount();
    for(int cycle=0;cycle<32;++cycle) {
        cfg.seconds=.003;cfg.frequency=cycle%4==3?473'000'000:470'000'000;accepted=0;
        bool failed=false;Json result;
        try {
            result=receiver.stream(cfg,[&](auto bytes,auto first){
                if(cycle%4==1)throw std::runtime_error("Deliberate consumer failure");
                receive(bytes,first);
            },[&]{return cycle%4==2;},{});
        }catch(const OperationError& error){
            require(cycle%4==3 && error.code()==ErrorCode::receiver_stream,"Unexpected repeated stream operation failure");failed=true;
        }catch(const std::runtime_error& error){
            require(cycle%4==1 && std::string(error.what())=="Deliberate consumer failure","Unexpected repeated stream consumer failure");failed=true;
        }
        require(failed==(cycle%4==1 || cycle%4==3),"Repeated stream fault did not follow the expected path");
        if(!failed)require(result.at("status")== (cycle%4==2?"cancelled":"complete"),"Unexpected repeated stream result");
        require(openResourceCount()==baseline,"Repeated service streams retained descriptors or handles");
    }
    std::cout<<"{\"receiver_resource_cycles\":32,\"baseline\":"<<baseline<<",\"final\":"<<openResourceCount()<<"}\n";
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=3)return 2;
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(),"Could not create SDRplay test directory");
        ChildRunner runner(temporary.path());
        const auto exe=app.arguments()[1];
        const auto plugin=QFileInfo(app.arguments()[2]).absoluteFilePath();
        const auto run=[&](QString command,const QStringList& arguments={},int expected=0) {
            QStringList options{command,"--receiver","sdrplay"};
            options.append(arguments);
            const auto output=runner.run(exe,options,expected);
            if(expected && command!="validate")return QJsonObject{};
            const auto json=QJsonDocument::fromJson(output);
            require(json.isObject(),"SDRplay command did not return a JSON object");
            return json.object();
        };
        runner.setEnvironment("FORESHADOW_SDRPLAY_SERVICE","tcp://127.0.0.1:1");
        const auto unconfigured=run("devices");
        if(unconfigured.value("backend").toString()!="unavailable") {
            std::cout<<"Skip the simulated driver because an SDRplay driver is already installed.\n";
            return 77;
        }
        require(unconfigured.value("reason_code").toString()=="service_unavailable",
                "An enabled backend did not report the missing driver separately");
        runner.setEnvironment("SOAPY_SDR_PLUGIN_PATH",plugin);
        runner.setEnvironment("FORESHADOW_SDRPLAY_SERVICE",qEnvironmentVariable("FORESHADOW_SDRPLAY_SERVICE"));
        const auto devices=run("devices").value("devices").toArray();
        require(!devices.isEmpty() && devices[0].toObject().value("identification").toObject()
                .value("serial").toString()=="AMC_TEST","Simulated SDRplay was not enumerated");
        const auto probe=run("probe");
        const auto pinned=run("probe",{"--device","9","--serial","AMC_TEST"});
        require(pinned.value("device").toInt(-1)==0,"Serial lookup did not resolve the current SDRplay index");
        run("probe",{"--serial","UNKNOWN"},1);
        require(probe.value("hardware").toString()=="RSPdx-R2" && probe.value("native_format").toString()=="CS16" &&
                probe.value("antennas").toArray()==QJsonArray{"Antenna A","Antenna B","Antenna C"} &&
                probe.value("bandwidths_hz").toArray().contains(1536000) &&
                probe.value("hardware_info").toObject().value("sdrplay_api_api_version").toString()=="3.150000",
                "Unexpected RSPdx-R2 capabilities");
        QJsonArray gains;
        for(int db=20;db<=59;++db)gains.append(db*10);
        require(probe.value("gain_tenths_db").toArray()==gains,"SDRplay probe did not respect the IF gain step");
        const auto highPlan=run("validate",{"--rate","10000000","--purpose","monitor","--lora","0"}).value("plan").toObject();
        require(highPlan.value("hardware_valid").toBool() && highPlan.value("valid").toBool() &&
                highPlan.value("nominal_adc_bits").toInt()==8 && highPlan.value("bytes_per_second").toInt()==40000000,
                "Preflight did not distinguish high-rate receiver capability from analysis support");
        require(run("validate",{"--rate","10000000","--purpose","capture"}).value("plan").toObject().value("valid").toBool(),
                "Capture incorrectly inherited monitoring's sample rate limit");
        const auto configured=run("configure",{"--rate","6000000","--gain","32","--antenna","Antenna B"});
        require(configured.value("activation_confirmed").toBool() && configured.value("actual").toObject().value("sample_rate_hz").toInt()==6000000 &&
                configured.value("actual").toObject().value("filter_bandwidth_hz").toInt()==6000000 &&
                configured.value("requested").toObject().value("gain_tenths_db").toInt()==320,
                "Applying settings did not return hardware readback");
        run("configure",{"--rate","3100000"},1);
        run("configure",{"--frequency","473000000"},1);
        const auto complete=temporary.filePath("complete");
        const auto capture=run("capture",{"--output",complete,"--seconds",".01","--settle","0","--antenna","Antenna B","--gain","32",
                                          "--device","9","--serial","AMC_TEST"});
        require(capture.value("antenna").toString()=="Antenna B" &&
                capture.value("device_serial").toString()=="AMC_TEST" && capture.value("device").toInt(-1)==0 &&
                capture.value("actual_bandwidth_hz").toInt()==1536000 &&
                capture.value("actual_if_gain_reduction_db").toInt()==32 &&
                capture.value("accepted_pairs").toInt()==20000 &&
                capture.value("status").toString()=="complete","SDRplay capture did not complete");
        const auto reads=capture.value("read_stats").toObject();
        require(reads.value("reads").toInt()==5 && reads.value("short_reads").toInt()==3 &&
                reads.value("min_read_pairs").toInt()==3616 && reads.value("max_read_pairs").toInt()==4096 &&
                reads.value("delivered_blocks").toInt()==2,"SDRplay short-read diagnostics are incorrect");
        require(read(complete+"/capture.sigmf-data")==QByteArray::fromHex("d7f6851a").repeated(20000),
                "SDRplay capture altered CS16 bytes");
        require(QJsonDocument::fromJson(read(complete+"/capture.sigmf-meta")).object()
                .value("global").toObject().value("core:datatype").toString()=="ci16_le",
                "SDRplay capture datatype is incorrect");
        run("capture",{"--output",temporary.filePath("invalid-antenna"),"--seconds",".01","--antenna","invalid"},1);
        run("capture",{"--output",temporary.filePath("invalid-if-gain"),"--seconds",".01","--gain","31.5"},1);
        run("capture",{"--output",temporary.filePath("invalid-antenna-frequency"),"--seconds",".01",
                       "--frequency","200000001","--antenna","Antenna C"},1);
        for(const auto rate:{2'048'000,3'000'000,10'000'000}) {
            const auto text=QString::number(rate);
            const auto preset=run("capture",{"--output",temporary.filePath("rate-"+text),"--seconds",".01",
                                            "--settle","0","--rate",text});
            require(preset.value("status")=="complete" && preset.value("actual_rate_hz").toInt()==rate &&
                    preset.value("actual_bandwidth_hz").toInt()==(rate==10'000'000?8'000'000:1'536'000) &&
                    preset.value("accepted_pairs").toInt()==rate/100,
                    "SDRplay preset did not apply or delivered the wrong sample count");
        }
        run("capture",{"--output",temporary.filePath("bandwidth-not-applied"),"--seconds",".01","--rate","3100000"},1);
        const auto antennaC=run("capture",{"--output",temporary.filePath("antenna-c"),"--seconds",".01",
                                         "--settle","0","--frequency","200000000","--antenna","Antenna C"});
        require(antennaC.value("antenna").toString()=="Antenna C","Antenna C failed at its upper frequency limit");
        const auto failed=temporary.filePath("failed");
        run("capture",{"--output",failed,"--seconds",".01","--settle","0","--frequency","471000000"},1);
        const auto meta=QJsonDocument::fromJson(read(failed+"/capture.sigmf-meta")).object().value("global").toObject();
        require(meta.value("signalscope:status").toString()=="error" &&
                meta.value("signalscope:sample_count").toInt()==4096 &&
                meta.value("signalscope:result").toObject().value("driver_result").toInt()==-4,
                "SDRplay overflow did not preserve the partial capture and error metadata");
        require(meta.value("signalscope:result").toObject().value("read_stats").toObject()
                    .value("delivered_blocks").toInt()==1,
                "The partial capture did not preserve its read diagnostics");
        const auto retried=run("capture",{"--output",temporary.filePath("one-timeout"),"--seconds",".01",
                                        "--settle","0","--frequency","476000000"});
        require(retried.value("read_stats").toObject().value("scope").toString()=="client_transport_reads" &&
                retried.value("accepted_pairs").toInt()==20000,"A server-side transient timeout changed the accepted samples");
        const auto session=run("monitor",{"--output",temporary.filePath("monitor"),"--seconds",".005","--settle","0"});
        require(session.value("source").toString()=="live-sdrplay" &&
                session.value("sample_datatype").toString()=="ci16_le" &&
                session.value("source_pairs").toInt()==10000 &&
                session.value("processed_pairs").toInt()==10000 &&
                session.value("recording_processed_pairs").toInt()==10000,"SDRplay monitor sample counts disagree");
        const auto tooWide=temporary.filePath("lora-passband-too-wide");
        run("monitor",{"--output",tooWide,"--seconds",".005","--settle","0","--search-width","2100000"},1);
        require(!QFileInfo::exists(tooWide),"An invalid search created a session before configuration preflight");
        require(session.value("receiver_configuration").toObject().value("applied").toObject()
                .value("actual").toObject().value("sample_rate_hz").toInt()==2000000,
                "The live session did not publish actual receiver configuration");
        const auto narrower=run("monitor",{"--output",temporary.filePath("narrowband-passband"),"--seconds",".005",
            "--settle","0","--search-width","1100000","--lora","0","--fsk","1"});
        require(narrower.value("source_result").toObject().value("required_passband_hz").toInt()==1100000 &&
                narrower.value("processed_pairs").toInt()==10000,
                "The same SDRplay search width must work without the LoRa edge allowance");
        runner.setEnvironment("AMC_TEST_SOAPY_REALTIME","1");
        const auto burst=run("monitor",{"--output",temporary.filePath("fsk-short-reads"),"--seconds",".06","--settle","0",
            "--frequency","475000000","--lora","0","--fsk","1","--ook","0","--ask","0","--fm","0"});
        const auto fsk=burst.value("recent_fsk_events").toArray();
        require(fsk.size()==1 && std::abs(fsk.at(0).toObject().value("center_frequency_hz").toDouble()-475125000)<2500 &&
                burst.value("analysis_dropped_pairs").toInt(-1)==0,
                "A short-read SDRplay FSK burst was lost or misidentified");
        checkStreamRecovery(plugin);
        std::cout<<"SDRplay enumeration, probe, CS16 recording, overflow, monitoring and stream recovery passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
