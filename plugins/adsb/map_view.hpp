#pragma once
#include "core/tracker.hpp"
#include "map_geography.hpp"
#include <QWidget>
#include <QPointF>
#include <QPolygonF>
#include <QToolButton>
#include <functional>
namespace signalscope::adsb {
class MapView final:public QWidget {
public:
    explicit MapView(QWidget* parent=nullptr,QString language="en");
    void setAircraft(std::vector<Aircraft>);
    void fitAircraft();
    void selectAircraft(std::uint64_t key);
    std::function<void(std::uint64_t)> selected;
    void setCaption(QString caption){caption_=std::move(caption);update();}
    bool hasBackground() const noexcept {return !land_.empty()||!geography_->countries().empty();}
    bool hasGeography() const noexcept {return !geography_->regions().empty();}
    QString geographicContext(Position p,bool nearby=true) const{return geography_->describe(p,language_,nearby);}
protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    QPointF project(Position) const;
    QPointF screen(Position) const;
    void updateContext();
    QString text(const std::string&,const nlohmann::json& arguments=nlohmann::json::object()) const;
    void zoomBy(double);
    std::vector<Aircraft> aircraft_;
    std::vector<QPolygonF> land_;
    QPointF center_{.5,.5},drag_;
    double zoom_=1;
    QString caption_;
    QString language_,selectedIdentity_,selectedContext_;
    std::shared_ptr<const MapGeography> geography_;
    QToolButton* zoom_in_=nullptr;
    QToolButton* zoom_out_=nullptr;
    std::optional<std::uint64_t> selected_;
};
}
