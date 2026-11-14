#include "test_process.hpp"
#include "signalscope/adapters/modulation/snapshot_extension.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "plugins/desktop/research/research_provider.hpp"
#include "analysis_panel.hpp"
#include "desktop_menus.hpp"
#include "desktop_language.hpp"
#include "language_preference.hpp"
#include "modulation_assessment.hpp"
#include "event_presentation.hpp"
#include "plugin_text.hpp"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>
using namespace signalscope;
using namespace signalscope::desktop;
using amc_test::require;
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    if(app.arguments().value(1)=="--resource-layout") {
        try {
            TextCatalog catalog=TextCatalog::resource("modulation");Json text={{"label_key","test.detector.label"}};catalog.resolve(text);
            require(text.at("label").contains("en") && !languagePacks().empty(),"Multi-configuration resources were not loaded");return 0;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(app.arguments().value(1)=="--input") {
        const auto arguments=app.arguments();const auto at=arguments.indexOf("--settings-file");
        if(at<0 || at+1>=arguments.size())return 2;
        const auto settings=amc_test::read(arguments[at+1]);const auto output=arguments.indexOf("--output");
        if(output<0 || output+1>=arguments.size())return 2;amc_test::write(arguments[output+1],settings);return 0;
    }
    try {
        require(argc==3,"Expected extension fixtures and lrelease");const QDir fixtures(app.arguments()[1]);
        QTemporaryDir temp;require(temp.isValid(),"Could not isolate extension tests");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        app.setProperty("signalscope-language","en");
        TextCatalog catalog;catalog.load((fixtures.absolutePath()+"/detector-i18n/..").toStdString(),{"detector-i18n"});
        const auto package=temp.path()+"/Resource Package.app";
        require(QDir().mkpath(package+"/Contents/MacOS/receiver-plugins") && QDir().mkpath(package+"/Contents/Resources/catalog"),
                "Could not create a split code/resource package");
        amc_test::write(package+"/Contents/Resources/catalog/en.json",amc_test::read(fixtures.filePath("detector-i18n/en.json")));
        const auto code=path_from_utf8((package+"/Contents/MacOS/receiver-plugins").toStdString());
        TextCatalog bundled;
        bundled.load(code,{"../../Resources/catalog"},path_from_utf8(package.toStdString()));
        Json resourceText={{"label_key","test.detector.label"}};bundled.resolve(resourceText);
        require(resourceText.at("label").contains("en"),"Package resource boundary did not support separate code and data");
        require(QDir().mkpath(temp.path()+"/outside-catalog"),"Could not create outside-package resource fixture");
        amc_test::write(temp.path()+"/outside-catalog/en.json",amc_test::read(fixtures.filePath("detector-i18n/en.json")));
        bool rejected=false;
        try{TextCatalog outside;outside.load(code,{"../../../../outside-catalog"},path_from_utf8(package.toStdString()));}
        catch(const std::exception&){rejected=true;}
        require(rejected,"Catalog escaped the declared package boundary");
        Json presentation={{"label_key","test.detector.label"},{"covered",true},
            {"parsing",{{"message_key","test.detector.parsing"}}},{"effect",{{"message_key","test.detector.effect"}}},
            {"parsing_summary",{{"message_key","test.detector.summary"}}},{"impact_summary",{{"message_key","test.detector.impact"}}}};
        catalog.resolve(presentation);
        registerSnapshotExtension({{"independent.carrier","independent.carrier",DetectorStage::snapshot,{},[presentation](const DetectorContext&){return presentation;}},
            [catalog,presentation](const SnapshotInput& input,Json& events) {
                Json event={{"modulation_id","independent.carrier"},{"modulation","IndependentCarrier"},{"display_label",presentation.at("label")},
                    {"evidence_level","independent_feature"},{"first_observed_sample",input.first},{"center_frequency_hz",input.center},
                    {"features",Json::array({{{"label_key","test.feature.label"},{"value",7},{"unit","Hz"}}})}};
                catalog.resolve(event);events.push_back(std::move(event));
            }});
        QMainWindow window;QTabWidget tabs;DesktopMenus menus;
        menus.mount(window,tabs,{}, {},{});
        auto* action=window.findChild<QAction*>("detection/independent.carrier");
        require(action && action->isChecked(),"New detector did not enter the generic menu");
        auto selected=menus.selection();require(selected.snapshot.extensions.at("independent.carrier"),"New detector selection was lost");
        SnapshotSelection only{{false,false},false,false,false};only.extensions["independent.carrier"]=true;
        const auto events=detectSnapshot(Bytes(4096,128),SampleFormat::cu8,2000000,100000000,1000000,42,only);
        require(events.size()==1 && events[0].at("first_observed_sample")==42,"Snapshot scheduler did not execute a newly registered detector");
        const auto cells=eventCells(events[0]);require(cells[0]==pluginText(presentation.at("label")) && cells[2].contains("7"),"New event label/features were hidden by the frontend");
        ReceiverCapabilities caps;caps.backend="recorded";Config cfg;cfg.auto_gain=true;LoRaConfig lc;
        const auto rows=assessReceiverModulations(caps,cfg,lc,false,{},only);bool assessed=false;
        for(const auto& row:rows)if(row.id=="independent.carrier")assessed=row.enabled && row.compatible && row.parsing==pluginText(presentation.at("parsing").at("message"));
        require(assessed,"New detector did not enter the generic configuration assessment");
        std::atomic<bool> parametersDelivered=false;
        registerResearchProvider({{"independent.research",pluginText(presentation.at("label"))},
            [&parametersDelivered](const QString&,double,std::stop_token,const ResearchOptions& options)->ResearchResult {
                parametersDelivered=options.parameters.at("knob")=="23";return QString::number(options.parameters.at("knob").get<std::string>().size());
            },true,[](QWidget* parent){auto* edit=new QLineEdit(parent);edit->setObjectName("independent-research-knob");edit->setText("23");
                return ResearchControls{edit,[edit]{return Json{{"knob",edit->text().toStdString()}};},{}};}, {}});
        Config recorded;recorded.rate=250000;Recorder recording(temp.path().toStdString()+"/recording",recorded,"extension-test");
        recording.append(Bytes(5000,127));recording.finish("complete",Json::object());
        AnalysisPanel panel({},"en");
        require(panel.analyzeRange(temp.path()+"/recording/capture.sigmf-meta","independent.research",{0,100}),"New research provider was not selectable");
        QEventLoop loop;QTimer poll;QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(!panel.isAnalyzing())loop.quit();});poll.start(10);QTimer::singleShot(5000,&loop,&QEventLoop::quit);loop.exec();
        require(parametersDelivered && panel.hasResult(),"Provider-owned parameters did not reach its renderer");
        const auto externalSource=temp.path()+"/external-source";require(QDir().mkpath(externalSource+"/i18n"),"Could not prepare external parameter package");
        auto manifest=Json::parse(amc_test::read(fixtures.filePath("research-parameters.json")));
        const auto executable=QFileInfo(app.applicationFilePath()).suffix().isEmpty()?QStringLiteral("parameter-tool"):QStringLiteral("parameter-tool.exe");
        manifest["executable"]=executable.toStdString();amc_test::write(externalSource+"/plugin.json",QByteArray::fromStdString(manifest.dump()));
        require(QFile::copy(app.applicationFilePath(),externalSource+'/'+executable),"Could not copy standalone research fixture");
        for(const auto& file:QDir(fixtures.filePath("research-i18n")).entryList({"*.json"},QDir::Files))
            require(QFile::copy(fixtures.filePath("research-i18n/"+file),externalSource+"/i18n/"+file),"Could not prepare external translation resource");
        const auto installed=temp.path()+"/external-installed";
        require(installResearchPlugin(externalSource+"/plugin.json",installed)==ResearchInstallResult::installed,"External parameter/resources package could not install");
        const auto discovered=discoverResearchPlugins(installed,"en");require(discovered.size()==1 && discovered.front().parameterDescriptions.size()==2,"Manifest parameters were lost during discovery");
        const auto providers=researchProviders(discovered);const auto provider=std::find_if(providers.begin(),providers.end(),[](const auto& p){return p.plugin.id=="external.parameter-fixture";});
        require(provider!=providers.end() && provider->controls,"External tool parameters did not create generic controls");
        const auto controls=provider->controls(&window);auto* profile=controls.widget->findChild<QComboBox*>("research-plugin-profile");auto* boost=controls.widget->findChild<QCheckBox*>("research-plugin-boost");
        require(profile && boost,"External manifest controls are missing");boost->setChecked(true);profile->setCurrentIndex(profile->findData(QStringLiteral("\"eco\"")));
        bool conflict=false;try{controls.read();}catch(const std::invalid_argument&){conflict=true;}
        require(!boost->isEnabled() && conflict,"External parameter dependency did not block a conflicting request");
        profile->setCurrentIndex(profile->findData(QStringLiteral("\"wide\"")));ResearchOptions externalOptions;externalOptions.parameters=controls.read();
        const auto report=provider->render(temp.path()+"/recording/capture.sigmf-meta",0,{},externalOptions);
        const auto transported=Json::parse(std::get<QString>(report).toStdString());
        require(transported.at("boost")==true && transported.at("profile")=="wide","Manifest parameter values did not reach the external executable");
        const auto binaryHash=QCryptographicHash::hash(amc_test::read(app.applicationFilePath()),QCryptographicHash::Sha256);
        const auto originalDirectory=resource_directory("desktop")/"languages";
        const auto languageDirectory=temp.path()+"/resources/desktop/languages";require(QDir().mkpath(languageDirectory),"Could not isolate language packs");
        for(const auto& file:QDir(QString::fromStdString(path_to_utf8(originalDirectory))).entryList({"*.json"},QDir::Files))
            require(QFile::copy(QString::fromStdString(path_to_utf8(originalDirectory/file.toStdString())),languageDirectory+'/'+file),"Could not copy base language packs");
        const auto french=Json::parse(amc_test::read(fixtures.filePath("fr.json")));
        require(QFile::copy(fixtures.filePath("fr.json"),languageDirectory+"/fr.json"),"Could not add a language pack");
        amc_test::ChildRunner runner(temp.path());runner.run(app.arguments()[2],{fixtures.filePath("fr.ts"),"-qm",languageDirectory+"/ams_fr.qm"});
        require(QDir().mkpath(temp.path()+"/Debug") && QDir().mkpath(temp.path()+"/resources/modulation/i18n"),"Could not stage development resources");
        const auto developmentExecutable=temp.path()+"/Debug/"+QFileInfo(app.applicationFilePath()).fileName();
        require(QFile::copy(app.applicationFilePath(),developmentExecutable) &&
            QFile::copy(fixtures.filePath("detector-i18n/en.json"),temp.path()+"/resources/modulation/i18n/en.json"),"Could not copy development resources");
        runner.run(developmentExecutable,{"--resource-layout"});
        const auto unicodeResources=temp.path()+QStringLiteral("/\u8d44\u6e90 \U0001f4e1");
        require(QDir().mkpath(unicodeResources+"/modulation/i18n") && QDir().mkpath(unicodeResources+"/desktop/languages"),
                "Could not prepare Unicode environment resources");
        require(QFile::copy(fixtures.filePath("detector-i18n/en.json"),unicodeResources+"/modulation/i18n/en.json") &&
                QFile::copy(QString::fromStdString(path_to_utf8(originalDirectory/"en.json")),unicodeResources+"/desktop/languages/en.json"),
                "Could not copy Unicode environment resources");
        amc_test::ChildRunner unicodeRunner(temp.path());
        unicodeRunner.setEnvironment("SIGNALSCOPE_RESOURCE_PATH",unicodeResources);
        unicodeRunner.run(app.applicationFilePath(),{"--resource-layout"});
        const auto previous=qgetenv("SIGNALSCOPE_RESOURCE_PATH");qputenv("SIGNALSCOPE_RESOURCE_PATH",(temp.path()+"/resources").toUtf8());
        const auto selection=resolveLanguageSelection({},"system",{QString::fromStdString(french.at("system_request"))});
        require(selection && selection->tag==QString::fromStdString(french.at("tag")),"New desktop language was not discovered from resources");
        Language language;require(language.install(QString::fromStdString(french.at("tag"))),"New language catalog could not load");language.add_menu(window);
        require(QCoreApplication::translate("ReceiverDialog","Receiver settings")!=QStringLiteral("Receiver settings"),"New language metadata did not activate its translator");
        bool offered=false;for(auto* menu:window.menuBar()->actions())if(menu->menu())for(auto* item:menu->menu()->actions())
            offered=offered || item->text()==QString::fromStdString(french.at("native_name"));
        require(offered && QCryptographicHash::hash(amc_test::read(app.applicationFilePath()),QCryptographicHash::Sha256)==binaryHash,"Adding a desktop language required changing the host");
        if(previous.isNull())qunsetenv("SIGNALSCOPE_RESOURCE_PATH");else qputenv("SIGNALSCOPE_RESOURCE_PATH",previous);
        std::cout<<"Independent detector, event presentation, research parameters and desktop language extensions passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
