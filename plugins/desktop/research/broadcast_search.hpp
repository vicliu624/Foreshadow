#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
namespace signalscope::desktop {
// Search scheduling/presentation is shared; detection and result identities are
// supplied by each module. A result is a candidate until that module verifies it.
class BroadcastSearch final:public QGroupBox {
public:
    using Text=std::function<QString(const std::string&,const Json&)>;
    BroadcastSearch(Text text,QString prefix,double low,double high,double step,double dwell,QWidget* parent=nullptr):QGroupBox(parent),text_(std::move(text)) {
        setTitle(t("scan.title"));auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        const auto maximumFrequencyMHz=std::nextafter(std::ldexp(1.,64)/1e6,0.);
        low_=number(prefix+"-scan-low",0,maximumFrequencyMHz,low,3);high_=number(prefix+"-scan-high",0,maximumFrequencyMHz,high,3);step_=number(prefix+"-scan-step",.001,100,step,3);dwell_=number(prefix+"-scan-dwell",.5,30,dwell,1);
        form->addRow(t("scan.low"),low_);form->addRow(t("scan.high"),high_);form->addRow(t("scan.step"),step_);form->addRow(t("scan.dwell"),dwell_);layout->addLayout(form);
        auto* row=new QHBoxLayout;start_=new QPushButton(t("scan.start"));start_->setObjectName(prefix+"-scan-start");cancel_=new QPushButton(t("scan.cancel"));cancel_->setObjectName(prefix+"-scan-cancel");cancel_->setEnabled(false);row->addWidget(start_);row->addWidget(cancel_);layout->addLayout(row);
        results_=new QComboBox;results_->setObjectName(prefix+"-scan-results");results_->setMinimumContentsLength(15);results_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);layout->addWidget(results_);
        status_=new QLabel(t("scan.hint"));status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
        connect(start_,&QPushButton::clicked,this,[this]{begin();});connect(cancel_,&QPushButton::clicked,this,[this]{cancel();});
        connect(results_,&QComboBox::activated,this,[this](int index){if(index<0 || index>=int(values_.size()))return;const auto value=values_[index];cancel();if(selected)selected(value);});
        timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,[this]{tick();});
    }
    bool active() const {return active_;}
    QString summary() const {return status_->text();}
    void ready(){waiting_=false;clock_.restart();}
    void cancel(){if(!active_)return;active_=false;timer_.stop();if(stopProbe)stopProbe();setControls();status_->setText(t("scan.finished",{{"tested",tested_},{"found",values_.size()}}));}
    void fail(const QString& reason){cancel();status_->setText(reason);}
    std::function<void(double)> startProbe;
    std::function<void()> stopProbe;
    std::function<std::vector<Json>()> candidates;
    std::function<QString(const Json&)> caption;
    std::function<void(const Json&)> selected;
private:
    QString t(const std::string& key,const Json& args=Json::object()) const{return text_(key,args);}
    QDoubleSpinBox* number(const QString& name,double low,double high,double value,int decimals){auto* box=new QDoubleSpinBox;box->setObjectName(name);box->setDecimals(decimals);box->setRange(low,high);box->setValue(value);return box;}
    void setControls(){start_->setEnabled(!active_);cancel_->setEnabled(active_);for(auto* box:{low_,high_,step_,dwell_})box->setEnabled(!active_);}
    void begin(){
        if(active_ || !startProbe)return;
        const auto count=std::floor((high_->value()-low_->value())/step_->value()+.000001)+1;
        if(count<1 || count>4096){status_->setText(t("scan.invalid"));return;}
        values_=Json::array();results_->clear();tested_=0;count_=static_cast<unsigned>(count);cursor_=low_->value();active_=true;setControls();timer_.start();next();
    }
    void next(){if(!active_)return;waiting_=true;clock_.restart();startProbe(std::round(cursor_*1e6));if(active_)status_->setText(t("scan.progress",{{"index",tested_+1},{"total",count_},{"frequency",locale().toString(cursor_,'f',3).toUtf8().toStdString()}}));}
    void tick(){
        if(!active_)return;
        if(waiting_){if(clock_.elapsed()>15000)fail(t("scan.timeout"));return;}
        if(clock_.elapsed()<dwell_->value()*1000)return;
        if(candidates)for(const auto& value:candidates())if(std::find(values_.begin(),values_.end(),value)==values_.end()){values_.push_back(value);results_->addItem(caption?caption(value):QString::fromUtf8(value.dump()));}
        ++tested_;if(tested_>=count_){cancel();return;}if(stopProbe)stopProbe();cursor_=low_->value()+tested_*step_->value();next();
    }
    Text text_;QDoubleSpinBox *low_,*high_,*step_,*dwell_;QPushButton *start_,*cancel_;QComboBox* results_;QLabel* status_;QTimer timer_;QElapsedTimer clock_;bool active_=false,waiting_=false;unsigned tested_=0,count_=0;double cursor_=0;Json values_=Json::array();
};
}
