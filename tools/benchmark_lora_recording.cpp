#include "offline_detect.hpp"
#include "signalscope/backend/iq/iq.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using signalscope::Json;

void saveReport(const QString& path,const Json& report) {
    QSaveFile file(path);
    const auto bytes=QByteArray::fromStdString(report.dump(2)+'\n');
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}

void benchmark(const QString& executable,const QString& metadata,const QString& output,
               const QStringList& thresholds,int workers) {
    const auto metadataPath=QFile(metadata).filesystemFileName();
    const auto info=signalscope::inspect_recording(metadataPath);
    const auto outputPath=QFile(output).filesystemFileName();
    if(std::filesystem::exists(outputPath) &&
       (std::filesystem::equivalent(outputPath,metadataPath) || std::filesystem::equivalent(outputPath,info.data_path) ||
        std::filesystem::equivalent(outputPath,QFile(executable).filesystemFileName())))
        throw std::invalid_argument("Benchmark report must be separate from the recording and detector");
    const double duration=double(info.pairs)/info.rate;
    if(!(duration>0))throw std::invalid_argument("The benchmark requires a nonempty recording");
    std::vector<double> parsedThresholds;
    for(const auto& text:thresholds) {
        bool valid=false;
        const auto threshold=text.toDouble(&valid);
        if(!valid || !std::isfinite(threshold))throw std::invalid_argument("Invalid benchmark threshold");
        parsedThresholds.push_back(threshold);
    }
    Json report{{"duration_seconds",duration},{"trials",Json::array()}};
    for(const auto threshold:parsedThresholds) {
        const QStringList arguments{"detect","--input",metadata,"--threshold-db",QString::number(threshold,'g',17),
                                    "--workers",QString::number(workers)};
        const auto started=std::chrono::steady_clock::now();
        const auto decoded=signalscope::tools::runOfflineDetect(executable,arguments);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        auto matches=Json::array();
        for(const auto& event:decoded.at("events"))
            if(event.at("evidence_level")=="lora_structure_match")matches.push_back(event);
        Json trial{{"threshold_db",threshold},{"elapsed_seconds",elapsed},{"workers",workers},
            {"processing_seconds",decoded.value("processing_seconds",Json())},
            {"search_work",decoded.value("search_work",Json::array())},
            {"wall_seconds_per_iq_second",elapsed/duration},{"structure_matches",std::move(matches)}};
        report["trials"].push_back(trial);
        saveReport(output,report);
        trial.erase("search_work");
        std::cout<<trial.dump()<<std::endl;
    }
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Measure offline LoRa detection throughput on a fixed I/Q recording.");
    const auto help=parser.addHelpOption();
    parser.addOptions({{"exe","Offline signalscope executable.","path"},
                       {"input","SigMF recording metadata.","path"},
                       {"output","Benchmark report file.","path"},
                       {"threshold","Detection threshold in dB. May be repeated.","db"},
                       {"workers","Detection workers.","count","3"}});
    if(!parser.parse(app.arguments())) {
        std::cerr<<parser.errorText().toStdString()<<'\n';
        return 2;
    }
    if(parser.isSet(help) || parser.isSet("help-all")) {
        std::cout<<parser.helpText().toStdString();
        return 0;
    }
    try {
        bool valid=false;
        const auto workers=parser.value("workers").toInt(&valid);
        if(!valid || workers<1 || workers>4 || parser.value("exe").isEmpty() || parser.value("input").isEmpty() ||
           parser.value("output").isEmpty() || parser.values("threshold").isEmpty() || !parser.positionalArguments().isEmpty())
            throw std::invalid_argument("Specify --exe, --input, --output, --threshold and optionally --workers 1..4");
        benchmark(QFileInfo(parser.value("exe")).absoluteFilePath(),QFileInfo(parser.value("input")).absoluteFilePath(),
                  parser.value("output"),parser.values("threshold"),workers);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
