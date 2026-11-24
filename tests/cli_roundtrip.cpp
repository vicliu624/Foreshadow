#include "test_process.hpp"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>

using namespace amc_test;

namespace {
void checkLoRaAudit(const ChildRunner& runner,const QString& root,const QString& cli,const QString& comparator,
                    const QString& benchmark,const QDir& fixture) {
    const auto input=root+"/source.sigmf-meta";
    const auto status=root+"/status.json";
    const auto audit=root+"/audit";
    write(input,read(fixture.filePath("capture.json")));
    write(root+"/source.sigmf-data",read(fixture.filePath("capture.cu8")));
    write(status,runner.run(cli,{"monitor","--input",input,"--output",root+"/events",
                                  "--audit-iq-output",audit,"--threshold-db","11"}));
    const auto store=root+"/snapshot store";
    const auto event=QStringLiteral("event-\u4e8b\u4ef6");
    require(QDir().mkpath(store+'/'+event) && QDir().mkpath(store+"/event-empty"),
            "Could not create an event snapshot fixture");
    write(store+"/monitor-status.json",read(root+"/events/monitor-status.json"));
    const auto eventMetadata=store+'/'+event+"/capture.sigmf-meta";
    const auto metadataBytes=read(audit+"/capture.sigmf-meta");
    write(eventMetadata,metadataBytes);
    const auto snapshot=QJsonDocument::fromJson(runner.run(cli,{"store-snapshot","--input",store})).object();
    const auto monitorStatus=snapshot.value("status").toObject();
    const auto records=snapshot.value("records").toArray();
    require(snapshot.value("remote_unix_seconds").toDouble()>0 &&
            monitorStatus.value("state")=="stopped" &&
            records.size()==1 && records[0].toObject().value("name")==event &&
            records[0].toObject().value("metadata").toObject()==QJsonDocument::fromJson(metadataBytes).object() &&
            read(eventMetadata)==metadataBytes,"The read-only snapshot lost event metadata or changed the source");
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    const auto cpu=monitorStatus.value("cpu_percent_of_machine").toDouble(-1);
    const auto rss=monitorStatus.value("rss_bytes").toInteger();
    require(std::isfinite(cpu) && cpu>=0 && rss>0 && monitorStatus.value("peak_rss_bytes").toInteger()>=rss,
            "Monitor did not report consistent native process resource usage");
#endif
    const auto output=root+"/comparison.json";
    const QStringList options{"--input",audit+"/capture.sigmf-meta","--live-status",status,
                              "--exe",cli,"--output",output};
    runner.run(comparator,options);
    const auto bytes=read(output);
    const auto comparison=QJsonDocument::fromJson(bytes).object();
    require(comparison.value("source_pairs").toInt()==120000 &&
            comparison.value("live_events").toArray().size()==1,"LoRa audit lost the live event");
    for(const auto* mode:{"default","dense"}) {
        const auto results=comparison.value("offline").toObject().value(mode).toObject();
        const auto pairs=results.value("against_live").toObject();
        require(results.value("events").toArray().size()==1 && pairs.value("matched").toArray().size()==1 &&
                pairs.value("missing").toArray().isEmpty() && pairs.value("additional").toArray().isEmpty(),
                "LoRa audit did not match the offline result to the live event");
    }
    runner.run(comparator,options,1);
    require(read(output)==bytes,"LoRa audit replaced an existing comparison");
    const auto benchmarkReport=root+"/benchmark.json";
    const QStringList benchmarkOptions{"--exe",cli,"--input",input,"--threshold","18","--threshold","11","--workers","2"};
    runner.run(benchmark,benchmarkOptions+QStringList{"--output",benchmarkReport});
    const auto trials=QJsonDocument::fromJson(read(benchmarkReport)).object().value("trials").toArray();
    require(trials.size()==2 && trials[0].toObject().value("structure_matches").toArray().isEmpty() &&
            trials[1].toObject().value("structure_matches").toArray().size()==1 &&
            trials[1].toObject().value("workers").toInt()==2 &&
            trials[1].toObject().value("wall_seconds_per_iq_second").toDouble()>0,
            "Native LoRa benchmark did not preserve the threshold sweep");
    runner.run(benchmark,benchmarkOptions+QStringList{"--output",input},1);
    require(read(input)==read(fixture.filePath("capture.json")),"Benchmark replaced the source metadata");
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=6)return 2;
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(),"Could not create CLI test directory");
        const ChildRunner runner(temporary.path());
        const auto exe=QFileInfo(app.arguments()[1]).absoluteFilePath();
        const auto run=[&](const QStringList& arguments,int expected=0) {
            const auto output=runner.run(exe,arguments,expected);
            if(expected)return QJsonObject{};
            const auto json=QJsonDocument::fromJson(output);
            require(json.isObject(),"CLI did not return a JSON object");
            return json.object();
        };
        const auto original=temporary.filePath(QStringLiteral("\u5f55\u5236 cafe\u0301 \U0001f4e1"));
        const auto auditRoot=temporary.filePath(QStringLiteral("\u5bf9\u7167 audit"));
        require(QDir().mkpath(auditRoot),"Could not create the LoRa audit directory");
        checkLoRaAudit(runner,auditRoot,exe,app.arguments()[3],app.arguments()[4],QDir(app.arguments()[5]));
        const auto meta=original+"/capture.sigmf-meta";
        const auto data=original+"/capture.sigmf-data";
        const auto generated=run({"synth","--output",original,"--seconds",".05","--tone","123456"});
        const auto payload=read(data);
        const auto report=temporary.filePath(QStringLiteral("\u6d4b\u91cf.txt"));
        runner.run(app.arguments()[2],{"--input",meta,"--output",report,"--language","en"});
        require(read(report).contains("Analyzed I/Q pairs: 100000") && read(report).contains("dBFS"),
                "Power plugin did not report the measured I/Q");
        const auto localizedReport=temporary.filePath(QStringLiteral("\u529f\u7387.txt"));
        runner.run(app.arguments()[2],{"--input",meta,"--output",localizedReport,"--language","zh-CN"});
        require(QString::fromUtf8(read(localizedReport)).startsWith(QStringLiteral("I/Q \u529f\u7387\u62a5\u544a")),
                "Power plugin did not generate a Chinese report");
        const auto invalidReport=temporary.filePath("invalid-range.txt");
        runner.run(app.arguments()[2],{"--input",meta,"--output",invalidReport,"--language","en",
                                      "--first-sample","99999","--samples","2"},1);
        require(!QFile::exists(invalidReport),"An out-of-bounds sample range produced a report");
        require(generated.value("pairs").toInt()==100000 && payload.size()==200000,"Unexpected synth sample count");
        const auto info=run({"inspect","--input",meta});
        const auto recorderVersion=QString::fromUtf8(runner.run(exe,{"--version"})).trimmed();
        const auto recordedMetadata=QJsonDocument::fromJson(read(meta)).object();
        require(recordedMetadata.value("global").toObject().value("core:recorder").toString()==
                    "Foreshadow "+recorderVersion &&
                info.value("metadata").toObject()==recordedMetadata,
                "Recording provenance does not match the actual recorder binary version");
        auto historicalMetadata=recordedMetadata;
        auto historicalGlobal=historicalMetadata.value("global").toObject();
        historicalGlobal.insert("core:recorder","SignalScope 0.1.0");
        historicalMetadata.insert("global",historicalGlobal);
        write(meta,QJsonDocument(historicalMetadata).toJson());
        require(run({"inspect","--input",meta}).value("pairs")==info.value("pairs"),
                "Historical recorder identity made existing I/Q unreadable");
        write(meta,QJsonDocument(recordedMetadata).toJson());
        const auto replay=run({"replay-events","--input",meta,"--lora","0","--fsk","0","--ook","0","--ask","0","--fm","0"});
        require(replay.value("state")=="replay_finished" && replay.value("processed_pairs")==info.value("pairs"),
                "Headless replay did not finish the Unicode-path recording");
        run({"replay-events","--input",meta,"--speed","0.001"},1);
        const auto destination=temporary.filePath(QStringLiteral("\u5bfc\u51fa caf\u00e9"));
        const QStringList exportArgs{"export","--input",meta,"--output",destination};
        require(run(exportArgs).value("status")=="complete","Recording export did not finish");
        const auto sourceMeta=read(meta);
        require(read(destination+"/capture.sigmf-data")==payload &&
                read(destination+"/capture.sigmf-meta")==sourceMeta,"Export changed SigMF bytes");
        run(exportArgs,1);
        require(read(destination+"/capture.sigmf-meta")==sourceMeta &&
                read(destination+"/capture.sigmf-data")==payload,"Duplicate export changed the existing copy");
        const auto invalidDestination=temporary.filePath("invalid-export");
        write(data,payload.chopped(1));
        run({"export","--input",meta,"--output",invalidDestination},1);
        require(!QFile::exists(invalidDestination),"Invalid recording published an export directory");
        write(data,payload);
        auto activeMeta=QJsonDocument::fromJson(sourceMeta).object();
        auto activeGlobal=activeMeta.value("global").toObject();
        activeGlobal.insert("signalscope:status","recording");
        activeMeta.insert("global",activeGlobal);
        write(meta,QJsonDocument(activeMeta).toJson());
        run({"export","--input",meta,"--output",invalidDestination},1);
        require(!QFile::exists(invalidDestination),"Active recording was exported");
        write(meta,sourceMeta);
        require(info.value("pairs").toInt()==100000 &&
                info.value("metadata").toObject().value("global").toObject().value("signalscope:status").toString()=="complete",
                "Original recording metadata is incomplete");
        require(runner.run(exe,{"--help"}).contains("all|bpsk,qpsk,4-fsk,ofdm,css"),
                "CLI help did not list the supported snapshot detector IDs");
        const auto scanAll=run({"scan","--input",meta,"--modulations","all"});
        const auto scanExplicit=run({"scan","--input",meta,"--modulations","bpsk,qpsk,4-fsk,ofdm,css"});
        require(scanAll==scanExplicit && scanAll.value("end_sample_exclusive").toInt()==100000,
                "The all selector differed from the explicit snapshot detector list");
        run({"scan","--input",meta,"--modulations","bpsk,unknown"},1);
        run({"scan","--input",meta,"--modulations","4-fsk,"},1);
        const auto audio=temporary.filePath("audio");
        const QStringList fm{"fm-audio","--input",meta,"--frequency","470623456","--output",audio};
        const auto exported=run(fm);
        const auto wav=read(audio+"/audio.wav");
        QDataStream header(wav);
        header.setByteOrder(QDataStream::LittleEndian);
        require(header.skipRawData(22)==22,"WAV header is truncated");
        quint16 channels=0;
        quint32 sampleRate=0,payloadBytes=0;
        header>>channels>>sampleRate;
        require(header.skipRawData(12)==12,"WAV data header is truncated");
        header>>payloadBytes;
        require(wav.startsWith("RIFF") && wav.mid(8,4)=="WAVE" && wav.mid(36,4)=="data" &&
                channels==1 && sampleRate==48000 && payloadBytes+44==wav.size() &&
                exported.value("audio_frames").toInt()==int(payloadBytes/2) && payloadBytes>0,
                "FM audio WAV header disagrees with its PCM payload");
        run(fm,1);
        require(read(audio+"/audio.wav")==wav && read(data)==payload,"FM export overwrote source or existing audio");
        const auto copy=temporary.filePath("copy");
        require(run({"replay","--input",meta,"--speed","0","--output",copy}).value("power").toObject()==generated,
                "Replay and synth power disagree");
        require(read(copy+"/capture.sigmf-data")==payload,"Replay changed raw bytes");
        const auto copyMetadata=QJsonDocument::fromJson(read(copy+"/capture.sigmf-meta")).object();
        require(!copyMetadata.value("captures").toArray().at(0).toObject().contains("core:datetime"),
                "Derived recording claims an RF capture timestamp");
        const auto slice=temporary.filePath("slice");
        run({"replay","--input",meta,"--start",".01","--duration",".02","--speed","0","--output",slice});
        require(read(slice+"/capture.sigmf-data")==payload.mid(40000,80000),"CU8 slice changed bytes");

        const auto wide=temporary.filePath("16-bit I Q");
        require(QDir().mkpath(wide),"Could not create CI16 fixture directory");
        const auto wideMeta=wide+"/capture.sigmf-meta";
        const auto widePath=wide+"/capture.sigmf-data";
        auto metadata=QJsonDocument::fromJson(read(meta)).object();
        auto global=metadata.value("global").toObject();
        global.insert("core:datatype","ci16_le");
        metadata.insert("global",global);
        write(wideMeta,QJsonDocument(metadata).toJson());
        QByteArray wideData;
        QDataStream words(&wideData,QIODevice::WriteOnly);
        words.setByteOrder(QDataStream::LittleEndian);
        for(const auto byte:payload)words<<qint16((2*static_cast<unsigned char>(byte)-255)*128);
        write(widePath,wideData);
        require(run({"inspect","--input",wideMeta}).value("pairs")==info.value("pairs"),"CI16 sample count differs");
        const auto wideSlice=temporary.filePath("16-bit slice");
        const auto clipped=run({"replay","--input",wideMeta,"--start",".01","--duration",".02","--speed","0","--output",wideSlice});
        require(clipped.value("power").toObject().value("pairs").toInt()==40000 &&
                read(wideSlice+"/capture.sigmf-data")==wideData.mid(80000,160000),"CI16 slice changed bytes");
        require(run({"inspect","--input",wideSlice+"/capture.sigmf-meta"}).value("metadata").toObject()
                .value("global").toObject().value("core:datatype").toString()=="ci16_le","CI16 slice changed datatype");
        const auto wideAudit=temporary.filePath("16-bit audit");
        const auto native=run({"monitor","--input",wideMeta,"--output",temporary.filePath("16-bit events"),"--audit-iq-output",wideAudit});
        require(native.value("sample_datatype").toString()=="ci16_le" &&
                native.value("source_pairs")==info.value("pairs") &&
                native.value("recording_processed_pairs")==info.value("pairs") &&
                read(wideAudit+"/capture.sigmf-data")==wideData,"CI16 monitor did not preserve the input");
        write(widePath,wideData.chopped(2));
        run({"inspect","--input",wideMeta},1);

        const auto audit=temporary.filePath(QStringLiteral("\u5bf9\u7167"));
        const auto events=temporary.filePath(QStringLiteral("\u4e8b\u4ef6"));
        const QStringList monitor{"monitor","--input",meta,"--output",events,"--audit-iq-output",audit};
        const auto monitored=run(monitor);
        const auto auditInfo=monitored.value("audit_iq").toObject();
        const auto normalized=[](const QJsonValue& path){return QDir::cleanPath(QDir::fromNativeSeparators(path.toString()));};
        require(normalized(monitored.value("source_input"))==QDir::cleanPath(meta) &&
                normalized(auditInfo.value("directory"))==QDir::cleanPath(audit) &&
                monitored.value("storage").toObject().value("completed").toInt(-1)==0 &&
                auditInfo.value("status").toString()=="complete" &&
                auditInfo.value("sample_count")==info.value("pairs"),"Invalid CU8 audit metadata");
        run(monitor,1);
        const auto slow=run({"monitor","--input",meta,"--output",temporary.filePath("slow replay"),"--replay-speed","0.5"});
        require(slow.value("replay_speed")==0.5 && slow.value("source_pairs")==info.value("pairs"),
                "Slower monitor replay changed the sample count or did not report its rate");
        run({"monitor","--input",meta,"--output",temporary.filePath("invalid speed"),"--replay-speed","0"},1);
        run({"monitor","--output",temporary.filePath("invalid live speed"),"--replay-speed","0.5"},1);
        require(read(audit+"/capture.sigmf-data")==payload,"Audit changed bytes or accepted overwrite");
        const auto nested=temporary.filePath("nested");
        const auto unbounded=temporary.filePath("unbounded");
        const auto tooLong=temporary.filePath("too-long");
        run({"monitor","--input",meta,"--output",nested,"--audit-iq-output",nested+"/audit"},1);
        run({"monitor","--output",events,"--audit-iq-output",unbounded},1);
        run({"monitor","--output",events,"--seconds","9","--audit-iq-output",tooLong},1);
        require(!QFile::exists(nested)&&!QFile::exists(unbounded)&&!QFile::exists(tooLong),"Invalid audit created files");
        run({"synth","--output",original},1);
        require(read(data)==payload,"Recorder overwrote original I/Q");
        run({"replay","--input",meta,"--start","-.1"},1);
        run({"replay","--input",meta,"--duration",".06"},1);
        run({"replay","--input",meta,"--speed","-1"},1);
        const auto bad=temporary.filePath("bad");
        run({"synth","--output",bad,"--rate","2000000oops"},1);
        run({"synth","--output",bad,"--seconds","nan"},1);
        run({"synth","--output",bad,"--unknown","x"},1);
        require(!QFile::exists(bad),"Invalid CLI arguments created files");
        auto damaged=QJsonDocument::fromJson(read(meta)).object();
        auto captures=damaged.value("captures").toArray();
        captures.append(QJsonObject{{"core:sample_start",500},{"core:frequency",480000000}});
        damaged.insert("captures",captures);
        const auto originalMetadata=read(meta);
        write(meta,QJsonDocument(damaged).toJson());
        run({"inspect","--input",meta},1);
        write(meta,originalMetadata);
        write(data,payload.chopped(2));
        run({"inspect","--input",meta},1);
        write(data,payload.chopped(1));
        run({"inspect","--input",meta},1);
        std::cout<<"CLI recording, replay, audit and failure handling passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
