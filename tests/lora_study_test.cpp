#include "plugins/desktop/research/builtin/lora_study.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QApplication>
#include <QComboBox>
#include "plugins/desktop/research/research_provider.hpp"
#include <QLocale>
#include <QTemporaryDir>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        if(argc<2)return 1;
        const QDir fixture(app.arguments()[1]);
        QTemporaryDir temporary;
        const auto metadata=temporary.filePath("capture.sigmf-meta");
        const auto samples=temporary.filePath("capture.sigmf-data");
        if(!QFile::copy(fixture.filePath("capture.json"),metadata) ||
           !QFile::copy(fixture.filePath("capture.cu8"),samples))return 2;
        const auto previousLocale=QLocale();
        QLocale::setDefault(QLocale(QLocale::English,QLocale::UnitedStates));
        const auto image=signalscope::desktop::renderLoRaStudy(metadata,11,{});
        auto controls=signalscope::desktop::loRaStudyControls(nullptr);
        std::unique_ptr<QWidget> owned(controls.widget);
        auto* bandwidth=owned->findChild<QComboBox*>("lora-study-bandwidth");
        auto* sf=owned->findChild<QComboBox*>("lora-study-spreading-factor");
        if(!bandwidth || !sf)return 12;
        bandwidth->setCurrentIndex(bandwidth->findData(125000));sf->setCurrentIndex(sf->findData(7));
        const auto specific=controls.read();
        if(specific.at("bandwidth_hz")!=125000 || specific.at("spreading_factor")!=7)return 13;
        bool invalidRejected=false;
        try { signalscope::desktop::renderLoRaStudy(metadata,11,{},std::nullopt,{{"spreading_factor",13}}); }
        catch(const std::invalid_argument&){invalidRejected=true;}
        if(!invalidRejected)return 14;
        QLocale::setDefault(QLocale(QLocale::German,QLocale::Germany));
        const auto regionalImage=signalscope::desktop::renderLoRaStudy(metadata,11,{});
        QLocale::setDefault(previousLocale);
        if(image.isNull() || image.width()<1000)return 3;
        const QRect spectrogram(100,109,578,203);
        const QRect timeAxis(26,321,666,37);
        const QRect evidence(790,415,578,203);
        if(regionalImage.isNull() || image.copy(spectrogram)!=regionalImage.copy(spectrogram) ||
           image.copy(timeAxis)==regionalImage.copy(timeAxis) ||
           image.copy(evidence)==regionalImage.copy(evidence)) {
            std::cerr<<"LoRa study did not localize its axis and evidence while preserving the spectrogram\n";
            return 10;
        }
        using signalscope::desktop::ResearchSampleRange;
        if(signalscope::desktop::renderLoRaStudy(metadata,11,{},ResearchSampleRange{128,119872}).isNull())return 7;
        bool rejected=false;
        try {signalscope::desktop::renderLoRaStudy(metadata,11,{},ResearchSampleRange{119999,2});}
        catch(const std::invalid_argument&) {rejected=true;}
        if(!rejected)return 8;
        bool missed=false;
        try {signalscope::desktop::renderLoRaStudy(metadata,11,{},ResearchSampleRange{119900,100});}
        catch(const std::runtime_error&) {missed=true;}
        if(!missed)return 9;
        if(argc>2 && !image.save(app.arguments()[2]))return 4;
        std::stop_source cancel;
        cancel.request_stop();
        if(!signalscope::desktop::renderLoRaStudy(metadata,11,cancel.get_token()).isNull())return 5;
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 6;
    }
}
