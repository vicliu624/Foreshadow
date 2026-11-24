#include "map_view.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <fstream>
#include <cmath>
#include <numbers>
namespace signalscope::adsb {
MapView::MapView(QWidget* parent,QString language):QWidget(parent),language_(std::move(language)),geography_(MapGeography::bundled()) {
    setObjectName("adsb-map");setMinimumSize(220,160);
    setAccessibleName(text("map.title"));
    zoom_in_=new QToolButton(this);zoom_out_=new QToolButton(this);
    zoom_in_->setText(text("map.zoom_in.symbol"));zoom_out_->setText(text("map.zoom_out.symbol"));
    zoom_in_->setAccessibleName(text("map.zoom_in"));zoom_out_->setAccessibleName(text("map.zoom_out"));
    zoom_in_->setToolTip(text("map.zoom_in"));zoom_out_->setToolTip(text("map.zoom_out"));
    connect(zoom_in_,&QToolButton::clicked,this,[this]{zoomBy(.75);});connect(zoom_out_,&QToolButton::clicked,this,[this]{zoomBy(-.75);});
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
    return mapProject(p);
}
QPointF MapView::screen(Position p) const {return (project(p)-center_)*(256*std::exp2(zoom_))+QPointF(width()/2.,height()/2.);}
QString MapView::text(const std::string& key,const nlohmann::json& arguments) const {static const auto catalog=TextCatalog::resource("adsb");nlohmann::json value={{"label_key",key},{"arguments",arguments}};catalog.resolve(value);return desktop::localizedText(value.at("label"),language_,QString::fromStdString(key));}
void MapView::updateContext(){selectedIdentity_.clear();selectedContext_.clear();for(const auto& a:aircraft_)if(selected_==aircraft_key(a.latest)&&a.position){selectedIdentity_=a.callsign.empty()?QString::number(a.latest.address,16).toUpper():QString::fromStdString(a.callsign);selectedContext_=geographicContext(*a.position);break;}setAccessibleDescription(selectedContext_);setToolTip(selectedContext_);}
void MapView::setAircraft(std::vector<Aircraft> value){aircraft_=std::move(value);updateContext();update();}
void MapView::selectAircraft(std::uint64_t key){selected_=key;for(const auto& aircraft:aircraft_)if(aircraft_key(aircraft.latest)==key && aircraft.position){center_=project(*aircraft.position);if(zoom_<5)zoom_=7;break;}updateContext();update();}
void MapView::fitAircraft(){for(const auto& a:aircraft_)if(a.position&&(!selected_||selected_==aircraft_key(a.latest))){center_=project(*a.position);zoom_=7;selected_=aircraft_key(a.latest);updateContext();update();return;}for(const auto& a:aircraft_)if(a.position){selected_.reset();fitAircraft();return;}}
void MapView::zoomBy(double amount){zoom_=std::clamp(zoom_+amount,0.,12.);update();}
void MapView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);zoom_in_->setGeometry(width()-38,10,28,28);zoom_out_->setGeometry(width()-38,40,28,28);}
void MapView::paintEvent(QPaintEvent*) {
    QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(rect(),QColor("#101e2b"));
    const double scale=256*std::exp2(zoom_);const QPointF origin(width()/2.,height()/2.);
    painter.setPen(QPen(QColor("#426076"),1));painter.setBrush(QColor("#263d48"));
    for(const auto& ring:land_){QPolygonF polygon;for(const auto& point:ring)polygon<<(point-center_)*scale+origin;painter.drawPolygon(polygon);}
    const QRectF viewport(center_-origin/scale,QSizeF(width()/scale,height()/scale));
    QTransform transform;transform.translate(origin.x(),origin.y());transform.scale(scale,scale);transform.translate(-center_.x(),-center_.y());
    painter.save();painter.setWorldTransform(transform);
    QPen countryPen(QColor("#7192a4"),1);countryPen.setCosmetic(true);painter.setPen(countryPen);painter.setBrush(QColor("#213946"));
    for(const auto& region:geography_->countries())if(region.bounds.intersects(viewport))painter.drawPath(region.shape);
    if(zoom_>=4){QPen boundary(QColor("#54768d"),1);boundary.setCosmetic(true);boundary.setStyle(Qt::DashLine);painter.setPen(boundary);painter.setBrush(Qt::NoBrush);for(const auto& region:geography_->regions())if(region.bounds.intersects(viewport))painter.drawPath(region.shape);}
    for(const auto& a:aircraft_)if(selected_==aircraft_key(a.latest)&&a.position){const auto found=geography_->locate(*a.position,false);if(found.region){QPen highlight(QColor("#7baeb5"),1.3);highlight.setCosmetic(true);painter.setPen(highlight);painter.setBrush(QColor(80,180,170,24));painter.drawPath(found.region->shape);}break;}
    painter.restore();
    painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor("#294052"),1));
    const double grid=zoom_>=9?.1:zoom_>=7?.5:zoom_>=5?2:zoom_>=3?5:15;
    double lastLongitudeLabel=-1000;
    for(double lon=-180;lon<=180;lon+=grid){const auto a=screen({-85.,lon}),b=screen({85.,lon});if(a.x()<0 || a.x()>width())continue;painter.drawLine(a,b);
        if(a.x()-lastLongitudeLabel>=60 && a.x()<width()-45){painter.setPen(QColor("#8aa7b8"));painter.drawText(QPointF(a.x()+4,16),QString::number(lon,'f',grid<1?1:0)+QChar(0x00b0));lastLongitudeLabel=a.x();painter.setPen(QColor("#294052"));}}
    for(double lat=-85;lat<=85;lat+=grid){const auto a=screen({lat,-180}),b=screen({lat,180});if(a.y()<20 || a.y()>height()-30)continue;painter.drawLine(a,b);
        painter.setPen(QColor("#8aa7b8"));painter.drawText(QPointF(5,a.y()-4),QString::number(lat,'f',grid<1?1:0)+QChar(0x00b0));painter.setPen(QColor("#294052"));}
    std::vector<QRectF> occupied;
    const QRectF hud(8,8,std::min(360,width()-56),56);
    if(!selectedIdentity_.isEmpty())occupied.push_back(hud);
    occupied.emplace_back(width()-45,0,45,75);occupied.emplace_back(width()-150,height()-62,150,34);
    for(const auto& a:aircraft_)if(a.position){const auto point=screen(*a.position);occupied.emplace_back(point+QPointF(-12,-20),QSizeF(110,36));}
    const auto label=[&](Position anchor,const QString& name,QColor color,bool centered,bool strong){
        if(name.isEmpty()||occupied.size()>200)return;
        const auto point=screen(anchor);if(!rect().contains(point.toPoint()))return;
        QFont font=painter.font();font.setPixelSize(strong?13:11);font.setWeight(strong?QFont::DemiBold:QFont::Normal);painter.setFont(font);
        const auto size=QFontMetricsF(font).size(Qt::TextSingleLine,name);const double x=centered?-size.width()/2:7;
        const std::array<QPointF,4> offsets={QPointF(x,-7),QPointF(x,18),QPointF(-size.width()-7,-7),QPointF(-size.width()-7,18)};
        for(const auto& offset:offsets){const QRectF box(point+offset+QPointF(-2,-size.height()),size+QSizeF(4,4));if(!QRectF(rect().adjusted(3,22,-3,-32)).contains(box))continue;bool collision=false;for(const auto& used:occupied)if(used.intersects(box)){collision=true;break;}if(collision)continue;
            QPainterPath glyph;glyph.addText(point+offset,font,name);painter.setBrush(color);painter.setPen(QPen(QColor("#0b1925"),3));painter.drawPath(glyph);painter.setPen(Qt::NoPen);painter.drawPath(glyph);occupied.push_back(box);return;}
    };
    if(zoom_<6)for(const auto& region:geography_->countries())if(region.bounds.intersects(viewport)&&region.bounds.width()*scale>65)label(region.label,MapGeography::name(region.names,language_),QColor("#b1c3cf"),true,true);
    if(zoom_>=4.5)for(const auto& region:geography_->regions())if(region.bounds.intersects(viewport)&&region.bounds.width()*scale>75)label(region.label,MapGeography::name(region.names,language_),QColor("#9eb9cc"),true,true);
    for(const auto& place:geography_->places())if(zoom_>=2.5+place.rank*.45){const auto point=screen(place.position);if(!rect().adjusted(4,24,-4,-30).contains(point.toPoint()))continue;painter.setPen(Qt::NoPen);painter.setBrush(QColor("#c3d6df"));painter.drawEllipse(point,2.2,2.2);label(place.position,MapGeography::name(place.names,language_),QColor("#d8e7ee"),false,place.rank<=2);}
    for(const auto& aircraft:aircraft_)if(aircraft.position){
        painter.setPen(QPen(QColor("#488e9b"),1.5));QPainterPath trail;bool started=false;QPointF previous;
        for(const auto& position:aircraft.trail){const auto point=screen(position);if(!started || std::abs(point.x()-previous.x())>scale/2)trail.moveTo(point);else trail.lineTo(point);previous=point;started=true;}painter.drawPath(trail);
        const auto point=screen(*aircraft.position);if(!rect().adjusted(-40,-40,40,40).contains(point.toPoint()))continue;
        if(selected_==aircraft_key(aircraft.latest)){painter.setPen(QPen(QColor("#fff0a0"),2));painter.setBrush(Qt::NoBrush);painter.drawEllipse(point,14,14);}
        painter.save();painter.translate(point);painter.rotate(aircraft.track_deg.value_or(0));painter.setPen(QPen(QColor("#bcfff0"),1));painter.setBrush(QColor("#56dfb5"));
        if(aircraft.track_deg)painter.drawPolygon(QPolygonF{QPointF(0,-9),QPointF(6,7),QPointF(0,4),QPointF(-6,7)});
        else painter.drawEllipse(QPointF(0,0),4,4);
        painter.restore();
        QFont aircraftFont=painter.font();aircraftFont.setPixelSize(12);aircraftFont.setWeight(QFont::DemiBold);painter.setFont(aircraftFont);painter.setPen(QColor("#e2f0f4"));const auto identity=aircraft.callsign.empty()?QString::number(aircraft.latest.address,16).toUpper():QString::fromStdString(aircraft.callsign);
        painter.drawText(point+QPointF(11,-7),identity);
    }
    if(!selectedIdentity_.isEmpty()){painter.setPen(QPen(QColor("#456473"),1));painter.setBrush(QColor(10,24,36,235));painter.drawRoundedRect(hud,5,5);QFont font=painter.font();font.setPixelSize(12);font.setBold(true);painter.setFont(font);painter.setPen(QColor("#bcfff0"));painter.drawText(hud.adjusted(9,3,-8,-32),Qt::AlignVCenter,selectedIdentity_);font.setPixelSize(11);font.setBold(false);painter.setFont(font);painter.setPen(QColor("#dae8ee"));painter.drawText(hud.adjusted(9,21,-8,-3),Qt::TextWordWrap|Qt::AlignVCenter,selectedContext_);}
    const auto center=mapUnproject(center_);const double span=std::max(.01,100/scale*40030*std::cos(center.latitude*std::numbers::pi/180));const double factor=std::pow(10.,std::floor(std::log10(span)));const double distance=(span/factor>=5?5:span/factor>=2?2:1)*factor;const double pixels=100*distance/span;
    painter.setPen(QPen(QColor("#b5cdd8"),2));const QPointF end(width()-14,height()-40);painter.drawLine(end,end-QPointF(pixels,0));painter.drawLine(end-QPointF(pixels,4),end-QPointF(pixels,-4));painter.drawLine(end-QPointF(0,4),end+QPointF(0,4));QFont scaleFont=painter.font();scaleFont.setPixelSize(10);painter.setFont(scaleFont);painter.drawText(QRect(width()-150,height()-62,136,17),Qt::AlignRight,text("map.scale",{{"distance",QString::number(distance,'g',4).toStdString()}}));
    painter.fillRect(QRect(0,height()-28,width(),28),QColor(12,23,33,225));painter.setPen(QColor("#afc7d4"));painter.drawText(QRect(10,height()-28,width()-20,28),Qt::AlignVCenter,caption_);
}
void MapView::wheelEvent(QWheelEvent* event){const auto before=(event->position()-QPointF(width()/2.,height()/2.))/(256*std::exp2(zoom_))+center_;zoom_=std::clamp(zoom_+event->angleDelta().y()/480.,0.,12.);center_=before-(event->position()-QPointF(width()/2.,height()/2.))/(256*std::exp2(zoom_));center_.setY(std::clamp(center_.y(),0.,1.));update();event->accept();}
void MapView::mousePressEvent(QMouseEvent* event){drag_=event->position();if(event->button()!=Qt::LeftButton)return;
    for(const auto& aircraft:aircraft_)if(aircraft.position && QLineF(screen(*aircraft.position),drag_).length()<16){selected_=aircraft_key(aircraft.latest);updateContext();if(selected)selected(*selected_);update();break;}}
void MapView::mouseMoveEvent(QMouseEvent* event){if(event->buttons()&Qt::LeftButton){center_-=(event->position()-drag_)/(256*std::exp2(zoom_));drag_=event->position();center_.setY(std::clamp(center_.y(),0.,1.));update();}}
}
