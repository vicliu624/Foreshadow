#include "plugins/desktop/display/signal_display.hpp"
#include <QCoreApplication>
#include <QImage>
#include "signalscope/desktop/number_format.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSettings>
#include <QTabWidget>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace signalscope::desktop {
namespace {
void drawFrequencyAxis(QPainter& painter,const QRectF& plot,int width,double center,double rate,const QLocale& region) {
    const auto locale=measurementLocale(region);
    painter.setPen(QColor("#aebed1"));
    for(int i=0;i<=4;++i) {
        const double frequency=center-rate/2+i*rate/4;
        const auto x=plot.left()+i*plot.width()/4;
        painter.drawText(QRectF(std::clamp(x-55,0.0,std::max(0.,double(width)-110)),plot.bottom()+3,110,18),
                         Qt::AlignCenter,locale.toString(frequency/1e6,'f',3));
    }
}

void markSearchRange(QPainter& painter,const QRectF& plot,std::uint32_t rate,double searchWidth,bool label) {
    if(!rate)return;
    const auto half=std::clamp(searchWidth/(2*rate),0.0,.5);
    const auto left=plot.left()+plot.width()*(.5-half);
    const auto right=plot.left()+plot.width()*(.5+half);
    painter.fillRect(QRectF(plot.left(),plot.top(),left-plot.left(),plot.height()),QColor(0,0,0,70));
    painter.fillRect(QRectF(right,plot.top(),plot.right()-right,plot.height()),QColor(0,0,0,70));
    painter.setPen(QPen(QColor("#72a9db"),1,Qt::DashLine));
    painter.drawLine(QPointF(left,plot.top()),QPointF(left,plot.bottom()));
    painter.drawLine(QPointF(right,plot.top()),QPointF(right,plot.bottom()));
    if(label)painter.drawText(QRectF(left+5,plot.top()+3,right-left-10,18),Qt::AlignLeft,
                              QCoreApplication::translate("MonitorWindow", "Modulation search range"));
}

class SpectrumView:public SignalDisplay {
public:
    SpectrumView(){setMinimumHeight(72);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    void reset() override {frame_={};update();}
    void setFrame(const SpectrumFrame& frame) override {
        if(frame.sequence==frame_.sequence || frame.bins_dbfs.empty())return;
        frame_=frame;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),QColor("#0b1525"));p.setRenderHint(QPainter::Antialiasing);
        const bool compact=height()<150;
        const QRectF plot(55,18,width()-75,height()-45);
        const int intervals=compact?1:4;
        p.setPen(QColor("#8294ad"));
        for(int i=0;i<=intervals;++i) {
            const auto y=plot.top()+i*plot.height()/intervals;
            p.drawText(QRectF(0,y-8,48,16),Qt::AlignRight,locale().toString(-20-i*80/intervals));
            p.setPen(QColor("#233247"));p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));p.setPen(QColor("#8294ad"));
        }
        p.drawText(5,13,QCoreApplication::translate("MonitorWindow", "dBFS/bin"));
        if(frame_.bins_dbfs.empty()) {p.setPen(Qt::white);p.drawText(rect(),Qt::AlignCenter,QCoreApplication::translate("MonitorWindow", "Start listening to show the live spectrum"));return;}
        // Bound raster work by visible pixels while retaining both narrow
        // peaks and the noise floor. Thousands of overlapping antialiased
        // strokes per pixel can monopolize the GUI thread on a narrow dock.
        const auto columns=std::min(frame_.bins_dbfs.size(),static_cast<std::size_t>(std::max(1.,std::floor(plot.width()))));
        std::vector<QLineF> envelope;envelope.reserve(columns);QPolygonF peaks;peaks.reserve(static_cast<int>(columns));
        const auto y=[&](double level){return plot.top()+std::clamp((-20-level)/80.,0.,1.)*plot.height();};
        for(std::size_t column=0;column<columns;++column) {
            auto low=0.f,high=-200.f;bool valid=false;
            for(auto i=column*frame_.bins_dbfs.size()/columns;i<(column+1)*frame_.bins_dbfs.size()/columns;++i) {
                const auto value=frame_.bins_dbfs[i];if(!std::isfinite(value))continue;
                if(!valid){low=high=value;valid=true;}else{low=std::min(low,value);high=std::max(high,value);}
            }
            if(!valid)continue;
            const auto x=plot.left()+double(column)*plot.width()/std::max<std::size_t>(1,columns-1);
            envelope.emplace_back(x,y(high),x,y(low));peaks.append(QPointF(x,y(high)));
        }
        p.save();p.setClipRect(plot);p.setRenderHint(QPainter::Antialiasing,false);p.setPen(QPen(QColor("#5bdcc6"),1));
        p.drawLines(envelope.data(),static_cast<int>(envelope.size()));p.drawPolyline(peaks);p.restore();
        markSearchRange(p,plot,frame_.rate,frame_.search_width_hz,true);
        drawFrequencyAxis(p,plot,width(),frame_.frequency,frame_.rate,locale());
    }
