#include "test_process.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        amc_test::require(argc==2,"Pass the actual desktop executable");
        QTemporaryDir directory;amc_test::require(directory.isValid(),"Could not create isolated desktop settings");
        amc_test::ChildRunner runner(directory.path());const auto executable=app.arguments()[1];
        for(const auto& scenario:{QStringLiteral("wide"),QStringLiteral("compact"),QStringLiteral("scaled")}) {
            const auto settings=QDir(directory.path()).filePath(scenario);const QStringList common{"--receiver","rtlsdr","--language","en","--settings-dir",settings,"--output",settings+"/events","--smoke-seconds","1"};
            runner.setEnvironment("QT_SCALE_FACTOR",scenario=="scaled"?"1.25":"1");
            auto restart=common;restart<<"--smoke-restored-research-layout"<<"live-research/waveform-laboratory";
            if(scenario=="wide")runner.run(executable,restart,4); // No saved layout is not a successful restore.
            auto initial=common;initial<<"--smoke-research-layout"<<"live-research/waveform-laboratory";if(scenario=="compact")initial<<"--smoke-compact";runner.run(executable,initial);
            const auto config=QDir(settings).filePath("SignalScope/SignalScope.ini");QSettings state(config,QSettings::IniFormat);
            amc_test::require(!state.value("workspace/v1/state").toByteArray().isEmpty(),"Desktop did not persist its workspace before exit");
            runner.run(executable,restart);
            QSettings after(config,QSettings::IniFormat);amc_test::require(!after.value("workspace/v1/state").toByteArray().isEmpty(),"Restart lost workspace state");
            if(scenario=="wide"){auto invalid=common;invalid<<"--smoke-restored-research-layout"<<"independent.missing-layout";runner.run(executable,invalid,4);}
        }
        std::cout<<"Actual desktop research layout survives process restart in wide, compact and scaled scenarios\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
