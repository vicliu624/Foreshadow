#include "desktop_tools.hpp"
#include "receiver_dialog.hpp"
#include "search_width_control.hpp"
#include "desktop_theme.hpp"
#include "test_process.hpp"
#include "signalscope/adapters/modulation/descriptor.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTableWidget>
#include <QSplitter>
#include <QEventLoop>
#include <QHeaderView>
#include <string_view>

namespace {
using amc_test::require;
using signalscope::desktop::ReceiverDialog;
constexpr auto invalidReply = "<invalid reply>";

void checkFailure(const QString& expected, const QString& detail) {
    ReceiverDialog dialog({"rtlsdr",0,{},std::nullopt,0,{}},nullptr);
    auto* status=dialog.findChild<QLabel*>("receiver-status");
    auto* buttons=dialog.findChild<QDialogButtonBox*>();
    require(status && buttons,"Receiver dialog controls are missing");
    QTimer probe;
    QObject::connect(&probe,&QTimer::timeout,&dialog,[&] {
        if(status->text()==expected)dialog.accept();
    });
    probe.start(20);
    QTimer::singleShot(7000,&dialog,&QDialog::reject);
    require(dialog.exec()==QDialog::Accepted,"Receiver dialog did not show the expected failure");
    require(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Failed receiver query left Apply enabled");
    require(!status->toolTip().isEmpty() && status->toolTip().contains(detail),
            "Receiver dialog lost or failed to escape diagnostic details");
}

void checkQueryFailures(const QString& catalog) {
    const auto catalogPath=QFileInfo(catalog).absoluteFilePath();
    QTemporaryDir layout;
    require(layout.isValid(),"Could not isolate the receiver query executables");
    QDir root(layout.path());
    require(root.mkpath("stage/app/bin"),"Could not create the test executable layout");
    const QFileInfo original(QCoreApplication::applicationFilePath());
    const auto copy=root.filePath("stage/app/bin/"+original.fileName());
    require(QFile::copy(original.absoluteFilePath(),copy),"Could not copy the receiver dialog test");
    amc_test::ChildRunner runner(layout.path());
    runner.run(copy,{"--expect-missing-utility"});
    runner.run(copy,{"--expect-missing-utility",catalogPath});
    const auto utility=root.filePath(original.suffix().isEmpty()?"stage/app/bin/signalscope":"stage/app/bin/signalscope.exe");
    require(QFile::copy(original.absoluteFilePath(),utility),"Could not stage the malformed receiver utility");
    runner.run(copy,{"--expect-invalid-reply"});
    runner.run(copy,{"--expect-invalid-reply",catalogPath});
}
void checkAio2() {
    QTemporaryDir settings;require(settings.isValid(),"Could not isolate AIO2 settings");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    signalscope::desktop::ReceiverSelection initial{"rtlsdr",0,{}};initial.sampleRate=2000000;initial.searchWidth=10000000;
    initial.lora.bandwidth_candidates={125000,250000,500000};initial.snapshots=true;
    ReceiverDialog dialog(initial,nullptr);dialog.resize(1200,650);
    auto* rates=dialog.findChild<QComboBox*>("receiver-sample-rate");auto* input=dialog.findChild<QComboBox*>("receiver-input-mode");
    auto* gain=dialog.findChild<QComboBox*>("receiver-gain");auto* frequency=dialog.findChild<QDoubleSpinBox*>("receiver-frequency");
    auto* width=dialog.findChild<signalscope::desktop::SearchWidthControl*>("receiver-search-width");
    auto* recommended=dialog.findChild<QPushButton*>("receiver-use-recommended");auto* summary=dialog.findChild<QLabel*>("receiver-coverage-summary");
    auto* status=dialog.findChild<QLabel*>("receiver-status");auto* apply=dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    QString failure;bool checked=false;QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{
        if(!rates->isEnabled() || !status->text().contains("AIO V2"))return;timer.stop();
        try {
            require(!apply->isEnabled() && !input->isHidden(),"AIO2 identity or conflicting saved width was ignored");
            for(auto rate:{2048000u,2400000u,2560000u,3200000u})require(rates->findData(rate)>=0,"AIO2 hardware rate is missing");
            recommended->click();require(apply->isEnabled() && rates->currentData().toUInt()==2560000 && width->value()==2560,"AIO2 recommendation is not the full 2.56 MHz span");
            frequency->setValue(7.1);require(apply->isEnabled() && !gain->isEnabled(),"AIO2 automatic HF input did not deactivate tuner gain");
            input->setCurrentIndex(input->findData("tuner"));require(!apply->isEnabled(),"AIO2 HF was accepted through the tuner");
            input->setCurrentIndex(input->findData("auto"));require(apply->isEnabled(),"AIO2 automatic HF repair failed");
            if(const auto screenshot=qEnvironmentVariable("AMC_RECEIVER_DIALOG_SCREENSHOT");!screenshot.isEmpty())dialog.grab().save(screenshot+"-hf.png");
            frequency->setValue(470.5);require(gain->isEnabled(),"AIO2 tuner gain remained inactive after leaving HF");
            rates->setCurrentIndex(rates->findData(3200000u));require(apply->isEnabled() && summary->text().contains("2.56"),"AIO2 high rate is blocked or its USB risk is hidden");
            recommended->click();QCoreApplication::processEvents();
            if(const auto screenshot=qEnvironmentVariable("AMC_RECEIVER_DIALOG_SCREENSHOT");!screenshot.isEmpty())dialog.grab().save(screenshot);
            checked=true;apply->click();
        }catch(const std::exception& e){failure=QString::fromUtf8(e.what());dialog.reject();}
    });
    timer.start(20);QTimer::singleShot(15000,&dialog,&QDialog::reject);
    const auto result=dialog.exec();if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
    require(checked && result==QDialog::Accepted,"Physical AIO2 settings did not apply");
    const auto chosen=dialog.selection();require(chosen.applied.at("actual").at("sample_rate_hz")==2560000 && chosen.applied.at("actual").at("input_mode")=="tuner","AIO2 configuration readback differs");
}
}

