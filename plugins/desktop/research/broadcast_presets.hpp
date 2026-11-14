#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QUuid>
#include <QVBoxLayout>

namespace signalscope::desktop {
// The module owns the opaque station identity/value and all visible resources.
// This editor only persists versioned named records in that module's settings.
class BroadcastPresets final:public QWidget {
public:
    BroadcastPresets(QSettings& settings,std::function<QString(const std::string&)> text,QString prefix,QWidget* parent=nullptr)
        :QWidget(parent),settings_(settings),text_(std::move(text)) {
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* row=new QHBoxLayout;list_=new QComboBox;list_->setObjectName(prefix+"-presets");list_->setMinimumContentsLength(12);list_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        name_=new QLineEdit;name_->setObjectName(prefix+"-preset-name");name_->setPlaceholderText(text_("preset.name"));
        name_->setMinimumWidth(40);
        auto* save=new QPushButton(text_("preset.save"));save_=save;save->setObjectName(prefix+"-preset-save");
        remove_=new QPushButton(text_("preset.remove"));remove_->setObjectName(prefix+"-preset-remove");
        layout->addWidget(list_);row->addWidget(name_,1);row->addWidget(save);row->addWidget(remove_);layout->addLayout(row);
        auto value=Json::parse(settings_.value("favorites_v1").toString().toUtf8().toStdString(),nullptr,false);
        const bool empty=!settings_.contains("favorites_v1");valid_=empty || (value.is_object() && value.contains("version") && value.at("version").is_number_integer() && value.at("version")==1 && value.contains("stations") && value.at("stations").is_array());
        if(valid_ && !empty) {
            document_=value;
            for(const auto& item:value.at("stations")) {
                if(item.is_object() && item.value("id",Json()).is_string() && item.value("name",Json()).is_string() && item.contains("value"))records_.push_back(item);
                else valid_=false;
            }
        }
        if(!valid_){auto* error=new QLabel(text_("preset.invalid"));error->setWordWrap(true);layout->addWidget(error);save->setEnabled(false);}
        populate();
        connect(list_,&QComboBox::currentIndexChanged,this,[this](int index){remove_->setEnabled(valid_ && index>=0);});
        connect(list_,&QComboBox::activated,this,[this](int index){if(index<0 || index>=int(records_.size()))return;name_->setText(QString::fromUtf8(records_[index].at("name").get<std::string>()));selecting_=true;if(selected)selected(records_[index].at("value"));selecting_=false;});
        connect(save,&QPushButton::clicked,this,[this]{if(current)saveValue(current());});
        connect(remove_,&QPushButton::clicked,this,[this]{const auto index=list_->currentIndex();if(index<0)return;records_.erase(records_.begin()+index);persist();populate();});
    }
    void saveValue(const Json& value) {
        if(!valid_ || value.is_null() || value.empty())return;
        const auto found=std::find_if(records_.begin(),records_.end(),[&](const auto& r){return sameStation?sameStation(r.at("value"),value):r.at("value")==value;});
        auto name=name_->text().trimmed();
        if(name.isEmpty() && found!=records_.end())name=QString::fromUtf8(found->at("name").get<std::string>());
        if(name.isEmpty())name=defaultName?defaultName(value):text_("preset.unnamed");
        std::size_t index=std::distance(records_.begin(),found);
        if(found!=records_.end()){(*found)["name"]=name.toUtf8().toStdString();(*found)["value"]=value;}
        else records_.push_back({{"id",QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()},{"name",name.toUtf8().toStdString()},{"value",value}});
        persist();populate();list_->setCurrentIndex(static_cast<int>(index));
    }
    void clearSelection(){if(selecting_)return;list_->setCurrentIndex(-1);name_->clear();}
    void setSaveAvailable(bool available){save_->setEnabled(valid_ && available);}
    std::function<Json()> current;
    std::function<void(const Json&)> selected;
    std::function<QString(const Json&)> defaultName;
    std::function<bool(const Json&,const Json&)> sameStation;
private:
    void persist(){document_["version"]=1;document_["stations"]=records_;settings_.setValue("favorites_v1",QString::fromUtf8(document_.dump()));}
    void populate(){list_->clear();list_->setPlaceholderText(text_("preset.placeholder"));for(const auto& r:records_)list_->addItem(QString::fromUtf8(r.at("name").get<std::string>()));list_->setCurrentIndex(-1);remove_->setEnabled(false);}
    QSettings& settings_;std::function<QString(const std::string&)> text_;Json records_=Json::array(),document_=Json::object();bool valid_=true,selecting_=false;
    QComboBox* list_;QLineEdit* name_;QPushButton *remove_,*save_;
};
}
