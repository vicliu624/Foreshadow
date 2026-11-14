#pragma once

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <span>

namespace signalscope::desktop {
inline void trace(QPainter& painter,const QRectF& plot,std::span<const float> values,
           double low,double high,const QColor& color) {
    QPainterPath path;
    bool started=false;
    for(std::size_t i=0;i<values.size();++i) {
        if(!std::isfinite(values[i])){started=false;continue;}
        const auto x=plot.left()+plot.width()*i/std::max<std::size_t>(1,values.size()-1);
        const auto y=plot.bottom()-plot.height()*std::clamp((values[i]-low)/(high-low),0.,1.);
        if(started)path.lineTo(x,y);
        else path.moveTo(x,y);
        started=true;
    }
    painter.setPen(QPen(color,1));
    painter.drawPath(path);
}

inline QRectF panel(QPainter& painter,int index,const QString& heading,
             const QString& xAxis,const QString& yAxis) {
    const QRectF rect(26+(index%2)*690,74+(index/2)*306,666,284);
    const auto plot=rect.adjusted(74,35,-14,-46);
    painter.setPen(QColor("#aebed1"));
    painter.drawText(rect.adjusted(0,0,0,-250),Qt::AlignHCenter|Qt::AlignTop,heading);
    painter.drawText(rect.adjusted(0,247,0,0),Qt::AlignCenter,xAxis);
    painter.drawText(QRectF(rect.left(),plot.top(),68,plot.height()),Qt::AlignVCenter|Qt::AlignLeft,yAxis);
    painter.setPen(QColor("#40536b"));
    painter.drawRect(plot);
    return plot;
}
}
