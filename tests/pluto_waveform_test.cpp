#include "test_process.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace {
using namespace amc_test;

std::complex<double> sample(const QByteArray& data, int index) {
    const auto* bytes=data.constData()+4*index;
    return {double(qFromLittleEndian<qint16>(bytes)),double(qFromLittleEndian<qint16>(bytes+2))};
}

double frequency(const QByteArray& data,int index) {
    return std::arg(std::conj(sample(data,index))*sample(data,index+1))*3'000'000/(2*std::numbers::pi);
}

bool silent(QByteArrayView bytes) {
    return std::all_of(bytes.begin(),bytes.end(),[](char value){return value==0;});
}

void checkMode(const ChildRunner& runner,const QString& executable,const QString& cli,const QString& root,const QString& mode) {
    const auto dir=root+'/'+mode;
    const QStringList arguments{"--mode",mode,"--output",dir,"--seed","478"};
    runner.run(executable,arguments);
    const auto metadata=read(dir+"/waveform.json");
    const auto settings=QJsonDocument::fromJson(metadata).object();
    const auto iq=read(dir+"/waveform.iq");
    constexpr int total=606'208;
    const auto starts=settings.value("burst_start_samples").toArray();
    const int count=settings.value("burst_samples").toInt();
    require(settings.value("mode")==mode && settings.value("datatype")=="ci16_le" &&
            settings.value("sample_rate_hz").toInt()==3'000'000 && settings.value("sample_count").toInt()==total &&
            settings.value("iio_buffer_samples").toInt()==16384 && settings.value("offset_hz").toInt()==75000 &&
            starts==QJsonArray{60000,240000,420000} && iq.size()==4*total &&
            count==(mode=="css"?7104:100000),"Unexpected Pluto waveform layout");
    const QByteArrayView samples(iq);
    int end=0;
    for(const auto& entry:starts) {
        const auto start=entry.toInt();
        require(silent(samples.sliced(4*end,4*(start-end))),"Samples leaked outside a Pluto burst");
        require(samples.sliced(4*start,4*count)==samples.sliced(4*60000,4*count),
                "Repeated Pluto bursts have different samples");
        end=start+count;
    }
    require(silent(samples.sliced(4*end)),"Samples leaked after the final Pluto burst");
    constexpr int first=60000;
    if(mode=="css") {
        require(settings.value("format")=="generic_css" && frequency(iq,first+500)>frequency(iq,first+100) &&
                frequency(iq,first+8*768+500)<frequency(iq,first+8*768+100),"CSS chirp directions changed");
    } else if(mode=="ook") {
        require(std::abs(sample(iq,first))==8192 && std::abs(sample(iq,first+1250))==0,
                "OOK envelope did not switch off");
        const auto meta=dir+"/capture.sigmf-meta";
        write(meta,R"({"global":{"core:datatype":"ci16_le","core:sample_rate":3000000},
                       "captures":[{"core:sample_start":0,"core:frequency":478500000}]})");
        write(dir+"/capture.sigmf-data",iq);
        const auto output=runner.run(cli,{"replay-events","--input",meta,"--lora","0","--fsk","0",
                                         "--ook","1","--ask","0","--fm","0"});
        const auto replay=QJsonDocument::fromJson(output).object();
        const auto events=replay.value("recent_ask_events").toArray();
        require(replay.value("processed_pairs").toInt()==total &&
                std::any_of(events.begin(),events.end(),[](const auto& entry) {
                    return entry.toObject().value("evidence_level")=="ook_candidate";
                }),"Native replay missed the Pluto CI16 OOK waveform");
    } else {
        require(std::abs(frequency(iq,first+100)-79800)<150 &&
                std::abs(frequency(iq,first+1300)-70200)<150,"FSK frequency states changed");
    }
    runner.run(executable,arguments,1);
    require(read(dir+"/waveform.iq")==iq && read(dir+"/waveform.json")==metadata,
            "Duplicate output replaced an existing Pluto waveform");
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=3)return 2;
    try {
        QTemporaryDir temporary(QStringLiteral("amc-\u6ce2\u5f62 test-XXXXXX"));
        require(temporary.isValid(),"Could not create the Pluto waveform test directory");
        const auto root=QFileInfo(temporary.path()).absoluteFilePath();
        const ChildRunner runner(root);
        for(const auto* mode:{"css","ook","fsk"})
            checkMode(runner,app.arguments()[1],app.arguments()[2],root,QString::fromLatin1(mode));
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
