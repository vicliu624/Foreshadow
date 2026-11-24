#include "map_geography.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <set>
namespace signalscope::adsb {
namespace {
constexpr double radians=std::numbers::pi/180;
bool valid(Position p){return std::isfinite(p.latitude)&&std::isfinite(p.longitude)&&std::abs(p.latitude)<=90&&std::abs(p.longitude)<=180;}
int cell(Position p){return std::clamp(int((p.latitude+90)/2),0,89)*180+std::clamp(int((p.longitude+180)/2),0,179);}
Position position(const nlohmann::json& j){if(!j.is_array()||j.size()!=2)throw std::invalid_argument("Map position");Position p{j[1].get<double>(),j[0].get<double>()};if(!valid(p))throw std::invalid_argument("Map coordinate range");return p;}
void names(const nlohmann::json& j){if(!j.is_object()||j.size()>64||!j.contains("en"))throw std::invalid_argument("Map names");for(const auto& value:j)if(!value.is_string()||value.get<std::string>().size()>512)throw std::invalid_argument("Map name budget");}
QString text(const std::string& key,const nlohmann::json& args,const QString& language){static const auto catalog=TextCatalog::resource("adsb");nlohmann::json j={{"label_key",key},{"arguments",args}};catalog.resolve(j);return desktop::localizedText(j.at("label"),language,QString::fromStdString(key));}
double distance(Position a,Position b){const double lat=(b.latitude-a.latitude)*radians,lon=(b.longitude-a.longitude)*radians;const double h=std::sin(lat/2)*std::sin(lat/2)+std::cos(a.latitude*radians)*std::cos(b.latitude*radians)*std::sin(lon/2)*std::sin(lon/2);return 12742*std::asin(std::sqrt(std::clamp(h,0.,1.)));}
}
QPointF mapProject(Position p){const auto lat=std::clamp(p.latitude,-85.,85.)*radians;return {(p.longitude+180)/360,(1-std::asinh(std::tan(lat))/std::numbers::pi)/2};}
Position mapUnproject(QPointF p){return {std::atan(std::sinh((1-2*p.y())*std::numbers::pi))/radians,p.x()*360-180};}
QString MapGeography::name(const nlohmann::json& value,const QString& language){return desktop::localizedText(value,language,QString::fromStdString(value.value("en",std::string())));}
MapGeography MapGeography::load(const std::filesystem::path& path){
    if(std::filesystem::file_size(path)>16*1024*1024)throw std::length_error("Geography resource size");
    std::ifstream file(path);nlohmann::json data;file>>data;
    if(data.at("schema")!=1)throw std::invalid_argument("Geography schema");
    MapGeography result;std::size_t points=0,references=0;
    const auto index=[&](Grid& grid,const MapRegion& region,std::size_t number){
        const auto& b=region.geographic_bounds;
        const int x0=std::clamp(int((b[0]+180)/2),0,179),x1=std::clamp(int((b[2]+180)/2),0,179);
        const int y0=std::clamp(int((b[1]+90)/2),0,89),y1=std::clamp(int((b[3]+90)/2),0,89);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){if(++references>2000000)throw std::length_error("Geography index budget");grid[y*180+x].push_back(number);}
    };
    const auto regions=[&](const char* key,std::vector<MapRegion>& output,Grid& grid){
        const auto& array=data.at(key);if(!array.is_array()||array.size()>10000)throw std::invalid_argument("Geography feature budget");
        std::set<std::string> ids;
        for(const auto& value:array){MapRegion region;region.id=value.at("id");region.country=value.at("country");region.names=value.at("names");names(region.names);
            if(region.id.empty()||region.id.size()>80||region.country.size()>80||!ids.insert(region.id).second)throw std::invalid_argument("Geography identity");
            region.label=position(value.at("label"));region.geographic_bounds={180,90,-180,-90};region.shape.setFillRule(Qt::OddEvenFill);
            const auto& polygons=value.at("polygons");if(!polygons.is_array()||polygons.empty())throw std::invalid_argument("Geography polygons");
            for(const auto& polygon:polygons){if(!polygon.is_array()||polygon.empty())throw std::invalid_argument("Geography polygon");for(const auto& ring:polygon){
                if(!ring.is_array()||ring.size()<4||(points+=ring.size())>650000)throw std::length_error("Geography point budget");
                QPolygonF projected;for(const auto& coordinate:ring){const auto p=position(coordinate);region.geographic_bounds[0]=std::min(region.geographic_bounds[0],p.longitude);region.geographic_bounds[1]=std::min(region.geographic_bounds[1],p.latitude);region.geographic_bounds[2]=std::max(region.geographic_bounds[2],p.longitude);region.geographic_bounds[3]=std::max(region.geographic_bounds[3],p.latitude);projected<<mapProject(p);}
                if(projected.first()!=projected.last())throw std::invalid_argument("Unclosed geography ring");region.shape.addPolygon(projected);
            }}region.bounds=region.shape.boundingRect();index(grid,region,output.size());output.push_back(std::move(region));
        }
    };
    regions("countries",result.countries_,result.country_grid_);regions("regions",result.regions_,result.region_grid_);
    for(std::size_t i=0;i<result.countries_.size();++i)result.country_ids_.emplace(result.countries_[i].id,i);
    const auto& places=data.at("places");if(!places.is_array()||places.size()>20000)throw std::invalid_argument("Geography place budget");
    for(const auto& value:places){MapPlace place;place.names=value.at("names");names(place.names);place.country=value.at("country");place.position=position(value.at("position"));place.rank=value.at("rank");if(place.rank<0||place.rank>20)throw std::invalid_argument("Geography place rank");result.place_grid_[cell(place.position)].push_back(result.places_.size());result.places_.push_back(std::move(place));}
    return result;
}
std::shared_ptr<const MapGeography> MapGeography::bundled(){static const auto value=[](){try{return std::make_shared<const MapGeography>(load(resource_directory("adsb")/"geography.json"));}catch(const std::exception&){return std::make_shared<const MapGeography>();}}();return value;}
GeographicLocation MapGeography::locate(Position p,bool nearby) const{
    GeographicLocation result;if(!valid(p))return result;const auto point=mapProject(p);
    const auto find=[&](const auto& items,const auto& indices)->const MapRegion*{const MapRegion* match=nullptr;for(auto i:indices){const auto& item=items[i];if(item.bounds.contains(point)&&item.shape.contains(point)&&(!match||item.bounds.width()*item.bounds.height()<match->bounds.width()*match->bounds.height()))match=&item;}return match;};
    result.region=find(regions_,region_grid_[cell(p)]);
    if(result.region){const auto country=country_ids_.find(result.region->country);if(country!=country_ids_.end())result.country=&countries_[country->second];}
    if(!result.country)result.country=find(countries_,country_grid_[cell(p)]);
    if(!nearby)return result;
    double best=200;const int center=cell(p),cy=center/180,cx=center%180;
    const int dx=std::min(90,int(std::ceil(3/std::max(.02,std::cos(p.latitude*radians))/2))+1);
    for(int y=std::max(0,cy-2);y<=std::min(89,cy+2);++y)for(int x=cx-dx;x<=cx+dx;++x)for(auto i:place_grid_[y*180+(x%180+180)%180]){
        const auto& place=places_[i];if(result.country&&place.country!=result.country->id)continue;
        const double km=distance(p,place.position);if(km<best){best=km;result.place=&place;result.distance_km=km;}
    }return result;
}
QString MapGeography::describe(Position p,const QString& language,bool nearby) const{
    const auto found=locate(p,nearby);nlohmann::json args={{"country",found.country?name(found.country->names,language).toStdString():std::string()},{"region",found.region?name(found.region->names,language).toStdString():std::string()}};
    std::string key=found.region&&found.country?"location.region":found.country?"location.country":"location.unavailable";
    if(found.place){args["city"]=name(found.place->names,language).toStdString();args["distance"]=std::llround(found.distance_km);key=found.region&&found.country?"location.region_near":found.country?"location.country_near":"location.near";}
    return text(key,args,language);
}
}
