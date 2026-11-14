#include "test_process.hpp"
#include "desktop_recording_export.hpp"
#include "desktop_error.hpp"
#include "plugins/desktop/research/external_research.hpp"
#include "plugins/desktop/research/builtin/fm_research.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <iostream>
#include <stdexcept>

using namespace amc_test;

namespace {
QString synthesizeNarrowband(const QString& directory) {
    using namespace signalscope;
    Config config;
    config.rate=1750000;
    config.seconds=1;
    constexpr double tau=2*std::numbers::pi;
    Bytes samples;
    samples.reserve(config.rate*2);
    std::mt19937 random(581);
    int bit=0;
    double phase=0;
    for(std::uint32_t i=0;i<config.rate;++i) {
        const double t=double(i)/config.rate;
        std::complex<double> value{};
        if(t>=.05 && t<.2) {
            const auto symbol=i/729;
            if(i%729==0)bit=symbol<160?symbol%2:random()&1;
            phase+=tau*(125000+(bit?4800:-4800))/config.rate;
            value=std::polar(.20,phase);
        } else if((t>=.35 && t<.47) || (t>=.65 && t<.77)) {
            const bool ask=t>.5;
            const auto high=static_cast<unsigned>(t/.0025)%2!=0;
            value=std::polar(high?.20:ask?.08:0.,tau*(ask?175000:225000)*t);
        }
        value+=std::complex<double>((int(random()%7)-3)/128.,(int(random()%7)-3)/128.);
        for(const auto component:{value.real(),value.imag()})
            samples.push_back(static_cast<std::uint8_t>(std::lround(127.5+128*component)));
    }
    Recorder recorder(QFile(directory).filesystemFileName(),config,"synthetic-narrowband");
    recorder.append(samples);
    recorder.finish("complete");
    return directory+"/capture.sigmf-meta";
}

QString synthesizeBroadcastFm(const QString& directory) {
    using namespace signalscope;
    Config config;
    config.frequency=100800000;
    config.rate=1750000;
    config.seconds=.5;
    constexpr double tau=2*std::numbers::pi;
    Bytes samples;
    samples.reserve(config.rate);
    double phase=0;
    for(std::uint32_t i=0;i<config.rate/2;++i) {
        const auto t=tau*i/config.rate;
        phase+=tau*(155000+17000*std::sin(1900*t)+11000*std::sin(4200*t)+6700*std::cos(19000*t))/config.rate;
        const auto signal=std::polar(.22,phase)+std::polar(.28,350000*t);
        for(const auto component:{signal.real(),signal.imag()})
            samples.push_back(static_cast<std::uint8_t>(std::lround(127.5+128*component)));
    }
    Recorder recorder(QFile(directory).filesystemFileName(),config,"synthetic-fm");
    recorder.append(samples);
    recorder.finish("complete");
    return directory+"/capture.sigmf-meta";
}

void checkExportWorker(const QString& metadata,const QString& destination) {
    signalscope::desktop::RecordingExportJob job;
    const auto source=QFile(metadata).filesystemFileName();
    const auto target=QFile(destination).filesystemFileName();
    for(const bool expectFailure:{false,true}) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
        bool completed=false;
        const auto onFinished=[&](std::exception_ptr failure){
            completed=bool(failure)==expectFailure && !job.busy() &&
                      QThread::currentThread()==QCoreApplication::instance()->thread();
            loop.quit();
        };
        require(job.start(source,target,onFinished),"Desktop export worker did not start");
        require(job.busy() && !job.start(source,target,onFinished),"Desktop accepted an overlapping export");
        timeout.start(5000);
        loop.exec();
        require(completed,"Desktop export worker lost its completion or called it on the wrong thread");
    }
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto arguments=app.arguments();
    if(arguments.size()==7 && arguments[1]=="--input" && arguments[3]=="--output" && arguments[5]=="--language") {
        if(arguments[6]!="zh-CN" || !QFileInfo::exists(arguments[2]))return 5;
        if(arguments[4].endsWith(".bin")) {
            write(arguments[4],QByteArray::fromHex("00ff010d0a80"));
            return 0;
        }
        if(arguments[4].endsWith(".txt")) {
            if(arguments[2].endsWith("/input.bin")) {
                write(arguments[4],"Decoded: "+read(arguments[2]).toHex());
                return 0;
            }
            const auto bytes=arguments[2].endsWith("16-bit.sigmf-meta")
                ?QByteArray::fromHex("ff"):QStringLiteral("\u6d4b\u91cf\u62a5\u544a\n").toUtf8();
            write(arguments[4],bytes);
            return 0;
        }
        if(arguments[4].endsWith(".wav")) {
            auto wav=QByteArray::fromHex("524946462c00000057415645666d7420100000000100010080bb00000077010002001000646174610800000000000100ffff0000");
            if(arguments[2].endsWith("16-bit.sigmf-meta"))wav.chop(1);
            write(arguments[4],wav);
            return 0;
        }
        QImage image(160,100,QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        return image.save(arguments[4],"PNG")?0:5;
    }
    if(arguments.size()!=4 && arguments.size()!=6)return 2;
    try {
        const signalscope::SourceError overflow("OVERFLOW",{{"reason_code","stream_discontinuity"}});
        const auto overloadMessage=signalscope::desktop::error_summary(overflow,{});
        require(overloadMessage.contains("overflowed") && overloadMessage.contains("system load") && overloadMessage.contains("incomplete data"),
            "Receiver overflow was not explained to the user as incomplete input with load/USB diagnostics");
        require(!signalscope::desktop::error_summary(signalscope::SourceError("unknown",{}),{}).isEmpty(),"Missing receiver details broke error presentation");
        QTemporaryDir temporary;
        require(temporary.isValid(),"Could not create smoke-test directory");
        ChildRunner runner(temporary.path());
        const auto gui=arguments[1],cli=arguments[2];
        const auto settingsDirectory=temporary.filePath(QStringLiteral("\u8bbe\u7f6e settings"));
        const auto runGui=[&](QStringList options,int expected=0){
            options.prepend(settingsDirectory);
            options.prepend("--settings-dir");
            return runner.run(gui,options,expected);
        };
        const QDir fixture(QFileInfo(arguments[3]).absoluteFilePath());
        const auto recording=temporary.filePath(QStringLiteral("\u5f55\u5236 cafe\u0301 \U0001f4e1"));
        const auto output=temporary.filePath(QStringLiteral("\u4e8b\u4ef6"));
        runner.run(cli,{"synth","--output",recording,"--seconds",".1"});
        const auto raw=recording+"/capture.sigmf-data";
        const auto original=recording+"/capture.sigmf-meta";
        const auto before=read(raw);
        const auto timestampOutput=temporary.filePath("recording time checks");
        const auto timestampRecording=timestampOutput+"/clip";
        require(QDir().mkpath(timestampRecording),"Could not create a recording timestamp fixture");
        auto timestampMetadata=QJsonDocument::fromJson(read(original)).object();
        auto captures=timestampMetadata.value("captures").toArray();
        auto capture=captures.at(0).toObject();
        const QString timestamp="2026-11-06T22:30:00+02:00";
        capture["core:datetime"]=timestamp;
        captures[0]=capture;
        timestampMetadata["captures"]=captures;
        const auto timestampBytes=QJsonDocument(timestampMetadata).toJson();
        const auto timestampPath=timestampRecording+"/capture.sigmf-meta";
        write(timestampPath,timestampBytes);
        write(timestampRecording+"/capture.sigmf-data",before);
        const auto chineseTimeHeading=QStringLiteral("\u4e3b\u673a\u89c2\u5bdf\u65f6\u95f4");
        for(const auto& language:{QStringLiteral("en"),QStringLiteral("zh-CN")}) {
            const auto heading=language=="en"?QStringLiteral("Host observation time"):chineseTimeHeading;
            runGui({"--language",language,"--output",timestampOutput,"--smoke-seconds","1",
                    "--smoke-recording-time",timestamp,"--smoke-recording-time-heading",heading});
        }
        require(read(timestampPath)==timestampBytes && read(timestampRecording+"/capture.sigmf-data")==before,
                "Listing a timestamp changed the recording");
        const auto exportPath=temporary.filePath(QStringLiteral("\u5bfc\u51fa \U0001f4e1"));
        checkExportWorker(original,exportPath);
        require(read(exportPath+"/capture.sigmf-data")==before,"Desktop export worker changed source bytes");
        const auto wide=recording+"/16-bit";
        auto metadata=QJsonDocument::fromJson(read(original)).object();
        auto global=metadata.value("global").toObject();
        global.insert("core:datatype","ci16_le");
        metadata.insert("global",global);
        write(wide+".sigmf-meta",QJsonDocument(metadata).toJson());
        QByteArray wideData;
        wideData.reserve(before.size()*2);
        QDataStream words(&wideData,QIODevice::WriteOnly);
        words.setByteOrder(QDataStream::LittleEndian);
        for(const auto byte:before)words<<qint16((2*static_cast<unsigned char>(byte)-255)*128);
        write(wide+".sigmf-data",wideData);

        const QStringList recordings{original,original,wide+".sigmf-meta"};
        const QStringList languages{"en","zh-CN","zh-CN"};
        const auto chineseHeading=QStringLiteral("\u8c03\u5236\u5224\u5b9a");
        runner.setEnvironment("QT_SCALE_FACTOR","1.25");
        for(int i=0;i<recordings.size();++i) {
            const auto image=temporary.filePath(QString("view-%1.png").arg(i));
            runGui({"--language",languages[i],"--replay",recordings[i],"--output",output,
                            "--receiver",i==2?"sdrplay":"rtlsdr","--gain",i==0?"19.7":"auto",
                            "--device","0","--autostart","--smoke-seconds","2","--smoke-compact","--screenshot",image,
                            "--smoke-modulation-heading",i==0?QStringLiteral("Modulation"):chineseHeading});
            require(!QImage(image,"PNG").isNull(),"Desktop did not produce a valid screenshot");
        }
        // CTest's virtual platform can exercise a 1920x1080 workspace even on
        // a small host. A native window manager may constrain the archived app
        // to its actual screen, so that pass verifies the compact layout.
        // Keep the virtual large-screen assertion and native archive replay.
        const auto platform=qEnvironmentVariable("QT_QPA_PLATFORM");
        const bool virtualScreen=platform.isEmpty() || platform=="offscreen";
        runner.setEnvironment("QT_SCALE_FACTOR","1");
        runGui({"--language","system","--receiver","rtlsdr","--device","0","--output",output,
                "--smoke-seconds","1",virtualScreen?"--smoke-wide":"--smoke-compact"});
        runner.setEnvironment("QT_SCALE_FACTOR","1.25");
        runGui({"--language","system","--receiver","rtlsdr","--device","0","--output",output,
                "--smoke-seconds","1","--smoke-companion",QFileInfo(cli).absoluteFilePath()});
        runGui({"--receiver","sdrplay","--frequency","2000","--rate","2048000","--output",output,
                "--smoke-seconds","1"});
        runGui({"--receiver","sdrplay","--rate","1750000","--output",output,"--smoke-seconds","1"});
        runGui({"--receiver","sdrplay","--rate","10000000","--output",output,"--smoke-seconds","1"});
        runGui({"--receiver","sdrplay","--rate","0","--output",output,"--smoke-seconds","1"},2);
        require(read(raw)==before && read(wide+".sigmf-data")==wideData,"Desktop replay changed source I/Q");
        QDirIterator copies(output,{"*.sigmf-data"},QDir::Files,QDirIterator::Subdirectories);
        require(!copies.hasNext(),"Desktop replay copied source I/Q");
        runGui({"--language","en","--replay",recording+"/missing.sigmf-meta","--output",output,
                        "--receiver","rtlsdr","--device","0","--autostart","--smoke-seconds","1"},3);

        const auto study=recording+"/analysis";
        const auto fixtureData=read(fixture.filePath("capture.cu8"));
        write(study+".sigmf-meta",read(fixture.filePath("capture.json")));
        write(study+".sigmf-data",fixtureData);
        runner.run(gui,{"--settings-dir",study+".sigmf-meta","--smoke-seconds","1"},2);
        const auto profile=QDir(settingsDirectory).filePath("SignalScope/SignalScope.ini");
        {
            QSettings settings(profile,QSettings::IniFormat);
            settings.setValue("detection/lora",false);
            settings.sync();
            require(settings.status()==QSettings::NoError,"Could not seed the isolated settings profile");
        }
        runGui({"--language","zh-CN","--replay",study+".sigmf-meta","--autostart","--smoke-research-event",
                "--receiver","rtlsdr","--device","0","--threshold-db","11","--output",output,"--smoke-seconds","2"},3);
        {
            QSettings settings(profile,QSettings::IniFormat);
            settings.setValue("detection/lora",true);
            settings.sync();
            require(settings.status()==QSettings::NoError,"Could not update the isolated settings profile");
        }
        const auto loraReplay=QJsonDocument::fromJson(runner.run(cli,{"replay-events","--input",study+".sigmf-meta",
                                                                   "--threshold-db","11"})).object();
        require(loraReplay.value("state")=="replay_finished" &&
                loraReplay.value("processed_pairs")==loraReplay.value("source_pairs") &&
                !loraReplay.value("recent_lora_events").toArray().isEmpty(),"Headless replay lost the LoRa event");
        runGui({"--language","zh-CN","--analyze",study+".sigmf-meta","--analysis-plugin","lora-structure",
                        "--receiver","rtlsdr","--device","0","--threshold-db","11","--output",output,"--smoke-seconds","12"});
        runGui({"--language","zh-CN","--replay",study+".sigmf-meta","--autostart","--smoke-research-event",
                        "--receiver","rtlsdr","--device","0","--threshold-db","11","--output",output,"--smoke-seconds","12"});
        require(read(study+".sigmf-data")==fixtureData,"Desktop research changed source I/Q");
        runGui({"--language","en","--analyze",original,"--analysis-plugin","iq-overview",
                        "--receiver","rtlsdr","--device","0","--output",output,"--smoke-seconds","4"});
        const auto fmRange=signalscope::desktop::ResearchSampleRange{7000,20000};
        const signalscope::desktop::FmResearchOptions fmOptions{470623456.,75,50000};
        const auto previousLocale=QLocale();
        QLocale::setDefault(QLocale::c());
        const auto fm=signalscope::desktop::renderFmResearch(original,{},fmRange,fmOptions);
        QLocale::setDefault(QLocale(QLocale::German,QLocale::Germany));
        const auto regionalFm=signalscope::desktop::renderFmResearch(original,{},fmRange,fmOptions);
        QLocale::setDefault(previousLocale);
        require(fm.description.contains("470.623456 MHz") && regionalFm.description.contains("470,623456 MHz") &&
                fm.wav==regionalFm.wav,"Regional formatting changed FM audio or did not localize its center frequency");
        require(fm.rate==48000 && fm.channels==1 && fm.frames>=400 && fm.frames<=480,
                "FM research did not demodulate the requested recording range");
        const auto fmReference=temporary.filePath("fm reference");
        runner.run(cli,{"fm-audio","--input",original,"--output",fmReference,"--first-sample","7000",
                        "--samples","20000","--frequency","470623456","--deemphasis-us","75","--deviation-hz","50000"});
        require(fm.wav==read(fmReference+"/audio.wav") && fm.description.contains("75") &&
                fm.description.contains("50 kHz"),"Desktop FM parameters differ from the equivalent CLI analysis");
        runGui({"--language","zh-CN","--analyze",original,"--analysis-plugin","fm-audio",
                        "--receiver","rtlsdr","--device","0","--output",output,"--smoke-seconds","3"});
        const auto fmRecording=synthesizeBroadcastFm(temporary.filePath("FM replay"));
        runGui({"--language","zh-CN","--replay",fmRecording,"--autostart","--smoke-close-active",
                "--receiver","rtlsdr","--device","0","--output",output,"--smoke-seconds","5"});
        const auto fmReplay=QJsonDocument::fromJson(runner.run(cli,{"replay-events","--input",fmRecording,
                                                                  "--lora","0","--fsk","0","--ook","0","--ask","0"})).object();
        require(fmReplay.value("recent_fm_events").toArray().size()==1 &&
                fmReplay.value("recent_lora_events").toArray().isEmpty(),
                "Headless replay lost the FM event or ignored detector settings");
        runGui({"--language","zh-CN","--replay",fmRecording,"--autostart","--smoke-research-fm-event",
                        "--receiver","rtlsdr","--device","0","--output",output,"--smoke-seconds","12"});
        const auto narrowbandRecording=synthesizeNarrowband(temporary.filePath("Narrowband replay"));
        runGui({"--language","zh-CN","--replay",narrowbandRecording,"--autostart","--smoke-replay-narrowband",
                        "--receiver","rtlsdr","--device","0","--search-width","800000","--output",output,"--smoke-seconds","4"});
        const auto pluginDir=temporary.filePath("research plugins");
        require(QDir().mkpath(pluginDir),"Could not create a test plugin directory");
        const auto executable=QFileInfo(QCoreApplication::applicationFilePath()).suffix().isEmpty()
            ?QStringLiteral("plotter"):QStringLiteral("plotter.exe");
        const auto plugin=QDir(pluginDir).filePath(executable);
        require(QFile::copy(QCoreApplication::applicationFilePath(),plugin),"Could not copy the test plugin");
#ifndef Q_OS_WIN
        require(QFile::setPermissions(plugin,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner),
                "Could not make the test plugin executable");
#endif
        write(pluginDir+"/plotter.json",QJsonDocument(QJsonObject{{"api",1},{"id","external.test-plotter"},
              {"executable",executable},{"title",QJsonObject{{"en","Test plotter"},
                  {"zh-CN",QStringLiteral("\u6d4b\u8bd5\u7ed8\u56fe")},{"zh-Hans","Script fallback"}}}}).toJson());
        write(pluginDir+"/unsupported.json",QJsonDocument(QJsonObject{{"api",2},{"id","external.unsupported"},
              {"executable",executable},{"title",QJsonObject{{"en","Unsupported"}}}}).toJson());
        using signalscope::desktop::installResearchPlugin;
        using signalscope::desktop::ResearchInstallResult;
        const auto installedDir=temporary.filePath(QStringLiteral("\u63d2\u4ef6 installed \U0001f4e1"));
        const auto localizedTitle=QStringLiteral("\u6d4b\u8bd5\u7ed8\u56fe");
        QJsonObject localizedManifest{{"api",1},{"id","external.localized-title"},{"executable",executable},
            {"title",QJsonObject{{"en","Test plotter"},{"zh-Hans",localizedTitle},{"fr",QString(81,u'x')}}}};
        const auto localizedPath=pluginDir+"/localized.json";
        write(localizedPath,QJsonDocument(localizedManifest).toJson());
        require(installResearchPlugin(localizedPath,installedDir)==ResearchInstallResult::invalid_package,
                "Plugin installation accepted an oversized title in an inactive language");
        localizedManifest["title"]=QJsonObject{{"en","Test plotter"},{"zh-Hans",localizedTitle}};
        write(localizedPath,QJsonDocument(localizedManifest).toJson());
        const auto localized=signalscope::desktop::discoverResearchPlugins(pluginDir,"zh-CN");
        require(localized.size()==2 && localized.front().plugin.id=="external.localized-title" &&
                localized.front().plugin.title==localizedTitle &&
                localized.front().language=="zh-CN","Script-based title fallback changed the plugin language contract");
        require(QFile::remove(localizedPath),"Could not remove the test manifest");
        require(installResearchPlugin(pluginDir+"/unsupported.json",installedDir)==ResearchInstallResult::invalid_package &&
                !QFileInfo::exists(installedDir),"Unsupported research plugin was installed");
        require(installResearchPlugin(pluginDir+"/plotter.json",installedDir)==ResearchInstallResult::installed &&
                installResearchPlugin(pluginDir+"/plotter.json",installedDir)==ResearchInstallResult::duplicate_id,
                "Plugin installation failed or accepted a duplicate ID");
        const auto installed=signalscope::desktop::discoverResearchPlugins(installedDir,"zh-CN");
        require(installed.size()==1 && installed.front().plugin.managed &&
                installed.front().plugin.title==QStringLiteral("\u6d4b\u8bd5\u7ed8\u56fe"),
                "Installed research plugin lost its localized title");
        runGui({"--language","zh-CN","--research-plugin-dir",pluginDir,"--analyze",original,
                        "--analysis-plugin","external.unsupported","--output",output,"--smoke-seconds","2"},2);
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",original,
                        "--analysis-plugin","external.test-plotter","--output",output,"--smoke-seconds","3"});
        using signalscope::desktop::removeResearchPlugin;
        require(removeResearchPlugin("external.test-plotter",pluginDir).isEmpty() &&
                QFileInfo::exists(plugin),"Uninstall changed a manually managed plugin");
        require(removeResearchPlugin("../outside",installedDir).isEmpty(),"Uninstall accepted a non-plugin path");
        const auto archive=removeResearchPlugin("external.test-plotter",installedDir);
        require(!archive.isEmpty() && QFileInfo::exists(QDir(archive).filePath("plugin.json")) &&
                read(QDir(archive).filePath(executable))==read(plugin) &&
                signalscope::desktop::discoverResearchPlugins(installedDir,"en").isEmpty(),
                "Uninstall did not preserve the package outside the loading directory");
        require(installResearchPlugin(pluginDir+"/plotter.json",installedDir)==ResearchInstallResult::installed,
                "A removed plugin could not be reinstalled");
        write(pluginDir+"/report.json",QJsonDocument(QJsonObject{{"api",1},{"id","external.test-report"},
              {"output","text"},{"executable",executable},{"title",QJsonObject{{"en","Test report"}}}}).toJson());
        require(installResearchPlugin(pluginDir+"/report.json",installedDir)==ResearchInstallResult::installed,
                "Text research plugin could not be installed");
        const auto reports=signalscope::desktop::discoverResearchPlugins(installedDir,"zh-CN");
        require(reports.size()==2 && reports.back().plugin.title=="Test report",
                "A plugin without a localized title did not fall back to English");
        const auto report=reports.back().render(original,{});
        require(std::holds_alternative<QString>(report) &&
                std::get<QString>(report)==QStringLiteral("\u6d4b\u91cf\u62a5\u544a\n"),
                "Research report lost its UTF-8 text");
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",original,
                        "--analysis-plugin","external.test-report","--output",output,"--smoke-seconds","3"});
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",wide+".sigmf-meta",
                        "--analysis-plugin","external.test-report","--output",output,"--smoke-seconds","2"},3);
        QJsonObject artifactManifest{{"api",1},{"id","external.test-artifact"},{"executable",executable},
                                     {"title",QJsonObject{{"en","Test binary artifact"}}}};
        const auto artifactManifestPath=pluginDir+"/artifact.json";
        artifactManifest.insert("output",QJsonObject{{"format","../unknown"},{"version",2}});
        write(artifactManifestPath,QJsonDocument(artifactManifest).toJson());
        require(installResearchPlugin(artifactManifestPath,installedDir)==ResearchInstallResult::invalid_package,
                "Invalid artifact format identifier was accepted");
        artifactManifest.insert("output",QJsonObject{{"format","test.telemetry-frames"},{"version",2}});
        write(artifactManifestPath,QJsonDocument(artifactManifest).toJson());
        require(installResearchPlugin(artifactManifestPath,installedDir)==ResearchInstallResult::installed,
                "Research plugin with a custom output format could not be installed");
        const auto artifactPlugins=signalscope::desktop::discoverResearchPlugins(installedDir,"zh-CN");
        const auto artifactPlugin=std::find_if(artifactPlugins.begin(),artifactPlugins.end(),[](const auto& entry) {
            return entry.plugin.id=="external.test-artifact";
        });
        require(artifactPlugin!=artifactPlugins.end(),"Custom output format was not discovered");
        const auto result=artifactPlugin->render(original,{});
        const auto* artifact=std::get_if<signalscope::desktop::ResearchArtifact>(&result);
        require(artifact && artifact->format.id=="test.telemetry-frames" && artifact->format.version==2 &&
                artifact->bytes==QByteArray::fromHex("00ff010d0a80"),"Custom artifact lost its descriptor or changed bytes");
        const QDir artifactExport(temporary.filePath(QStringLiteral("\u7814\u7a76 export")));
        require(QDir().mkpath(artifactExport.absolutePath()),"Could not create artifact export folder");
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",original,
                "--analysis-plugin","external.test-artifact","--output",output,"--smoke-seconds","2",
                "--smoke-export-analysis",artifactExport.absolutePath()});
        require(read(artifactExport.filePath("external.test-artifact.test.telemetry-frames.v2.bin"))==artifact->bytes,
                "Desktop export changed the custom artifact bytes or format identity");
        const auto decoderManifest=pluginDir+"/decoder.json";
        write(decoderManifest,QJsonDocument(QJsonObject{{"api",2},{"id","external.test-decoder"},
              {"executable",executable},{"title",QJsonObject{{"en","Test artifact decoder"}}},{"output","text"},
              {"input",QJsonObject{{"format","test.telemetry-frames"},{"version",2}}}}).toJson());
        require(installResearchPlugin(decoderManifest,installedDir)==ResearchInstallResult::installed,
                "Artifact decoder could not be installed");
        const auto processors=signalscope::desktop::discoverResearchPlugins(installedDir,"zh-CN");
        const auto decoder=std::find_if(processors.begin(),processors.end(),[](const auto& entry) {
            return entry.plugin.id=="external.test-decoder";
        });
        require(decoder!=processors.end() && decoder->accepts(artifact->format) &&
                !decoder->accepts({"test.telemetry-frames",1}) && !decoder->accepts({"test.other",2}),
                "The artifact decoder did not match format and version exactly");
        const QDir chainExport(temporary.filePath("chain export"));
        require(QDir().mkpath(chainExport.absolutePath()),"Could not create chain export folder");
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",original,
                "--analysis-plugin","external.test-artifact","--output",output,"--smoke-seconds","3",
                "--smoke-analysis-processor","external.test-decoder","--smoke-export-analysis",chainExport.absolutePath(),
                "--smoke-export-previous-analysis",chainExport.absolutePath()});
        require(read(chainExport.filePath("external.test-decoder.txt"))=="Decoded: 00ff010d0a80",
                "Desktop did not pass the original artifact bytes to the compatible decoder");
        require(read(chainExport.filePath("external.test-artifact.test.telemetry-frames.v2.bin"))==artifact->bytes,
                "Restored input artifact lost its format, version or bytes");
        write(pluginDir+"/audio.json",QJsonDocument(QJsonObject{{"api",1},{"id","external.test-audio"},
              {"output","wav"},{"executable",executable},{"title",QJsonObject{{"en","Test audio"}}}}).toJson());
        require(installResearchPlugin(pluginDir+"/audio.json",installedDir)==ResearchInstallResult::installed,
                "Audio research plugin could not be installed");
        const auto audioPlugins=signalscope::desktop::discoverResearchPlugins(installedDir,"zh-CN");
        const auto audioPlugin=std::find_if(audioPlugins.begin(),audioPlugins.end(),[](const auto& entry){
            return entry.plugin.id=="external.test-audio";
        });
        require(audioPlugin!=audioPlugins.end(),"Audio plugin was not discovered");
        const auto audio=std::get<signalscope::desktop::ResearchAudio>(audioPlugin->render(original,{}));
        require(audio.rate==48000 && audio.channels==1 && audio.frames==4,"Research WAV format was not preserved");
        bool rejectedAudio=false;
        try {audioPlugin->render(wide+".sigmf-meta",{});}
        catch(const std::runtime_error&) {rejectedAudio=true;}
        require(rejectedAudio,"A truncated research WAV was accepted");
        runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",original,
                        "--analysis-plugin","external.test-audio","--output",output,"--smoke-seconds","3"});
        if(arguments.size()==6) {
            const auto powerBinary=QFileInfo(arguments[4]).absoluteFilePath();
            const auto powerTarget=QDir(pluginDir).filePath(QFileInfo(powerBinary).fileName());
            require(QFile::copy(powerBinary,powerTarget),"Could not copy the power report plugin");
            const auto manifestBytes=read(QFileInfo(arguments[5]).absoluteFilePath());
            for(const auto& resource:QJsonDocument::fromJson(manifestBytes).object().value("resources").toArray()) {
                const auto relative=resource.toString();require(!relative.isEmpty() && !QDir::isAbsolutePath(relative) && QDir::cleanPath(relative)==relative && !relative.startsWith("../"),"Invalid fixture resource path");
                const auto destination=QDir(pluginDir).filePath(relative);
                require(QDir().mkpath(QFileInfo(destination).path()) && QFile::copy(QFileInfo(powerBinary).dir().filePath(relative),destination),"Could not copy a declared power-report resource");
            }
            const auto powerManifest=QDir(pluginDir).filePath("power-report.json");
            write(powerManifest,manifestBytes);
            require(installResearchPlugin(powerManifest,installedDir)==ResearchInstallResult::installed,
                    "Power report plugin could not be installed");
            const auto available=signalscope::desktop::discoverResearchPlugins(installedDir,"en");
            const auto power=std::find_if(available.begin(),available.end(),[](const auto& entry){
                return entry.plugin.id=="external.amc-power-report";
            });
            require(power!=available.end() && power->supportsRange,"Power plugin did not declare sample ranges");
            const auto ranged=power->render(original,{},signalscope::desktop::ResearchSampleRange{7000,12000});
            require(std::get<QString>(ranged).contains("First analyzed sample: 7000") &&
                    std::get<QString>(ranged).contains("Analyzed I/Q pairs: 12000"),
                    "The research host did not pass the selected range");
            runGui({"--language","zh-CN","--research-plugin-dir",installedDir,"--analyze",wide+".sigmf-meta",
                            "--analysis-plugin","external.amc-power-report","--output",output,"--smoke-seconds","3"});
        }
        require(read(raw)==before,"I/Q overview changed source data");
        std::cout<<"Desktop replay, analysis and source data protection passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