int main(int argc,char** argv) {
    if(argc>1 && std::string_view(argv[1])=="devices") {
        std::cout<<invalidReply<<'\n';
        return 0;
    }
    QApplication app(argc,argv);
    signalscope::desktop::applyDesktopTheme(app);
    const auto arguments=app.arguments();
    if(arguments.size()<2 || arguments.size()>3)return 2;
    try {
        const auto mode=arguments[1];
        if(mode=="--check-query-failures" && arguments.size()==3) {
            checkQueryFailures(arguments[2]);
            return 0;
        }
        QTranslator translator;
        const bool localized=arguments.size()==3;
        if(localized)
            require(translator.load(arguments[2]) && app.installTranslator(&translator),
                    "Could not install the compiled desktop translation");
        app.setProperty("signalscope-language",localized?"zh-CN":"en");
        if(mode=="--aio2"){checkAio2();return 0;}
        if(mode=="--expect-missing-utility") {
            require(!QFileInfo::exists(signalscope::desktop::companion_cli()),"Isolated receiver utility unexpectedly exists");
            checkFailure(localized?QStringLiteral("无法启动接收机查询程序。请检查 Foreshadow 的安装。"):
                         QStringLiteral("Could not start the receiver utility. Check the Foreshadow installation."),"<pre>");
            return 0;
        }
        if(mode=="--expect-invalid-reply") {
            checkFailure(localized?QStringLiteral("接收机查询程序返回了无效响应。请检查 Foreshadow 的安装。"):
                         QStringLiteral("The receiver utility returned an invalid response. Check the Foreshadow installation."),"&lt;invalid reply&gt;");
            return 0;
        }
        QTemporaryDir settings;
        require(settings.isValid(),"Could not isolate receiver settings");
        amc_test::ChildRunner runner(settings.path());
        const auto cli=signalscope::desktop::companion_cli();
        const auto response=QJsonDocument::fromJson(runner.run(cli,{"devices","--receiver","sdrplay"})).object();
        const bool realHardware=qEnvironmentVariableIntValue("AMC_TEST_REAL_SDRPLAY")==1;
        QString expectedSerial="AMC_TEST";
        if(realHardware) {
            const auto devices=response.value("devices").toArray();
            require(!devices.isEmpty(),"The explicitly requested physical RSPdx-R2 is not connected");
            expectedSerial=devices.first().toObject().value("identification").toObject().value("serial").toString();
            require(!expectedSerial.isEmpty(),"Physical SDRplay identity is missing");
        } else if(qEnvironmentVariableIntValue("AMC_TEST_REMOTE_SERVICE")!=1) {
            if(response.value("backend")=="soapy-sdrplay")return 77;
            require(response.value("reason_code")=="driver_missing","Could not isolate the simulated SDRplay driver");
            qputenv("SOAPY_SDR_PLUGIN_PATH",QFile::encodeName(app.arguments()[1]));
        }
        app.setOrganizationName("AMC Tests");
        app.setApplicationName("Receiver Settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        QSettings().setValue("receiver/sdrplay/sample_rate_hz",2'048'000u);

        signalscope::desktop::ReceiverSelection initial{"sdrplay",9,"Antenna B",std::nullopt,0,expectedSerial};
        const auto serviceEndpoint=qEnvironmentVariable("FORESHADOW_SDRPLAY_SERVICE");
        initial.settings["service_endpoint"]=serviceEndpoint.toStdString();
        // Saved explicit connection context must reach discovery, probe and
        // apply even when the process default points at an unavailable service.
        qputenv("FORESHADOW_SDRPLAY_SERVICE","tcp://127.0.0.1:1");
        initial.lora.bandwidth_candidates={125000,250000,500000};
        initial.searchWidth=5000000;
        initial.snapshots=true;
        ReceiverDialog dialog(initial,nullptr);
        dialog.resize(1200,650);
        auto* rates=dialog.findChild<QComboBox*>("receiver-sample-rate");
        auto* antennas=dialog.findChild<QComboBox*>("receiver-antenna");
        auto* buttons=dialog.findChild<QDialogButtonBox*>();
        auto* status=dialog.findChild<QLabel*>("receiver-status");
        auto* feedback=dialog.findChild<QLabel*>("receiver-configuration-feedback");
        auto* frequency=dialog.findChild<QDoubleSpinBox*>("receiver-frequency");
        auto* gain=dialog.findChild<QComboBox*>("receiver-gain");
        auto* width=dialog.findChild<signalscope::desktop::SearchWidthControl*>("receiver-search-width");
        auto* widthProblem=dialog.findChild<QLabel*>("receiver-width-problem");
        auto* advanced=dialog.findChild<QCheckBox*>("receiver-advanced-rates");
        auto* recommended=dialog.findChild<QPushButton*>("receiver-use-recommended");
        auto* repairWidth=dialog.findChild<QPushButton*>("receiver-repair-width");
        auto* repairRate=dialog.findChild<QPushButton*>("receiver-repair-rate");
        auto* repairAntenna=dialog.findChild<QPushButton*>("receiver-repair-antenna");
        auto* refresh=dialog.findChild<QPushButton*>("receiver-refresh");
        auto* table=dialog.findChild<QTableWidget*>("receiver-modulation-table");
        auto* panels=dialog.findChild<QSplitter*>("receiver-settings-panels");
        auto* service=dialog.findChild<QLineEdit*>("receiver-connection-service_endpoint");
        require(rates && antennas && buttons && status && feedback && frequency && gain && width && widthProblem &&
            advanced && recommended && repairWidth && repairRate && repairAntenna && table && panels && refresh && service &&
            service->text()==serviceEndpoint,"Receiver dialog controls or saved connection context are missing");
        auto* apply=buttons->button(QDialogButtonBox::Ok);
        bool valid=false;QString failure;bool refreshed=false;
        QTimer probe;
        QObject::connect(&probe,&QTimer::timeout,&dialog,[&]{
            if(!rates->isEnabled() || !status->text().contains("RSPdx-R2"))return;
            probe.stop();
            try {
                if(!refreshed) {
                    refresh->click();require(table->rowCount()==0 && !apply->isEnabled(),"Device refresh retained a stale compatible assessment");
                    refreshed=true;probe.start(20);return;
                }
                require(!apply->isEnabled() && rates->currentData().toUInt()==2048000 && rates->findData(10000000u)>=0 &&
                    !rates->isEditable() && feedback->isHidden() && feedback->styleSheet().isEmpty(),
                    "An incompatible saved rate remained applicable or technical details were highlighted");
                require(antennas->currentData().toString()=="Antenna B" && width->value()==5000 &&
                    widthProblem->text().contains("5000") && widthProblem->text().contains("2048"),
                    "Saved search-width conflict lost its requested value or numeric explanation");
                require(repairRate->isHidden(),"A valid fractional rate was incorrectly marked for repair");
                QCoreApplication::processEvents();
                if(const auto screenshot=qEnvironmentVariable("AMC_RECEIVER_DIALOG_SCREENSHOT");!screenshot.isEmpty())
                    dialog.grab().save(screenshot+"-conflict.png");
                repairWidth->click();require(width->value()==2048 && apply->isEnabled(),"One-click width repair did not make the configuration valid");
                require(table->rowCount()==static_cast<int>(signalscope::modulationDescriptors().size()) && table->item(0,0)->data(Qt::UserRole)=="lora","Modulation table did not populate the supported detectors");
                for(int column=0;column<3;++column)
                    require(table->columnWidth(column)>=table->horizontalHeader()->fontMetrics().horizontalAdvance(
                        table->horizontalHeaderItem(column)->text())+12,"The desktop theme clips a modulation table header");
                antennas->setCurrentIndex(antennas->findData("Antenna C"));
                require(!apply->isEnabled(),"Antenna C conflict allowed Apply");
                frequency->setValue(200.0);require(apply->isEnabled(),"Antenna C was rejected at its valid frequency boundary");
                frequency->setValue(470.5);repairAntenna->click();
                require(apply->isEnabled() && antennas->currentData().toString()!="Antenna C","Antenna repair did not preserve frequency");
                advanced->setChecked(true);require(rates->findData(10000000u)>=0 && rates->isEditable(),"Advanced hardware capability was lost");
                rates->setCurrentIndex(rates->findData(10000000u));
                require(apply->isEnabled() && feedback->text().contains("40.00"),"A hardware-supported wideband rate is still blocked");
                rates->setEditText("not a sample rate");require(!apply->isEnabled(),"Malformed custom sample rate allowed Apply");
                rates->setCurrentIndex(rates->findData(10000000u));
                antennas->setCurrentIndex(antennas->findData("Antenna C"));width->setValue(5000);
                recommended->click();
                require(apply->isEnabled() && rates->currentData().toUInt()==10000000 && width->value()==10000 &&
                    gain->currentData()=="auto" && frequency->value()==470.5 && antennas->currentData().toString()!="Antenna C",
                    "Recommended configuration did not repair all linked conflicts without retuning");
                advanced->setChecked(false);require(apply->isEnabled() && rates->findData(10000000u)>=0,"Normal rates hide the receiver's full capability");
                QCoreApplication::processEvents();
                if(const auto screenshot=qEnvironmentVariable("AMC_RECEIVER_DIALOG_SCREENSHOT");!screenshot.isEmpty())
                {
                    QEventLoop settled;QTimer::singleShot(50,&settled,&QEventLoop::quit);settled.exec();
                    dialog.grab().save(screenshot);
                }
                require(panels->orientation()==Qt::Horizontal,"Wide settings did not use the screen width");
                dialog.resize(780,650);QCoreApplication::processEvents();
                require(panels->orientation()==Qt::Vertical,"Narrow settings did not adapt the panels");
                dialog.resize(1200,650);
                antennas->setCurrentIndex(antennas->findData("Antenna B"));
                width->setValue(3000);
                rates->setCurrentIndex(rates->findData(3000000u));gain->setCurrentIndex(gain->findData("320"));
                require(apply->isEnabled(),"Valid final configuration was rejected");
                apply->click();dialog.reject();valid=dialog.isVisible();
            } catch(const std::exception& error){failure=QString::fromUtf8(error.what());dialog.reject();}
        });
        QTimer::singleShot(12000,&dialog,&QDialog::reject);
        probe.start(20);
        const auto result=dialog.exec();
        if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());
        require(result==QDialog::Accepted && valid,"RSPdx-R2 guided settings did not apply successfully");
        const auto selection=dialog.selection();
        require(selection.settings.at("service_endpoint")==serviceEndpoint.toStdString() &&
                selection.applied.at("actual").at("settings").at("service_endpoint")==serviceEndpoint.toStdString(),
                "Connection context was lost during application/readback");
        require(selection.backend=="sdrplay" && selection.index==0 && selection.serial==expectedSerial &&
                selection.antenna=="Antenna B" && selection.sampleRate==3'000'000 &&
                selection.capabilities && selection.applied.at("actual").at("sample_rate_hz")==3'000'000 &&
                selection.applied.at("actual").at("filter_bandwidth_hz")==1'536'000 &&
                selection.gain=="32.0" && selection.applied.at("actual").at("gain_tenths_db")==320 &&
                selection.applied.value("activation_confirmed",false),
                "Applying RSPdx-R2 settings returned the wrong device configuration");
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
