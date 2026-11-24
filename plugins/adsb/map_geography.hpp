#pragma once
#include "core/tracker.hpp"
#include <QPainterPath>
#include <QString>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <QPolygonF>
#include <nlohmann/json.hpp>
namespace signalscope::adsb {
QPointF mapProject(Position);
Position mapUnproject(QPointF);
struct MapRegion {
    std::string id,country;
    nlohmann::json names;
    Position label;
    QPainterPath shape;
    QRectF bounds;
    std::array<double,4> geographic_bounds;
};
struct MapPlace {
    nlohmann::json names;
    std::string country;
    Position position;
    int rank=10;
};
struct GeographicLocation {
    const MapRegion* country=nullptr;
    const MapRegion* region=nullptr;
    const MapPlace* place=nullptr;
    double distance_km=0;
};
class MapGeography {
public:
    static MapGeography load(const std::filesystem::path&);
    static std::shared_ptr<const MapGeography> bundled();
    GeographicLocation locate(Position,bool nearby=true) const;
    QString describe(Position,const QString& language,bool nearby=true) const;
    const std::vector<MapRegion>& countries() const{return countries_;}
    const std::vector<MapRegion>& regions() const{return regions_;}
    const std::vector<MapPlace>& places() const{return places_;}
    static QString name(const nlohmann::json&,const QString& language);
private:
    using Grid=std::vector<std::vector<std::size_t>>;
    std::vector<MapRegion> countries_,regions_;
    std::vector<MapPlace> places_;
    std::map<std::string,std::size_t> country_ids_;
    Grid country_grid_=Grid(180*90),region_grid_=Grid(180*90),place_grid_=Grid(180*90);
};
}
