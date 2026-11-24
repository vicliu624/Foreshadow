#include "plugins/adsb/map_geography.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
using namespace signalscope;
using Json=nlohmann::json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    const auto resource=resource_directory("adsb");Json provenance;std::ifstream(resource/"map-source.json")>>provenance;
    QFile geography(QString::fromStdString(path_to_utf8(resource/"geography.json")));require(geography.open(QIODevice::ReadOnly),"Geography resource unreadable");
    require(QCryptographicHash::hash(geography.readAll(),QCryptographicHash::Sha256).toHex().toStdString()==provenance.at("geography").at("processed_sha256").get<std::string>(),"Delivered geography differs from pinned provenance");
    const auto bundled=adsb::MapGeography::bundled();
    require(bundled->countries().size()>200&&bundled->regions().size()>4000&&bundled->places().size()>7000,"Installed geographic layers missing");
    const auto kunming=bundled->locate({24.9874,102.8465});
    require(kunming.region&&kunming.country&&kunming.place,"Kunming geographic context absent");
    require(kunming.country->id=="CHN"&&kunming.region->names.at("en")=="Yunnan"&&kunming.place->names.at("en")=="Kunming"&&kunming.distance_km<30,"Kunming containment/proximity incorrect");
    require(bundled->describe({24.9874,102.8465},"en").contains("Yunnan")&&bundled->describe({24.9874,102.8465},"en").contains("Kunming"),"Location readout missing");
    require(bundled->describe({24.9874,102.8465},"zh-CN").contains(QString::fromUtf8("云南")),"Geography localization absent");
    const auto paris=bundled->locate({48.8566,2.3522});require(paris.country&&paris.country->id=="FRA"&&paris.region&&paris.place&&paris.place->names.at("en")=="Paris","European administrative context incorrect");
    const auto ocean=bundled->locate({0,-140});require(!ocean.country&&!ocean.region&&!ocean.place,"Open ocean acquired a false administrative area");
    require(bundled->describe({0,-140},"fr-CA").contains("unavailable"),"Missing UI language did not fall back");
    QTemporaryDir temporary;require(temporary.isValid(),"Temporary directory");const auto file=path_from_utf8(temporary.path().toStdString())/"independent-geography.json";
    const Json outer=Json::array({{10,10},{20,10},{20,20},{10,20},{10,10}}),hole=Json::array({{13,13},{15,13},{15,15},{13,15},{13,13}});
    const Json country={{"id","TST"},{"country","TST"},{"names",{{"en","Test country"},{"eo","Sendependa lando"}}},{"label",{12,12}},{"polygons",Json::array({Json::array({outer})})}};
    Json region=country;region["id"]="independent-region";region["names"]={{"en","Example province"}};region["polygons"]=Json::array({Json::array({outer,hole})});
    Json data={{"schema",1},{"countries",Json::array({country})},{"regions",Json::array({region})},{"places",Json::array({{{"names",{{"en","Example city"}}},{"country","TST"},{"position",{12,12}},{"rank",1}}})}};
    const auto write=[&]{std::ofstream output(file);output<<data.dump();};write();
    const auto added=adsb::MapGeography::load(file);
    const auto inside=added.locate({12.01,12});require(inside.country&&inside.region&&inside.place&&inside.distance_km<2,"Independent resource was not consumed");
    const auto excluded=added.locate({14,14},false);require(excluded.country&&!excluded.region,"Polygon hole became a false province");
    require(adsb::MapGeography::name(country.at("names"),"eo")=="Sendependa lando","Resource-only language extension failed");
    data["regions"][0]["polygons"][0][0][0]={999,12};write();bool rejected=false;try{(void)adsb::MapGeography::load(file);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid coordinate accepted");
    std::cout<<"Offline administrative containment, city proximity, holes, missing coverage and resource-only extension passed\n";
}
