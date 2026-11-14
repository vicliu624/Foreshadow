#include "plugins/desktop/research/builtin/iq_overview.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try {
        QTemporaryDir temporary;
        if(!temporary.isValid())return 1;
        using namespace signalscope;
        for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le}) {
            const auto path=QFile(temporary.filePath(sample_datatype(format))).filesystemFileName();
            Config config;
            config.seconds=.01;
            const Bytes samples(format==SampleFormat::cu8?Bytes{128,144}:Bytes{128,255,128,4});
            Bytes data;
            for(std::uint32_t n=0;n<config.rate/100;++n)data.insert(data.end(),samples.begin(),samples.end());
            Recorder recording(path,config,"synthetic-test",format);
            recording.append(data);
            recording.finish("complete");
            const auto metadata=temporary.filePath(QString::fromLatin1(sample_datatype(format))+"/capture.sigmf-meta");
            const auto image=desktop::renderIqOverview(metadata,{});
            if(image.isNull() || image.width()<1000 || image.height()<900)return 2;
            if(format==SampleFormat::cu8) {
                const auto previous=QLocale();
                QLocale::setDefault(QLocale(QLocale::English,QLocale::UnitedStates));
                const auto english=desktop::renderIqOverview(metadata,{});
                QLocale::setDefault(QLocale(QLocale::German,QLocale::Germany));
                const auto german=desktop::renderIqOverview(metadata,{});
                QLocale::setDefault(previous);
                const QRect waveform(100,109,578,203);
                const QRect timeAxis(26,321,666,37);
                if(english.copy(timeAxis)==german.copy(timeAxis) ||
                   english.copy(waveform)!=german.copy(waveform)) {
                    std::cerr<<"Regional formatting did not change only the waveform annotations\n";
                    return 7;
                }
            }
            const auto stride=bytes_per_pair(format);
            constexpr desktop::ResearchSampleRange range{2345,8193};
            const auto segment=temporary.filePath(QString::fromLatin1(sample_datatype(format))+"-segment");
            Recorder clipped(QFile(segment).filesystemFileName(),config,"synthetic-test",format);
            clipped.append(std::span(data).subspan(range.first*stride,range.count*stride));
            clipped.finish("complete");
            const auto selected=desktop::renderIqOverview(metadata,{},range);
            const auto independent=desktop::renderIqOverview(segment+"/capture.sigmf-meta",{});
            const QRect panels(0,65,image.width(),image.height()-65);
            if(selected.isNull() || independent.isNull() || selected.copy(panels)!=independent.copy(panels)) {
                std::cerr<<"Selected I/Q panels differ from independently clipped recording\n";
                return 5;
            }
            bool rejected=false;
            try {desktop::renderIqOverview(metadata,{},desktop::ResearchSampleRange{config.rate/100-1,16});}
            catch(const std::invalid_argument&) {rejected=true;}
            if(!rejected)return 6;
            std::stop_source cancellation;
            cancellation.request_stop();
            if(!desktop::renderIqOverview(metadata,cancellation.get_token()).isNull())return 3;
        }
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 4;
    }
}
