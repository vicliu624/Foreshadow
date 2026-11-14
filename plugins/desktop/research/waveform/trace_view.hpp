#pragma once

#include "signalscope/backend/dsp/waveform.hpp"
#include <QColor>
#include <QWidget>
#include <functional>

namespace signalscope::desktop {
struct TraceViewSeries {
    std::shared_ptr<const AnalysisTrace> trace;
    QColor color;
    QString label;
    std::optional<double> wrapping_period;
    bool dashed=false;
};

// A consumer of the trace protocol, not of modulation/provider identifiers.
// Cursor repaints reuse per-pixel aggregates; they never walk all IQ samples.
class TraceView final:public QWidget {
public:
    explicit TraceView(QWidget* parent=nullptr);
    void setAnalysis(std::shared_ptr<const WaveformAnalysis>,std::vector<TraceViewSeries>);
    void setRange(SignalSampleRange);
    SignalSampleRange range() const noexcept {return range_;}
    void setTimeOrigin(std::uint64_t);
    void setCursors(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b);
    void resetRange();
    void zoom(double factor,std::optional<std::uint64_t> center={});
    std::size_t renderedPrimitives() const noexcept {return rendered_primitives_;}
    std::function<void(SignalSampleRange)> rangeChanged;
    std::function<void(std::uint64_t,bool cursorA)> cursorChanged;
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    struct Bucket {double low=0,high=0;bool valid=false;};
    struct Point {double x=0,y=0;bool start=false;};
    struct CachedSeries {std::vector<Bucket> buckets;std::vector<Point> points;};
    QRectF plot() const;
    std::uint64_t sampleAt(double x) const;
    double xAt(std::uint64_t) const;
    void rebuildCache();
    void moveCursor(const QMouseEvent*);
    std::shared_ptr<const WaveformAnalysis> analysis_;
    std::vector<TraceViewSeries> series_;
    std::vector<CachedSeries> cache_;
    SignalSampleRange range_;
    std::uint64_t time_origin_=0;
    std::optional<std::uint64_t> a_,b_,selection_;
    double selection_x_=0,low_=-1,high_=1,left_margin_=56;
    bool cache_dirty_=true;
    std::size_t rendered_primitives_=0;
};
}
