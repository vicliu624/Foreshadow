#include "receiver_dialog.hpp"
#include "desktop_theme.hpp"
#include "search_width_control.hpp"
#include "test_process.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
using amc_test::require;
using namespace signalscope;
using namespace signalscope::desktop;
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    applyDesktopTheme(app);
    try {
        require(argc>=2,"Expected independent plugin directory");qputenv("SIGNALSCOPE_RECEIVER_PLUGIN_PATH",argv[1]);
        QTemporaryDir temp;require(temp.isValid(),"Could not isolate settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        QTranslator language;if(argc>2){require(language.load(app.arguments()[2]),"Could not load Chinese translations");app.installTranslator(&language);app.setProperty("signalscope-language","zh-CN");}
        ReceiverSelection initial;initial.backend="test.receiver";initial.sampleRate=2000000;initial.frequency=5800000000ULL;
        initial.enableLora=false;initial.narrowband=true;initial.searchWidth=2000000;
        initial.settings={{"profile","eco"},{"preamp",false},{"calibration_ppm",2.5}};
        ReceiverDialog dialog(initial,nullptr);dialog.resize(1200,650);
        QString failure;bool checked=false;QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{
            auto* profile=dialog.findChild<QComboBox*>("receiver-plugin-profile");
            auto* preamp=dialog.findChild<QCheckBox*>("receiver-plugin-preamp");
            auto* correction=dialog.findChild<QDoubleSpinBox*>("receiver-plugin-calibration_ppm");
            if(!profile || !preamp || !correction)return;
            timer.stop();
            try {
                const auto buttons=dialog.findChild<QDialogButtonBox*>();auto* apply=buttons->button(QDialogButtonBox::Ok);
                require(!apply->isEnabled() && !preamp->isEnabled() && correction->value()==2.5,"Plugin dependency or restored value was not reflected in the UI");
                const auto* frequency=dialog.findChild<QDoubleSpinBox*>("receiver-frequency");
                require(frequency->value()==5800,"New RF range was truncated by the host");
                profile->setCurrentIndex(profile->findData(QStringLiteral("\"wide\"")));
                require(apply->isEnabled() && preamp->isEnabled(),"Changing a plugin-only parameter did not update validity");
                preamp->setChecked(true);
                dialog.findChild<QPushButton*>("receiver-use-recommended")->click();
                require(profile->currentData().toString()=="\"wide\"" && !preamp->isChecked() && correction->value()==0,"Recommendation did not apply plugin-specific fields");
                correction->setValue(2.5);preamp->setChecked(true);
                if(const auto path=qEnvironmentVariable("AMC_RECEIVER_PLUGIN_SCREENSHOT");!path.isEmpty())dialog.grab().save(path);
                checked=true;apply->click();
            }catch(const std::exception& e){failure=QString::fromUtf8(e.what());dialog.reject();}
        });
        QTimer::singleShot(10000,&dialog,&QDialog::reject);timer.start(20);
        require(dialog.exec()==QDialog::Accepted && checked && failure.isEmpty(),failure.isEmpty()?"Independent receiver settings did not apply":failure.toUtf8().constData());
        const auto selection=dialog.selection();
        require(selection.settings.at("preamp")==true && selection.applied.at("actual").at("settings").at("calibration_ppm")==2.5 && selection.frequency==5800000000ULL,
            "Plugin settings or actual configuration were lost on Apply");
        std::cout<<"Independent receiver controls, linkage, conflict prevention, recommendation and apply passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
