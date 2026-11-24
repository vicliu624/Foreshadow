#include "map_view.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <fstream>
#include <cmath>
#include <numbers>
namespace signalscope::adsb {
MapView::MapView(QWidget* parent):QWidget(parent) {
    setObjectName("adsb-map");setMinimumSize(220,160);
    try {
        const auto path=resource_directory("adsb")/"map.json";
        if(std::filesystem::file_size(path)>16*1024*1024)throw std::length_error("Map resource size exceeded");
        std::ifstream file(path);nlohmann::json data;file>>data;
        if(!data.is_array() || data.size()>20000)throw std::invalid_argument("Invalid map rings");
        std::size_t points=0;
        for(const auto& ring:data){if(!ring.is_array() || ring.size()<3 || (points+=ring.size())>500000)throw std::invalid_argument("Invalid map point budget");
            QPolygonF polygon;for(const auto& point:ring){if(!point.is_array() || point.size()!=2)throw std::invalid_argument("Invalid map coordinate");
                const double longitude=point[0].get<double>(),latitude=point[1].get<double>();
                if(!std::isfinite(longitude) || !std::isfinite(latitude) || std::abs(longitude)>180 || std::abs(latitude)>90)throw std::invalid_argument("Invalid map coordinate range");
                polygon<<project({latitude,longitude});}land_.push_back(std::move(polygon));}
    }catch(const std::exception&){land_.clear();}
}
QPointF MapView::project(Position p) const {
    const auto lat=std::clamp(p.latitude,-85.,85.)*std::numbers::pi/180;
    return {(p.longitude+180)/360,(1-std::asinh(std::tan(lat))/std::numbers::pi)/2};
}
QPointF MapView::screen(Position p) const {return (project(p)-center_)*(256*std::exp2(zoom_))+QPointF(width()/2.,height()/2.);}
void MapView::setAircraft(std::vector<Aircraft> value){aircraft_=std::move(value);update();}
void MapView::selectAircraft(std::uint64_t key){selected_=key;for(const auto& aircraft:aircraft_)if(aircraft_key(aircraft.latest)==key && aircraft.position){center_=project(*aircraft.position);break;}update();}
void MapView::fitAircraft(){for(const auto& aircraft:aircraft_)if(aircraft.position){center_=project(*aircraft.position);zoom_=7;update();return;}}
void MapView::paintEvent(QPaintEvent*) {
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(rect(),QColor("#101e2b"));
    const double scale=256*std::exp2(zoom_);const QPointF origin(width()/2.,height()/2.);
    painter.setPen(QPen(QColor("#426076"),1));painter.setBrush(QColor("#263d48"));
    for(const auto& ring:land_){QPolygonF polygon;for(const auto& point:ring)polygon<<(point-center_)*scale+origin;painter.drawPolygon(polygon);}
    painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#294052"),1));
    const double grid=zoom_>=9?.1:zoom_>=7?.5:zoom_>=5?2:zoom_>=3?5:15;
    double lastLongitudeLabel=-1000;
    for(double lon=-180;lon<=180;lon+=grid){const auto a=screen({-85.,lon}),b=screen({85.,lon});if(a.x()<0 || a.x()>width())continue;painter.drawLine(a,b);
        if(a.x()-lastLongitudeLabel>=60 && a.x()<width()-45){painter.setPen(QColor("#8aa7b8"));painter.drawText(QPointF(a.x()+4,16),QString::number(lon,'f',grid<1?1:0)+QChar(0x00b0));lastLongitudeLabel=a.x();painter.setPen(QColor("#294052"));}}
    for(double lat=-85;lat<=85;lat+=grid){const auto a=screen({lat,-180}),b=screen({lat,180});if(a.y()<20 || a.y()>height()-30)continue;painter.drawLine(a,b);
        painter.setPen(QColor("#8aa7b8"));painter.drawText(QPointF(5,a.y()-4),QString::number(lat,'f',grid<1?1:0)+QChar(0x00b0));painter.setPen(QColor("#294052"));}
    for(const auto& aircraft:aircraft_)if(aircraft.position){
        painter.setPen(QPen(QColor("#488e9b"),1.5));QPainterPath trail;bool started=false;QPointF previous;
        for(const auto& position:aircraft.trail){const auto point=screen(position);if(!started || std::abs(point.x()-previous.x())>scale/2)trail.moveTo(point);else trail.lineTo(point);previous=point;started=true;}painter.drawPath(trail);
        const auto point=screen(*aircraft.position);if(!rect().adjusted(-40,-40,40,40).contains(point.toPoint()))continue;
        if(selected_==aircraft_key(aircraft.latest)){painter.setPen(QPen(QColor("#fff0a0"),2));painter.setBrush(Qt::NoBrush);painter.drawEllipse(point,14,14);}
        painter.save();painter.translate(point);painter.rotate(aircraft.track_deg.value_or(0));painter.setPen(QPen(QColor("#bcfff0"),1));painter.setBrush(QColor("#56dfb5"));
        if(aircraft.track_deg)painter.drawPolygon(QPolygonF{QPointF(0,-9),QPointF(6,7),QPointF(0,4),QPointF(-6,7)});
        else painter.drawEllipse(QPointF(0,0),4,4);
        painter.restore();
        painter.setPen(QColor("#e2f0f4"));const auto identity=aircraft.callsign.empty()?QString::number(aircraft.latest.address,16).toUpper():QString::fromStdString(aircraft.callsign);
        painter.drawText(point+QPointF(11,-7),identity);
    }
    painter.fillRect(QRect(0,height()-28,width(),28),QColor(12,23,33,225));painter.setPen(QColor("#afc7d4"));painter.drawText(QRect(10,height()-28,width()-20,28),Qt::AlignVCenter,caption_);
}
void MapView::wheelEvent(QWheelEvent* event){const auto before=(event->position()-QPointF(width()/2.,height()/2.))/(256*std::exp2(zoom_))+center_;zoom_=std::clamp(zoom_+event->angleDelta().y()/480.,0.,12.);center_=before-(event->position()-QPointF(width()/2.,height()/2.))/(256*std::exp2(zoom_));update();event->accept();}
void MapView::mousePressEvent(QMouseEvent* event){drag_=event->position();if(event->button()!=Qt::LeftButton)return;
    for(const auto& aircraft:aircraft_)if(aircraft.position && QLineF(screen(*aircraft.position),drag_).length()<16){selected_=aircraft_key(aircraft.latest);if(selected)selected(*selected_);update();break;}}
void MapView::mouseMoveEvent(QMouseEvent* event){if(event->buttons()&Qt::LeftButton){center_-=(event->position()-drag_)/(256*std::exp2(zoom_));drag_=event->position();center_.setY(std::clamp(center_.y(),0.,1.));update();}}
}
