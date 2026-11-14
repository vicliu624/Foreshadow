#include "plane_view.hpp"
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace signalscope::desktop {
TracePlaneView::TracePlaneView(QWidget* parent):QWidget(parent){setMinimumSize(160,180);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
QRectF TracePlaneView::plot() const {
    const auto size=std::max(1,std::min(width()-70,height()-44));
    return QRectF(48+(width()-70-size)/2.,12+(height()-44-size)/2.,size,size);
}
void TracePlaneView::setAnalysis(std::shared_ptr<const WaveformAnalysis> analysis,std::shared_ptr<const AnalysisTrace> x,std::shared_ptr<const AnalysisTrace> y) {
    if(analysis && ((!analysis->source && !analysis->processed) || !x || !y || y->values.size()!=x->values.size()))
        throw std::invalid_argument("Paired traces require the same source sample axis");
    if(analysis) {
        validate_analysis_trace(*analysis,*x);validate_analysis_trace(*analysis,*y);
        if(analysis_trace_spans(*analysis,*x)!=analysis_trace_spans(*analysis,*y))throw std::invalid_argument("Paired traces have different packed mappings");
    }
    analysis_=std::move(analysis);x_=std::move(x);y_=std::move(y);range_={};
    if(analysis_ && analysis_->sample_count())range_={analysis_->first_sample(),analysis_->end_sample()-analysis_->first_sample()};
    dirty_=true;update();
}
void TracePlaneView::setRange(SignalSampleRange range){if(range==range_)return;range_=range;dirty_=true;update();}
void TracePlaneView::setCursors(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b){a_=a;b_=b;update();}
void TracePlaneView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);dirty_=true;}
void TracePlaneView::rebuild() {
    dirty_=false;points_.clear();cells_.clear();maximum_density_=0;extent_=1;
    if(!analysis_ || !range_.count)return;
    extent_=0;
    const auto end=range_.first>UINT64_MAX-range_.count?UINT64_MAX:range_.first+range_.count;
    for(const auto& span:analysis_trace_spans(*analysis_,*x_))for(auto sample=std::max(range_.first,span.first_sample);sample<std::min(end,span.first_sample+span.count);++sample) {
        const auto index=span.first_value+static_cast<std::size_t>(sample-span.first_sample);
        const auto x=x_->values[index],y=y_->values[index];if(std::isfinite(x) && std::isfinite(y))extent_=std::max({extent_,std::abs(x),std::abs(y)});
    }
    extent_=std::max(1e-6,extent_*1.08);
    columns_=rows_=static_cast<std::size_t>(std::clamp(plot().width()/2.,1.,256.));cells_.resize(columns_*rows_);
    const bool detail=range_.count<=std::max<std::size_t>(128,4*columns_);
    bool start=true;std::optional<std::uint64_t> next;
    for(const auto& span:analysis_trace_spans(*analysis_,*x_)) {
        if(!next || span.discontinuous_before || span.first_sample!=*next)start=true;
        for(auto sample=std::max(range_.first,span.first_sample);sample<std::min(end,span.first_sample+span.count);++sample) {
            const auto index=span.first_value+static_cast<std::size_t>(sample-span.first_sample);
            const auto x=x_->values[index],y=y_->values[index];if(!std::isfinite(x) || !std::isfinite(y)){start=true;continue;}
            if(detail)points_.push_back({x,y,start});start=false;
            const auto col=std::min(columns_-1,static_cast<std::size_t>(std::clamp((x/extent_+1)*.5,0.,1.)*columns_));
            const auto row=std::min(rows_-1,static_cast<std::size_t>(std::clamp((1-y/extent_)*.5,0.,1.)*rows_));
            maximum_density_=std::max(maximum_density_,++cells_[row*columns_+col].count);
        }
        next=span.first_sample+span.count;
    }
}
void TracePlaneView::paintEvent(QPaintEvent*) {
    if(dirty_)rebuild();QPainter p(this);p.fillRect(rect(),palette().base());const auto area=plot();
    auto grid=palette().text().color();grid.setAlpha(35);
    const auto point=[&](double x,double y){return QPointF(area.center().x()+x/extent_*area.width()/2,area.center().y()-y/extent_*area.height()/2);};
    p.setPen(grid);for(int tick=-1;tick<=1;++tick){const auto xy=point(tick*extent_,tick*extent_);p.drawLine(QPointF(xy.x(),area.top()),QPointF(xy.x(),area.bottom()));p.drawLine(QPointF(area.left(),xy.y()),QPointF(area.right(),xy.y()));
        p.setPen(palette().text().color());const auto value=locale().toString(tick*extent_,'g',3);
        p.drawText(QRectF(0,xy.y()-10,area.left()-6,20),Qt::AlignRight|Qt::AlignVCenter,value);
        p.drawText(QRectF(xy.x()-32,area.bottom()+3,64,20),Qt::AlignCenter,value);p.setPen(grid);}
    rendered_=0;p.save();p.setClipRect(area);
    if(!points_.empty()) {
        p.setPen(palette().highlight().color());QPainterPath path;for(const auto& value:points_){if(value.start)path.moveTo(point(value.x,value.y));else path.lineTo(point(value.x,value.y));++rendered_;}p.drawPath(path);
        p.setBrush(palette().highlight());for(const auto& value:points_){p.drawEllipse(point(value.x,value.y),1.6,1.6);++rendered_;}
    }else if(maximum_density_) {
        p.setPen(Qt::NoPen);for(std::size_t row=0;row<rows_;++row)for(std::size_t col=0;col<columns_;++col){const auto count=cells_[row*columns_+col].count;if(!count)continue;
            auto color=palette().highlight().color();color.setAlphaF(.2+.8*std::log1p(count)/std::log1p(maximum_density_));p.setBrush(color);
            p.drawRect(QRectF(area.left()+col*area.width()/columns_,area.top()+row*area.height()/rows_,area.width()/columns_,area.height()/rows_));++rendered_;}
    }
    p.setPen(palette().text().color());p.setBrush(palette().base());
    for(const auto& cursor:{a_,b_})if(cursor && analysis_ && *cursor>=range_.first && *cursor-range_.first<range_.count)if(const auto index=analysis_trace_value_index(*analysis_,*x_,*cursor)) {
        const auto x=x_->values[*index],y=y_->values[*index];if(!std::isfinite(x) || !std::isfinite(y))continue;
        const auto at=point(x,y);p.drawEllipse(at,4,4);p.drawText(at+QPointF(6,-6),cursor==a_?QStringLiteral("A"):QStringLiteral("B"));
    }
    p.restore();
}
}
