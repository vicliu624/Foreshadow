#include "search_width_control.hpp"
#include <QApplication>
#include <QComboBox>
#include <QSpinBox>
#include <stdexcept>
#include <iostream>
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void choose(QComboBox& combo,const QString& key) {
    const auto index=combo.findData(key);require(index>=0,"Width preset is missing");combo.setCurrentIndex(index);
    QMetaObject::invokeMethod(&combo,"activated",Qt::DirectConnection,Q_ARG(int,index));
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        signalscope::desktop::SearchWidthControl width;
        auto* presets=width.findChild<QComboBox*>("search-width-presets");
        auto* custom=width.findChild<QSpinBox*>("search-width-custom");
        require(presets && custom,"Search width controls are missing");
        width.setAvailableWidth(1036000);
        require(width.value()==1000 && presets->findData("5000")<0,"Unsafe width was offered as a preset");
        width.setValue(5000);
        require(width.value()==5000 && width.mode()=="custom" && !custom->isHidden(),"Invalid saved width was silently clamped");
        choose(*presets,"maximum");require(width.value()==1036 && width.mode()=="maximum","Maximum mode used the wrong margin");
        width.setAvailableWidth(1536000);require(width.value()==1536,"Maximum mode did not follow detector constraints");
        width.setAvailableWidth(1036999);require(width.value()==1036,"Maximum width rounded outside the usable passband");
        choose(*presets,"custom");custom->setValue(750);
        width.setAvailableWidth(500000);require(width.value()==750 && width.mode()=="custom","Custom value was silently modified");
        width.setSelection(500,"fixed");width.setAvailableWidth(250000);
        require(width.value()==500,"Fixed preset was silently modified after a constraint change");
        width.setSelection(250,"custom");require(width.mode()=="custom","Custom mode did not survive restoring a preset-sized number");
        width.setSelection(250,"maximum");width.setAvailableWidth(1036000);
        require(width.value()==1036 && width.mode()=="maximum","Saved maximum mode did not resume following constraints");
        width.setAvailableWidth(10000000);
        require(width.value()==10000 && width.recommendedValue()==10000 && presets->findData("10000")>=0,"RSP full bandwidth was restricted to a 1 MHz recommendation");
        width.setAvailableWidth(2560000);require(width.recommendedValue()==2560 && width.value()==2560,"AIO2 recommendation does not use its full span");
        std::cout<<"Search width presets, custom values and linked maximum passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
