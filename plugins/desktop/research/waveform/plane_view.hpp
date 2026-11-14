#pragma once
#include "signalscope/backend/dsp/waveform.hpp"
#include <QWidget>

namespace signalscope::desktop {
// Generic paired-trace projection. Axis names and meaning belong to callers;
// an unsynchronized IQ projection is not labelled as a symbol constellation.
class TracePlaneView final:public QWidget {
public:
    explicit TracePlaneView(QWidget* parent=nullptr);
    void setAnalysis(std::shared_ptr<const WaveformAnalysis>,std::shared_ptr<const AnalysisTrace> x,std::shared_ptr<const AnalysisTrace> y);
    void setRange(SignalSampleRange);
    void setCursors(std::optional<std::uint64_t>,std::optional<std::uint64_t>);
    std::size_t renderedPrimitives() const noexcept {return rendered_;}
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    struct Point {double x=0,y=0;bool start=false;};
    struct Cell {unsigned count=0;};
    QRectF plot() const;
    void rebuild();
    std::shared_ptr<const WaveformAnalysis> analysis_;
    std::shared_ptr<const AnalysisTrace> x_,y_;
    SignalSampleRange range_;
    std::optional<std::uint64_t> a_,b_;
    std::vector<Point> points_;
    std::vector<Cell> cells_;
    std::size_t columns_=0,rows_=0,rendered_=0;
    unsigned maximum_density_=0;
    double extent_=1;
    bool dirty_=true;
};
}
