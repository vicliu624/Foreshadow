#include "test_process.hpp"
#include "../src/frontend/desktop/plugin_text.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
using Json=nlohmann::json;
using amc_test::require;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(argc==5,"Expected CLI, independent receiver module, incompatible module and extra locale fixture");
        const auto cli=QFileInfo(app.arguments()[1]).absoluteFilePath();
        const auto original=QCryptographicHash::hash(amc_test::read(cli),QCryptographicHash::Sha256);
        QTemporaryDir temp;require(temp.isValid(),"Could not isolate receiver installation");
        const auto modules=temp.path()+"/receiver-plugins";require(QDir().mkpath(modules),"Could not create plugin directory");
        amc_test::ChildRunner runner(temp.path());runner.setEnvironment("SIGNALSCOPE_RECEIVER_PLUGIN_PATH",modules);
        require(Json::parse(runner.run(cli,{"receiver-plugins"})).empty(),"Empty plugin search path loaded a built-in receiver");
        runner.run(cli,{"--help"});
        const auto module=QFileInfo(app.arguments()[2]);const auto installed=modules+'/'+module.fileName();
        require(QFile::copy(module.absoluteFilePath(),installed),"Could not install independent receiver plugin");
        const QDir catalogs(module.dir().filePath("amc-receiver-test.i18n"));
        const auto installedCatalogs=modules+"/amc-receiver-test.i18n";require(QDir().mkpath(installedCatalogs),"Could not install plugin catalogs");
        for(const auto& file:catalogs.entryList({"*.json"},QDir::Files))
            require(QFile::copy(catalogs.filePath(file),installedCatalogs+'/'+file),"Could not copy plugin catalog");
        auto list=Json::parse(runner.run(cli,{"receiver-plugins"}));
        require(list.size()==1 && list[0]["id"]=="test.receiver" && list[0]["status"]=="loaded","Third plugin was not discovered");
        require(QDir().mkpath(temp.path()+"/Debug"),"Could not stage a multi-configuration executable");
        const auto developmentCli=temp.path()+"/Debug/"+QFileInfo(cli).fileName();
        require(QFile::copy(cli,developmentCli),"Could not copy the development CLI");
        amc_test::ChildRunner developmentRunner(temp.path());
        const auto developmentPlugins=Json::parse(developmentRunner.run(developmentCli,{"receiver-plugins"}));
        bool developmentLoaded=false;for(const auto& plugin:developmentPlugins)
            developmentLoaded=developmentLoaded || (plugin["id"]=="test.receiver" && plugin["status"]=="loaded");
        require(developmentLoaded,"Multi-configuration executable did not discover adjacent build plugins");
        const QStringList select{"--receiver","test.receiver"};
        auto args=QStringList{"devices"};args+=select;
        require(Json::parse(runner.run(cli,args))["devices"].size()==1,"New receiver did not enumerate");
        args.append({"--settings",R"({"independent_connection":"fixture://receiver"})"});
        require(Json::parse(runner.run(cli,args)).at("connection").at("independent_connection")=="fixture://receiver",
                "A new provider's pre-enumeration connection settings were lost");
        args={"probe"};args+=select;args.append({"--settings",R"({"independent_connection":"fixture://other"})"});
        require(Json::parse(runner.run(cli,args)).at("connection").at("independent_connection")=="fixture://other",
                "A new provider's pre-probe connection settings were lost");
        for(const auto* fault:{"request-version","request-fraction","request-missing-fields"}) {
            runner.setEnvironment("AMC_TEST_RECEIVER_FAULT",fault);
            const auto rejected=Json::parse(runner.run(cli,{"receiver-plugins"}));
            require(rejected.size()==1 && rejected[0]["status"]=="rejected" &&
                    rejected[0]["error"]=="Unsupported receiver request protocol","Unknown request semantics were accepted");
        }
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","legacy-request");
        args={"probe"};args+=select;
        require(Json::parse(runner.run(cli,args)).at("capabilities").at("backend")=="test.receiver","Legacy request protocol stopped working");
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","");
        args={"parameters"};args+=select;
        require(Json::parse(runner.run(cli,args)).size()>=5,"New receiver settings were not described");
        const auto pluginHash=QCryptographicHash::hash(amc_test::read(installed),QCryptographicHash::Sha256);
        require(QFile::copy(app.arguments()[4],installedCatalogs+"/fr.json"),"Could not add an independent translation resource");
        const auto extraLocale=Json::parse(amc_test::read(app.arguments()[4]));
        const auto described=Json::parse(runner.run(cli,args));bool translated=false;
        app.setProperty("signalscope-language","fr-CA");
        for(const auto& d:described)if(d.value("key",std::string())=="profile") {
            const auto key=d.at("label_key").get<std::string>();
            translated=d.at("label").at("fr")==extraLocale.at(key) &&
                signalscope::desktop::pluginText(d.at("label"))==QString::fromStdString(extraLocale.at(key).get<std::string>());
            app.setProperty("signalscope-language","untranslated-locale");
            require(signalscope::desktop::pluginText(d.at("label"))==QString::fromStdString(d.at("label").at("en").get<std::string>()),"Missing plugin locale did not fall back to English");
        }
        require(translated && QCryptographicHash::hash(amc_test::read(installed),QCryptographicHash::Sha256)==pluginHash,
            "Adding a locale required changing the plugin binary or locale fallback failed");
        args={"validate"};args+=select;args.append({"--rate","2000000","--search-width","2000000","--settings",R"({"profile":"eco"})"});
        const auto conflict=Json::parse(runner.run(cli,args,1));
        require(!conflict["plan"]["valid"].get<bool>() && conflict["plan"]["issues"][0]["code"]=="test_profile_rate","Plugin linkage rule was bypassed");
        args={"recommend"};args+=select;
        const auto proposals=Json::parse(runner.run(cli,args));
        require(proposals.size()==1 && proposals[0]["sample_rate_hz"]==2000000 && proposals[0]["settings"]["profile"]=="wide","Host replaced plugin recommendation with its own preset");
        args={"configure"};args+=select;args.append({"--rate","2000000","--frequency","5800000000","--settings",R"({"profile":"wide","preamp":true,"calibration_ppm":2.5})"});
        const auto applied=Json::parse(runner.run(cli,args));
        require(applied["actual"]["frequency_hz"]==5800000000ULL && applied["actual"]["settings"]["calibration_ppm"]==2.5,"Plugin-specific setting was lost before readback");
        args={"capture"};args+=select;args.append({"--seconds","0.05","--output",temp.path()+"/capture"});
        const auto capture=Json::parse(runner.run(cli,args));
        require(capture["accepted_pairs"]==100000,"Binary sample streaming did not reach capture");
        require(capture.at("startup_readback").at("acquisition_conditions").at("independent_future_condition").at("value")==42,"Finite capture lost provider startup evidence");
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","gap");
        args={"capture"};args+=select;args.append({"--seconds","0.05","--output",temp.path()+"/gap"});runner.run(cli,args,1);
        require(QFileInfo(temp.path()+"/gap/capture.sigmf-data").size()==65536,"Discontinuous plugin data was written or cancellation did not preserve the valid prefix");
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","");
        args={"monitor"};args+=select;args.append({"--seconds","0.1","--output",temp.path()+"/monitor","--lora","0","--fsk","1","--search-width","2000000"});
        const auto monitored=Json::parse(runner.run(cli,args));
        require(monitored.at("source_pairs")==200000 && monitored.at("processed_pairs")==200000,"New receiver did not reach analysis pipeline");
        const auto startup=monitored.at("receiver_configuration").at("applied").at("acquisition_conditions");
        require(startup.at("independent_future_condition").at("value")==42 && startup.at("scope")=="before_first_samples","Independent startup conditions did not reach monitor diagnostics");
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","legacy-readback");
        args={"monitor"};args+=select;args.append({"--seconds","0.05","--output",temp.path()+"/legacy-monitor","--lora","0","--fsk","0","--search-width","2000000"});
        const auto legacy=Json::parse(runner.run(cli,args)).at("receiver_configuration").at("applied").at("acquisition_conditions");
        require(legacy.at("origin")=="not_reported" && legacy.at("adc_overload").at("state")=="unknown","Legacy plugin was rejected or acquired invented quality facts");
        for(const auto* fault:{"readback-version","readback-fraction","readback-missing-conditions","bad-conditions","duplicate-ready"}) {
            runner.setEnvironment("AMC_TEST_RECEIVER_FAULT",fault);args={"capture"};args+=select;args.append({"--seconds","0.05","--output",temp.path()+'/'+fault});runner.run(cli,args,1);
            require(QFileInfo(temp.path()+'/'+fault+"/capture.sigmf-data").size()==0,"Samples escaped an unsupported or contradictory startup report");
        }
        runner.setEnvironment("AMC_TEST_RECEIVER_FAULT","");
        require(QCryptographicHash::hash(amc_test::read(cli),QCryptographicHash::Sha256)==original,"Host binary changed during plugin installation");
        const auto incompatible=QFileInfo(app.arguments()[3]);
        require(QFile::copy(incompatible.absoluteFilePath(),modules+'/'+incompatible.fileName()),"Could not stage incompatible plugin");
        require(QFile::copy(installed,modules+"/amc-receiver-duplicate."+module.suffix()),"Could not stage duplicate plugin");
        amc_test::write(modules+"/amc-receiver-broken."+module.suffix(),"not a library");
        list=Json::parse(runner.run(cli,{"receiver-plugins"}));unsigned loaded=0,rejected=0;
        for(const auto& item:list)if(item["status"]=="loaded")++loaded;else if(item["status"]=="rejected")++rejected;
        require(loaded==1 && rejected==3,"Invalid ABI, duplicate ID or unloadable module was not rejected independently");
        QFile::remove(installed);QFile::remove(modules+"/amc-receiver-duplicate."+module.suffix());
        args={"devices"};args+=select;runner.run(cli,args,1);
        std::cout<<"Independent plugin discovery, policy, control, binary streaming, analysis and compatibility passed without changing the host.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
