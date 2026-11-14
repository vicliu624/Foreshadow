#include "research_views.hpp"
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <limits>

namespace signalscope::desktop {
namespace {
QString number(const QLocale& locale,double value){return locale.toString(value,'g',5);}
void axes(QPainter& painter,const QWidget& widget,const QRectF& area,double x0,double x1,double y0,double y1,const QString& xunit,const QString& yunit) {
    auto grid=widget.palette().text().color();grid.setAlpha(32);painter.setPen(grid);painter.drawRect(area);
    for(int i=0;i<5;++i) {
        const auto fraction=i/4.,x=area.left()+fraction*area.width(),y=area.bottom()-fraction*area.height();
        painter.setPen(grid);painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));painter.setPen(widget.palette().text().color());
        painter.drawText(QRectF(x-43,area.bottom()+3,86,20),Qt::AlignHCenter|Qt::AlignTop,number(widget.locale(),x0+(x1-x0)*fraction));
        painter.drawText(QRectF(0,y-10,area.left()-6,20),Qt::AlignRight|Qt::AlignVCenter,number(widget.locale(),y0+(y1-y0)*fraction));
    }
    painter.drawText(QRectF(area.left(),area.bottom()+22,area.width(),18),Qt::AlignHCenter,xunit);
    painter.drawText(QRectF(4,0,area.left()+area.width(),18),Qt::AlignLeft,yunit);
}
QColor heat(double fraction) {
    fraction=std::clamp(fraction,0.,1.);
    if(fraction<.5){const auto t=fraction*2;return QColor::fromRgbF(.04+.32*t,.06+.3*t,.13+.6*t);}
    const auto t=(fraction-.5)*2;return QColor::fromRgbF(.36+.64*t,.36+.5*t,.73-.58*t);
}
}
ResearchCurveView::ResearchCurveView(QWidget* parent):QWidget(parent){setMinimumSize(180,140);}
void ResearchCurveView::setSeries(std::vector<QPointF> values,QString x_unit,QString y_unit,bool histogram) {
    if(values.size()>65536)throw std::length_error("Research curve exceeds its point budget");
    for(std::size_t i=0;i<values.size();++i)if(!std::isfinite(values[i].x()) || (i && values[i].x()<values[i-1].x()))throw std::invalid_argument("Research curve requires an ordered finite X axis");
    values_=std::move(values);x_unit_=std::move(x_unit);y_unit_=std::move(y_unit);histogram_=histogram;update();
}
void ResearchCurveView::paintEvent(QPaintEvent*) {
    QPainter painter(this);painter.fillRect(rect(),palette().base());const QRectF area(76,22,std::max(1,width()-96),std::max(1,height()-68));primitives_=0;
    if(values_.empty())return;
    const auto x0=values_.front().x(),x1=std::max(x0+1e-12,values_.back().x());double low=std::numeric_limits<double>::infinity(),high=-low;
    for(auto value:values_)if(std::isfinite(value.y())){low=std::min(low,value.y());high=std::max(high,value.y());}
    if(!std::isfinite(low)){low=0;high=1;}if(histogram_)low=std::min(low,0.);
    const auto padding=std::max(1e-9,(high-low)*.05);low-=padding;high+=padding;
    axes(painter,*this,area,x0,x1,low,high,x_unit_,y_unit_);painter.save();painter.setClipRect(area);painter.setPen(palette().highlight().color());
    const auto x=[&](double v){return area.left()+(v-x0)/(x1-x0)*area.width();};const auto y=[&](double v){return area.bottom()-(v-low)/(high-low)*area.height();};
    // Per-column min/max preserves narrow spectral peaks. Empty columns remain
    // empty, and invalid values break paths instead of joining observations.
    struct Bucket {double minimum=0,maximum=0,first=0,last=0;bool valid=false,break_before=false;};
    const auto columns=static_cast<std::size_t>(std::max(1.,std::ceil(area.width())));std::vector<Bucket> buckets(columns);bool discontinuous=true;
    for(auto value:values_) {
        if(!std::isfinite(value.y())){discontinuous=true;continue;}
        auto& bucket=buckets[std::min(columns-1,static_cast<std::size_t>(std::max(0.,x(value.x())-area.left())))];
        if(!bucket.valid){bucket.minimum=bucket.maximum=bucket.first=value.y();bucket.valid=true;bucket.break_before=discontinuous;}
        else {bucket.minimum=std::min(bucket.minimum,value.y());bucket.maximum=std::max(bucket.maximum,value.y());}
        bucket.last=value.y();discontinuous=false;
    }
    QPainterPath path;bool begun=false;
    for(std::size_t i=0;i<buckets.size();++i) {
        const auto& bucket=buckets[i];if(!bucket.valid)continue;const auto xx=area.left()+i+.5;
        if(histogram_){painter.drawLine(QPointF(xx,y(0)),QPointF(xx,y(bucket.maximum)));++primitives_;continue;}
        if(!begun || bucket.break_before)path.moveTo(xx,y(bucket.first));else path.lineTo(xx,y(bucket.first));
        path.lineTo(xx,y(bucket.minimum));path.lineTo(xx,y(bucket.maximum));path.lineTo(xx,y(bucket.last));primitives_+=4;begun=true;
    }
    painter.drawPath(path);painter.restore();
}
ResearchSpectrogramView::ResearchSpectrogramView(QWidget* parent):QWidget(parent){setMinimumSize(180,170);setMouseTracking(true);}
QRectF ResearchSpectrogramView::plot() const {return QRectF(76,22,std::max(1,width()-96),std::max(1,height()-68));}
void ResearchSpectrogramView::setResult(std::shared_ptr<const WindowResearch> result,QString power_unit) {
    if(result) {
        if(!result->analysis || !result->range.count || result->range.first>UINT64_MAX-result->range.count || result->spectral_power.size()>4*1024*1024 ||
           result->options.fft_size<16 || result->options.fft_size>65536 || !result->options.hop_samples ||
           result->frame_first_samples.size()>SIZE_MAX/result->options.fft_size || result->spectral_power.size()!=result->frame_first_samples.size()*result->options.fft_size)
            throw std::invalid_argument("Invalid spectrogram matrix contract");
        std::optional<std::uint64_t> previous;
        for(auto first:result->frame_first_samples){if(first<result->range.first || first>result->range.first+result->range.count || result->options.fft_size>result->range.first+result->range.count-first || (previous && first<=*previous))throw std::invalid_argument("Invalid spectrogram frame support");previous=first;}
        for(auto power:result->spectral_power)if(!std::isfinite(power) || power<0)throw std::invalid_argument("Invalid spectral power observation");
    }
    result_=std::move(result);power_unit_=std::move(power_unit);selection_.reset();dirty_=true;update();
}
void ResearchSpectrogramView::setColorRange(double low,double high) {
    if(!std::isfinite(low) || !std::isfinite(high) || low>=high)throw std::invalid_argument("Invalid spectral display color range");
    low_db_=low;high_db_=high;dirty_=true;update();
}
void ResearchSpectrogramView::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);dirty_=true;}
void ResearchSpectrogramView::rebuild() {
    dirty_=false;cache_={};if(!result_)return;
    const auto columns=std::clamp(static_cast<int>(plot().width()),1,1024);
    const auto maximum_rows=std::min(512,(4*1024*1024)/(8*columns+4));
    const auto rows=std::clamp(static_cast<int>(plot().height()),1,maximum_rows);
    cache_=QImage(columns,rows,QImage::Format_ARGB32);cache_.fill(palette().base().color());
    // At most 4 MiB combined raster and linear aggregation workspace.
    std::vector<float> grid(std::size_t(columns)*rows,-1),reduced(static_cast<std::size_t>(rows),-1);
    const auto size=result_->options.fft_size;const auto tile_width=std::min(result_->options.hop_samples,size);
    for(std::size_t frame=0;frame<result_->frame_first_samples.size();++frame) {
        std::fill(reduced.begin(),reduced.end(),-1);
        for(std::size_t bin=0;bin<size;++bin) {
            const auto first_row=(size-1-bin)*rows/size,last_row=((size-bin)*rows+size-1)/size;
            for(auto row=first_row;row<last_row;++row)reduced[row]=std::max(reduced[row],result_->spectral_power[frame*size+bin]);
        }
        const auto center=double(result_->frame_first_samples[frame]-result_->range.first)+(size-1)/2.;
        const auto left=std::clamp(static_cast<int>(std::floor((center-tile_width/2.)/result_->range.count*columns)),0,columns-1);
        const auto right=std::clamp(static_cast<int>(std::ceil((center+tile_width/2.)/result_->range.count*columns)),left+1,columns);
        for(int row=0;row<rows;++row)if(reduced[row]>=0)for(int column=left;column<right;++column)grid[std::size_t(row)*columns+column]=std::max(grid[std::size_t(row)*columns+column],reduced[row]);
    }
    for(int row=0;row<rows;++row)for(int column=0;column<columns;++column) {
        const auto power=grid[std::size_t(row)*columns+column];if(power<0)continue;
        const auto db=power>0?10*std::log10(power):low_db_;cache_.setPixelColor(column,row,heat((db-low_db_)/(high_db_-low_db_)));
    }
}
void ResearchSpectrogramView::paintEvent(QPaintEvent*) {
    if(dirty_)rebuild();QPainter painter(this);painter.fillRect(rect(),palette().base());const auto area=plot();if(!result_)return;
    painter.drawImage(area,cache_);const auto first=result_->range.first,origin=result_->analysis->time_origin();
    const auto start=(first>=origin?double(first-origin):-double(origin-first))/result_->analysis->sample_rate()+result_->analysis->time_offset_seconds();
    const auto finish=start+double(result_->range.count)/result_->analysis->sample_rate();const auto rate=result_->analysis->sample_rate();
    axes(painter,*this,area,start*1000,finish*1000,-double(rate)/2,rate/2.,QStringLiteral("ms"),QStringLiteral("Hz"));
    painter.setPen(palette().text().color());painter.drawText(QRectF(area.left()+area.width()/2,0,area.width()/2,18),Qt::AlignRight,number(locale(),low_db_)+QStringLiteral(" … ")+number(locale(),high_db_)+QStringLiteral(" ")+power_unit_);
    if(selection_){painter.setPen(palette().highlight().color());painter.setBrush(Qt::NoBrush);painter.drawRect(QRectF(*selection_,selection_end_).normalized().intersected(area));}
}
std::uint64_t ResearchSpectrogramView::sampleAt(double x) const {
    const auto fraction=std::clamp((x-plot().left())/plot().width(),0.,1.);if(fraction<=0)return result_->range.first;
    if(fraction>=1)return result_->range.first+result_->range.count-1;
    const auto offset=static_cast<std::uint64_t>(fraction*static_cast<long double>(result_->range.count-1));return result_->range.first+std::min(offset,result_->range.count-1);
}
double ResearchSpectrogramView::frequencyAt(double y) const {return (.5-std::clamp((y-plot().top())/plot().height(),0.,1.))*result_->analysis->sample_rate();}
void ResearchSpectrogramView::mousePressEvent(QMouseEvent* event){if(result_ && event->button()==Qt::LeftButton && plot().contains(event->position())){selection_=event->position();selection_end_=*selection_;update();}}
void ResearchSpectrogramView::mouseMoveEvent(QMouseEvent* event){if(selection_){selection_end_=event->position();update();}}
void ResearchSpectrogramView::mouseReleaseEvent(QMouseEvent* event) {
    if(!selection_ || !result_ || event->button()!=Qt::LeftButton)return;
    const auto start=*selection_,finish=event->position();selection_.reset();update();
    if(std::abs(start.y()-finish.y())<3 || std::abs(start.x()-finish.x())<3)return;
    const auto first=std::min(sampleAt(start.x()),sampleAt(finish.x())),last=std::max(sampleAt(start.x()),sampleAt(finish.x()));
    const auto low=std::min(frequencyAt(start.y()),frequencyAt(finish.y())),high=std::max(frequencyAt(start.y()),frequencyAt(finish.y()));
    if(timeFrequencySelected)timeFrequencySelected({{first,last-first+1},low,high});
}
}
