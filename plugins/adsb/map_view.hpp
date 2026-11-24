#pragma once
#include "core/tracker.hpp"
#include <QWidget>
#include <QPointF>
#include <QPolygonF>
#include <functional>
namespace signalscope::adsb {
class MapView final:public QWidget {
public:
    explicit MapView(QWidget* parent=nullptr);
    void setAircraft(std::vector<Aircraft>);
    void fitAircraft();
    void selectAircraft(std::uint64_t key);
    std::function<void(std::uint64_t)> selected;
    void setCaption(QString caption){caption_=std::move(caption);update();}
    bool hasBackground() const noexcept {return !land_.empty();}
protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    QPointF project(Position) const;
    QPointF screen(Position) const;
    std::vector<Aircraft> aircraft_;
    std::vector<QPolygonF> land_;
    QPointF center_{.5,.5},drag_;
    double zoom_=1;
    QString caption_;
    std::optional<std::uint64_t> selected_;
};
}
