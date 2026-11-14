#include "search_width_control.hpp"
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>
#include <cmath>

namespace signalscope::desktop {
SearchWidthControl::SearchWidthControl(QWidget* parent):QWidget(parent) {
    auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);
    presets_=new QComboBox;presets_->setObjectName("search-width-presets");
    presets_->setMinimumContentsLength(13);presets_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    custom_=new QSpinBox;custom_->setObjectName("search-width-custom");
    custom_->setRange(minimum(),maximum());custom_->setSuffix(" kHz");
    custom_->setKeyboardTracking(false);
    row->addWidget(presets_,1);row->addWidget(custom_);
    connect(presets_,&QComboBox::activated,this,[this](int index) {
        const auto choice=presets_->itemData(index).toString();
        mode_=choice=="maximum" || choice=="custom"?choice:QStringLiteral("fixed");
        if(mode_=="maximum")value_=std::max(1,available_);
        else if(mode_=="fixed")value_=choice.toInt();
        rebuild();emit valueChanged(value_);
        if(mode_=="custom"){custom_->setFocus();custom_->selectAll();}
    });
    connect(custom_,&QSpinBox::valueChanged,this,[this](int value) {
        value_=value;mode_="custom";emit valueChanged(value_);
    });
    rebuild();
}
int SearchWidthControl::value() const {return value_;}
int SearchWidthControl::recommendedValue() const {return available_;}
void SearchWidthControl::setValue(int khz) {setSelection(khz,"fixed");}
void SearchWidthControl::setSelection(int khz,const QString& mode) {
    mode_=mode=="maximum" || mode=="custom"?mode:QStringLiteral("fixed");
    value_=std::clamp(khz,minimum(),maximum());
    if(mode_=="maximum" && availableKnown_)value_=std::max(1,available_);
    rebuild();emit valueChanged(value_);
}
void SearchWidthControl::setAvailableWidth(double hz) {
    const auto limit=std::clamp(static_cast<int>(std::floor(hz/1000)),0,maximum());
    if(limit==available_ && availableKnown_)return;
    availableKnown_=true;
    available_=limit;
    const auto previous=value_;
    if(mode_=="maximum" && available_>=minimum())value_=available_;
    rebuild();if(value_!=previous)emit valueChanged(value_);
}
void SearchWidthControl::rebuild() {
    const QSignalBlocker blocked(presets_),blockedCustom(custom_);
    presets_->clear();
    const auto recommended=recommendedValue();
    if(recommended>=minimum())presets_->addItem(QCoreApplication::translate("SearchWidthControl","%1 kHz · Recommended").arg(recommended),QString::number(recommended));
    for(const int width:{250,500,1000,1500,2000,2500,5000,10000})
        if(width<=available_ && width!=recommended)presets_->addItem(QStringLiteral("%1 kHz").arg(width),QString::number(width));
    if(available_>=minimum())presets_->addItem(QCoreApplication::translate("SearchWidthControl","Current maximum · %1 kHz").arg(available_),"maximum");
    presets_->addItem(QCoreApplication::translate("SearchWidthControl","Custom…"),"custom");
    int selected=presets_->findData(mode_=="fixed"?QString::number(value_):mode_);
    if(selected<0){mode_="custom";selected=presets_->findData("custom");}
    presets_->setCurrentIndex(selected);custom_->setValue(value_);custom_->setVisible(mode_=="custom");
}
}
