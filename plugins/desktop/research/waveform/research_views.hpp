#pragma once
#include "signalscope/backend/dsp/window_research.hpp"
#include <QImage>
#include <QPointF>
#include <QWidget>
#include <functional>

namespace signalscope::desktop {
class ResearchCurveView final:public QWidget {
public:
    explicit ResearchCurveView(QWidget* parent=nullptr);
    void setSeries(std::vector<QPointF>,QString x_unit,QString y_unit,bool histogram=false);
    std::size_t renderedPrimitives() const {return primitives_;}
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<QPointF> values_;QString x_unit_,y_unit_;bool histogram_=false;std::size_t primitives_=0;
};
struct ResearchBandSelection {SignalSampleRange range;double low_offset_hz=0,high_offset_hz=0;};
class ResearchSpectrogramView final:public QWidget {
public:
    explicit ResearchSpectrogramView(QWidget* parent=nullptr);
    void setResult(std::shared_ptr<const WindowResearch>,QString power_unit);
    void setColorRange(double minimum_db,double maximum_db);
    std::size_t cachedPixels() const {return std::size_t(cache_.width())*cache_.height();}
    std::function<void(const ResearchBandSelection&)> timeFrequencySelected;
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF plot() const;
    void rebuild();
    std::uint64_t sampleAt(double x) const;
    double frequencyAt(double y) const;
    std::shared_ptr<const WindowResearch> result_;QString power_unit_;QImage cache_;
    double low_db_=-100,high_db_=-20;bool dirty_=true;std::optional<QPointF> selection_;QPointF selection_end_;
};
}
