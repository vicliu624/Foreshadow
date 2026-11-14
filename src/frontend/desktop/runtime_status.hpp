#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QHBoxLayout>
#include <QToolButton>
#include <QResizeEvent>
#include <QStylePainter>
#include <QStyleOptionToolButton>
#include <QWidget>
#include <algorithm>
namespace signalscope::desktop {
class RuntimeStatusButton final:public QToolButton {
public:
    explicit RuntimeStatusButton(QWidget* parent):QToolButton(parent){setToolButtonStyle(Qt::ToolButtonTextOnly);}
    QSize sizeHint() const override {auto value=QToolButton::sizeHint();value.setWidth(std::clamp(value.width(),80,240));return value;}
    QSize minimumSizeHint() const override {return {60,QToolButton::sizeHint().height()};}
protected:
    void paintEvent(QPaintEvent*) override {QStylePainter painter(this);QStyleOptionToolButton option;initStyleOption(&option);option.text=fontMetrics().elidedText(text(),Qt::ElideRight,std::max(0,width()-16));painter.drawComplexControl(QStyle::CC_ToolButton,option);}
};
class RuntimeStatus final:public QWidget {
public:
    explicit RuntimeStatus(QString language,QWidget* parent=nullptr):QWidget(parent),language_(std::move(language)),catalog_(TextCatalog::resource("desktop-status")) {
        setObjectName("runtime-status");auto* row=new QHBoxLayout(this);row->setContentsMargins(0,0,0,0);row->setSpacing(4);
        device=button("runtime-device");tasks=button("runtime-tasks");load=button("runtime-load");health=button("runtime-health");
        tasks->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        row->addWidget(device);row->addWidget(tasks,1);row->addWidget(load);row->addWidget(health);device->setMaximumWidth(260);health->setMaximumWidth(260);
    }
    QString text(const std::string& key,const Json& args=Json::object()) const {Json value={{"label_key",key},{"arguments",args}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));}
    QToolButton *device,*tasks,*load,*health;
protected:
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);load->setVisible(width()>=850);device->setVisible(width()>=650);}
private:
    QToolButton* button(const char* name){auto* result=new RuntimeStatusButton(this);result->setObjectName(name);result->setAutoRaise(true);result->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);return result;}
    QString language_;TextCatalog catalog_;
};
}