private:
    SpectrumFrame frame_;
};

class WaterfallView:public SignalDisplay {
public:
    WaterfallView(){setMinimumHeight(72);}
    void reset() override {waterfall_={};update();}
    void setFrame(const SpectrumFrame& frame) override {
        if(frame.bins_dbfs.empty())return;
        if(waterfall_.isNull() || center_!=frame.frequency || rate_!=frame.rate) {
            waterfall_=QImage(512,100,QImage::Format_RGB32);
            waterfall_.fill(QColor("#0b1525"));
        }
        center_=frame.frequency;
        rate_=frame.rate;
        searchWidth_=frame.search_width_hz;
        const auto stride=static_cast<std::size_t>(waterfall_.bytesPerLine());
        for(int y=waterfall_.height()-1;y>0;--y)
            std::memcpy(waterfall_.scanLine(y),waterfall_.constScanLine(y-1),stride);
        for(int x=0;x<waterfall_.width();++x) {
            const auto bin=static_cast<std::size_t>(x)*frame.bins_dbfs.size()/waterfall_.width();
            const double level=std::clamp((frame.bins_dbfs[bin]+100)/85.0,0.0,1.0);
            waterfall_.setPixelColor(x,0,QColor::fromHsvF(static_cast<float>(.66-level*.66),.8f,
                                                         static_cast<float>(.12+.88*level)));
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),QColor("#0b1525"));
        if(waterfall_.isNull()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(),Qt::AlignCenter,QCoreApplication::translate("MonitorWindow", "Start listening to show the waterfall"));
            return;
        }
        const QRectF plot(55,10,width()-75,height()-51);
        painter.drawImage(plot,waterfall_);
        markSearchRange(painter,plot,rate_,searchWidth_,false);
        drawFrequencyAxis(painter,plot,width(),center_,rate_,locale());
        painter.drawText(QRectF(plot.left(),height()-19,plot.width(),18),Qt::AlignRight,
                         QCoreApplication::translate("MonitorWindow", "MHz · Waterfall snapshots from continuous capture"));
    }
private:
    QImage waterfall_;
    double center_=0;
    double searchWidth_=0;
    std::uint32_t rate_=0;
};
class IqView:public SignalDisplay {
public:
    IqView(){setMinimumHeight(160);}
    void reset() override {points_.clear();update();}
    void setFrame(const SpectrumFrame& f) override {points_=f.iq_preview;update();}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),QColor("#0b1525"));p.setPen(QColor("#9badc4"));
        p.drawText(12,20,QCoreApplication::translate("MonitorWindow", "Current I/Q · Normalized amplitude · I cyan / Q orange"));
        p.drawLine(12,height()/2,width()-12,height()/2);
        for(int component=0;component<2;++component) {
            QPainterPath path;
            for(std::size_t i=0;i<points_.size();++i) {
                const auto x=12+double(i)*(width()-24)/std::max<std::size_t>(1,points_.size()-1);
                const double value=component?points_[i].imag():points_[i].real();
                const auto y=height()/2.0-value*(height()-50)/2;
                if(i==0)path.moveTo(x,y);else path.lineTo(x,y);
            }
            p.setPen(QPen(QColor(component?"#f1b761":"#5bdcc6"),1.4));p.drawPath(path);
        }
    }
private:std::vector<std::complex<float>> points_;
};
}

std::vector<SignalDisplay*> createSignalDisplays() {
    QSettings settings;
    const auto combined=settings.value("display/spectrum-waterfall");
    if(combined.isValid()) {
        for(const auto* key:{"display/spectrum","display/waterfall"})
            if(!settings.contains(key))settings.setValue(key,combined);
    }
    std::vector<SignalDisplay*> displays;
    const auto mount=[&](SignalDisplay* view,const char* id,const QString& title) {
        view->setObjectName(QString::fromLatin1(id));
        view->setWindowTitle(title);
        displays.push_back(view);
    };
    mount(new SpectrumView,"spectrum",QCoreApplication::translate("MonitorWindow", "Spectrum (MHz)"));
    mount(new WaterfallView,"waterfall",QCoreApplication::translate("MonitorWindow", "Waterfall"));
    mount(new IqView,"iq-waveform",QCoreApplication::translate("MonitorWindow", "I/Q waveform"));
    return displays;
}
std::vector<SignalDisplay*> mountSignalDisplays(QTabWidget& tabs) {
    auto displays=createSignalDisplays();
    for(auto *view:displays)tabs.addTab(view,view->windowTitle());
    return displays;
}
}
