#include "trace_view.hpp"
#include <QLocale>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <limits>

namespace signalscope::desktop {
TraceView::TraceView(QWidget* parent):QWidget(parent) {
    setMinimumSize(150,60);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
}
QRectF TraceView::plot() const {const auto top=series_.size()>1?29:12;return QRectF(left_margin_,top,std::max(1.,width()-left_margin_-14),std::max(1,height()-top-22));}
void TraceView::setAnalysis(std::shared_ptr<const WaveformAnalysis> analysis,std::vector<TraceViewSeries> series) {
    if(analysis) {
        if(!analysis->source && !analysis->processed)throw std::invalid_argument("Trace view requires input provenance");
        for(const auto& item:series) {
            if(!item.trace)throw std::invalid_argument("Missing trace values");
            validate_analysis_trace(*analysis,*item.trace);
            if(item.wrapping_period && (!std::isfinite(*item.wrapping_period) || *item.wrapping_period<=0))
                throw std::invalid_argument("Invalid trace wrapping period");
        }
    }
    analysis_=std::move(analysis);series_=std::move(series);cache_dirty_=true;range_={};
    if(analysis_ && analysis_->sample_count()) {
        time_origin_=analysis_->time_origin();
        range_={analysis_->first_sample(),analysis_->end_sample()-analysis_->first_sample()};
    }
    update();
}
void TraceView::setTimeOrigin(std::uint64_t origin){time_origin_=origin;update();}
void TraceView::setCursors(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b){a_=a;b_=b;update();}
void TraceView::setRange(SignalSampleRange range) {
    if(!analysis_ || !analysis_->sample_count())return;
    const auto first=analysis_->first_sample(),end=analysis_->end_sample();
    range.first=std::clamp(range.first,first,end-1);
    range.count=std::clamp<std::uint64_t>(range.count,1,end-range.first);
    if(range==range_)return;
    range_=range;cache_dirty_=true;update();if(rangeChanged)rangeChanged(range_);
}
void TraceView::resetRange(){if(analysis_ && analysis_->sample_count())setRange({analysis_->first_sample(),analysis_->end_sample()-analysis_->first_sample()});}
double TraceView::xAt(std::uint64_t sample) const {
    const auto area=plot();if(!range_.count)return area.left();
    if(sample<range_.first)return area.left()-1;
    if(sample-range_.first>=range_.count)return area.right()+1;
    return area.left()+double(sample-range_.first)/std::max<std::uint64_t>(1,range_.count-1)*area.width();
}
std::uint64_t TraceView::sampleAt(double x) const {
    const auto fraction=std::clamp((x-plot().left())/plot().width(),0.,1.);
    if(fraction<=0 || !range_.count)return range_.first;
    if(fraction>=1)return range_.first+range_.count-1;
    const auto offset=static_cast<std::uint64_t>(std::round(fraction*static_cast<long double>(range_.count-1)));
    return range_.first+std::min(offset,range_.count?range_.count-1:0);
}
void TraceView::zoom(double factor,std::optional<std::uint64_t> selected) {
    if(!analysis_ || !range_.count || !std::isfinite(factor) || factor<=0)return;
    const auto start=analysis_->first_sample(),end=analysis_->end_sample();
    const auto center=std::clamp(selected.value_or(range_.first+range_.count/2),start,end-1);
    const auto requested=range_.count*static_cast<long double>(factor);
    const auto length=requested>=static_cast<long double>(end-start)?end-start:static_cast<std::uint64_t>(std::max<long double>(1,requested));
    const auto first=std::max(start,center-std::min(center-start,length/2));
    setRange({std::min(first,end-length),length});
}
void TraceView::rebuildCache() {
    cache_dirty_=false;cache_.clear();low_=std::numeric_limits<double>::infinity();high_=-low_;
    if(!analysis_ || !range_.count)return;
    const auto columns=static_cast<std::size_t>(std::max(1.,std::floor(plot().width())));
    const bool detailed=range_.count<=2*columns;
    const auto end=range_.first+range_.count;
    for(const auto& item:series_) {
        CachedSeries cached;cached.buckets.resize(columns);
        bool start=true;std::optional<std::uint64_t> next;double previous=0;
        for(const auto& span:analysis_trace_spans(*analysis_,*item.trace)) {
            if(!next || span.discontinuous_before || span.first_sample!=*next)start=true;
            const auto first=std::max(range_.first,span.first_sample);
            const auto last=std::min(end,span.first_sample+span.count);
            for(auto sample=first;sample<last;++sample) {
                const auto value=item.trace->values[span.first_value+static_cast<std::size_t>(sample-span.first_sample)];
                if(!std::isfinite(value)){start=true;continue;}
                low_=std::min(low_,value);high_=std::max(high_,value);
                const auto x=xAt(sample);
                if(detailed) {
                    if(!start && item.wrapping_period && std::abs(value-previous)>*item.wrapping_period/2)start=true;
                    cached.points.push_back({x,value,start});
                }
                const auto col=std::min(columns-1,static_cast<std::size_t>(std::max(0.,x-plot().left())));
                auto& bucket=cached.buckets[col];
                if(!bucket.valid){bucket.low=bucket.high=value;bucket.valid=true;}
                else {bucket.low=std::min(bucket.low,value);bucket.high=std::max(bucket.high,value);}
                start=false;previous=value;
            }
            next=span.first_sample+span.count;
        }
        cache_.push_back(std::move(cached));
    }
    if(!std::isfinite(low_)){low_=-1;high_=1;}
    else {const auto padding=std::max(1e-6,std::max(std::abs(low_),std::abs(high_))*.005+(high_-low_)*.09);low_-=padding;high_+=padding;}
    QFont axisFont=font();axisFont.setPointSizeF(std::max(8.5,axisFont.pointSizeF()*.9));const QFontMetricsF metrics(axisFont);
    double needed=42;for(int n=0;n<3;++n)needed=std::max(needed,metrics.horizontalAdvance(locale().toString(high_-(high_-low_)*n/2,'g',4))+9);
    needed=std::min(needed,double(std::max(42,width()-70)));
    if(needed!=left_margin_){left_margin_=needed;rebuildCache();}
}
void TraceView::paintEvent(QPaintEvent*) {
    if(cache_dirty_)rebuildCache();
    QPainter p(this);p.fillRect(rect(),palette().base());p.setRenderHint(QPainter::Antialiasing,false);
    const auto area=plot();auto grid=palette().text().color();grid.setAlpha(30);p.setPen(grid);
    const auto y=[&](double value){return area.bottom()-(value-low_)/(high_-low_)*area.height();};
    QFont f=font();f.setPointSizeF(std::max(8.5,f.pointSizeF()*.9));p.setFont(f);
    if(series_.size()>1) {
        const auto slot=area.width()/series_.size();
        for(std::size_t n=0;n<series_.size();++n) {
            const auto& series=series_[n];const auto left=area.left()+n*slot;
            p.setPen(QPen(series.color.isValid()?series.color:palette().highlight().color(),1,series.dashed?Qt::DashLine:Qt::SolidLine));
            p.drawLine(QPointF(left,10),QPointF(left+16,10));p.setPen(palette().text().color());
            p.drawText(QRectF(left+21,0,std::max(1.,slot-25),21),Qt::AlignLeft|Qt::AlignVCenter,
                QFontMetricsF(p.font()).elidedText(series.label,Qt::ElideRight,std::max(1.,slot-25)));
        }
    }
    for(int tick=0;tick<3;++tick) {
        const auto value=high_-(high_-low_)*tick/2,yy=y(value);
        p.setPen(grid);p.drawLine(QPointF(area.left(),yy),QPointF(area.right(),yy));p.setPen(palette().text().color());
        const auto label=locale().toString(value,'g',4);
        p.drawText(QRectF(0,yy-8,area.left()-7,16),Qt::AlignRight|Qt::AlignVCenter,label);
    }
    const int ticks=width()<400?2:4;
    for(int tick=0;tick<=ticks;++tick) {
        const auto xx=area.left()+area.width()*tick/ticks;p.setPen(grid);p.drawLine(QPointF(xx,area.top()),QPointF(xx,area.bottom()));
        if(analysis_ && range_.count) {
            const auto maximum=range_.count-1;
            const auto sample=range_.first+(maximum/ticks)*tick+(maximum%ticks)*tick/ticks;
            const auto seconds=(sample>=time_origin_?double(sample-time_origin_):-double(time_origin_-sample))/analysis_->sample_rate()+analysis_->time_offset_seconds();
            const auto milliseconds=seconds*1000;
            const auto label=std::abs(milliseconds)>=1e7?locale().toString(milliseconds,'g',4):locale().toString(milliseconds,'f',range_.count<1000?4:3);
            p.setPen(palette().text().color());p.drawText(QRectF(std::clamp(xx-34,0.,double(width()-68)),area.bottom()+3,68,17),Qt::AlignCenter,label);
        }
    }
    rendered_primitives_=0;p.save();p.setClipRect(area);
    if(a_ && b_) {auto fill=palette().highlight().color();fill.setAlpha(17);p.fillRect(QRectF(QPointF(std::min(xAt(*a_),xAt(*b_)),area.top()),QPointF(std::max(xAt(*a_),xAt(*b_)),area.bottom())),fill);}
    for(std::size_t series=0;series<cache_.size();++series) {
        const auto& cached=cache_[series];const auto color=series_[series].color.isValid()?series_[series].color:palette().highlight().color();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color,1,series_[series].dashed?Qt::DashLine:Qt::SolidLine));
        if(!cached.points.empty()) {
            QPainterPath path;
            for(const auto& point:cached.points) {if(point.start)path.moveTo(point.x,y(point.y));else path.lineTo(point.x,y(point.y));++rendered_primitives_;}
            p.drawPath(path);
            if(range_.count<=100) {p.setBrush(color);for(const auto& point:cached.points){p.drawEllipse(QPointF(point.x,y(point.y)),1.8,1.8);++rendered_primitives_;}}
        }else {
            std::vector<QLineF> lines;lines.reserve(cached.buckets.size());
            for(std::size_t n=0;n<cached.buckets.size();++n) {const auto& bucket=cached.buckets[n];if(!bucket.valid)continue;const auto x=area.left()+n;lines.emplace_back(x,y(bucket.high),x,y(bucket.low)+.5);}
            if(!lines.empty())p.drawLines(lines.data(),static_cast<int>(lines.size()));rendered_primitives_+=lines.size();
        }
    }
    for(const auto& cursor:{a_,b_})if(cursor && range_.count && *cursor>=range_.first && *cursor<range_.first+range_.count) {
        const auto x=xAt(*cursor);p.setPen(QPen(palette().highlight().color(),1,cursor==a_?Qt::DashLine:Qt::SolidLine));p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
    }
    if(selection_) {auto fill=palette().highlight().color();fill.setAlpha(40);p.fillRect(QRectF(QPointF(std::min(xAt(*selection_),selection_x_),area.top()),QPointF(std::max(xAt(*selection_),selection_x_),area.bottom())),fill);}
    p.restore();
}
void TraceView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);cache_dirty_=true;}
void TraceView::moveCursor(const QMouseEvent* event) {
    if(!analysis_ || !range_.count)return;
    const auto sample=sampleAt(event->position().x());const bool a=event->modifiers().testFlag(Qt::ShiftModifier);
    if(a)a_=sample;else b_=sample;update();if(cursorChanged)cursorChanged(sample,a);
}
void TraceView::mousePressEvent(QMouseEvent* event) {
    if(event->button()==Qt::RightButton && range_.count){selection_=sampleAt(event->position().x());selection_x_=event->position().x();update();}
    else if(event->button()==Qt::LeftButton)moveCursor(event);
}
void TraceView::mouseMoveEvent(QMouseEvent* event) {
    if(selection_){selection_x_=event->position().x();update();}
    else if(event->buttons().testFlag(Qt::LeftButton))moveCursor(event);
}
void TraceView::mouseReleaseEvent(QMouseEvent* event) {
    if(selection_ && event->button()==Qt::RightButton) {const auto end=sampleAt(event->position().x()),first=*selection_;selection_.reset();if(end!=first)setRange({std::min(first,end),std::max(first,end)-std::min(first,end)+1});update();}
}
void TraceView::wheelEvent(QWheelEvent* event){if(range_.count){zoom(event->angleDelta().y()>0?.5:2.,sampleAt(event->position().x()));event->accept();}}
void TraceView::keyPressEvent(QKeyEvent* event) {
    if(!range_.count){QWidget::keyPressEvent(event);return;}
    if(event->key()==Qt::Key_Home){resetRange();return;}
    if(event->key()==Qt::Key_Plus || event->key()==Qt::Key_Equal){zoom(.5,b_);return;}
    if(event->key()==Qt::Key_Minus){zoom(2,b_);return;}
    if(event->key()==Qt::Key_Left || event->key()==Qt::Key_Right) {
        const bool a=event->modifiers().testFlag(Qt::ShiftModifier);auto& cursor=a?a_:b_;
        const auto sample=std::clamp(cursor.value_or(range_.first),range_.first,range_.first+range_.count-1);
        cursor=event->key()==Qt::Key_Left?sample-std::uint64_t(sample>range_.first):sample+std::uint64_t(sample<range_.first+range_.count-1);
        update();if(cursorChanged)cursorChanged(*cursor,a);return;
    }
    QWidget::keyPressEvent(event);
}
}
