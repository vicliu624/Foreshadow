#include "parameter_editor.hpp"
#include "signalscope/desktop/plugin_text.hpp"
#include <QLocale>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace signalscope::desktop {
namespace {
QString choiceLabel(const Json& descriptor,const Json& value) {
    const auto id=value.is_string()?value.get<std::string>():value.dump();
    const auto labels=descriptor.value("choice_labels",Json::object());
    if(labels.contains(id))return pluginText(labels.at(id).value("label",Json()),QString::fromStdString(id));
    return QString::fromStdString(id);
}
}
QString parameterLabel(const Json& descriptor) {
    return pluginText(descriptor.value("label",Json()),QString::fromStdString(descriptor.value("key",std::string())));
}
ParameterEditor::ParameterEditor(QWidget* parent,QString prefix):QWidget(parent),prefix_(std::move(prefix)),layout_(new QFormLayout(this)) {
    layout_->setContentsMargins(0,0,0,0);
    layout_->setRowWrapPolicy(QFormLayout::WrapLongRows);layout_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}
void ParameterEditor::populate(const Json& descriptors,const Json& saved) {
    while(layout_->count()) {auto* item=layout_->takeAt(0);if(item->widget())delete item->widget();delete item;}
    rows_.clear();
    for(const auto& d:descriptors) {
        if(!d.value("extension",false))continue;
        const auto key=d.at("key").get<std::string>(),type=d.value("type",std::string("string"));
        QWidget* field=nullptr;
        const auto change=[this]{if(changed)changed();};
        if(type=="boolean") {
            auto* box=new QCheckBox;field=box;connect(box,&QCheckBox::toggled,this,change);
        } else if(type=="enum") {
            auto* box=new QComboBox;field=box;
            for(const auto& value:d.at("choices"))box->addItem(choiceLabel(d,value),QString::fromStdString(value.dump()));
            connect(box,&QComboBox::currentIndexChanged,this,change);
        } else if(type=="integer" || type=="number") {
            auto* box=new QDoubleSpinBox;field=box;box->setDecimals(type=="integer"?0:6);
            box->setRange(d.value("minimum",-1e12),d.value("maximum",1e12));box->setSingleStep(d.value("step",1.));
            if(d.contains("unit"))box->setSuffix(" "+QString::fromStdString(d.at("unit").get<std::string>()));
            connect(box,&QDoubleSpinBox::valueChanged,this,change);
        } else {
            auto* edit=new QLineEdit;field=edit;connect(edit,&QLineEdit::textEdited,this,change);
        }
        field->setObjectName(prefix_+QString::fromStdString(key));
        auto* column=new QWidget;auto* v=new QVBoxLayout(column);v->setContentsMargins(0,0,0,0);v->setSpacing(2);v->addWidget(field);
        auto* issue=new QLabel;issue->setWordWrap(true);issue->setStyleSheet("color:#e76c61");issue->hide();v->addWidget(issue);
        auto* label=new QLabel(parameterLabel(d));label->setWordWrap(true);layout_->addRow(label,column);
        rows_.push_back({key,type,field,label,issue});
    }
    Json initial=Json::object();for(const auto& d:descriptors)if(d.value("extension",false) && d.contains("default"))initial[d.at("key").get<std::string>()]=d.at("default");
    initial.update(saved);setValues(initial);updateDescriptors(descriptors);setVisible(!rows_.empty());
}
Json ParameterEditor::values() const {
    auto result=Json::object();
    for(const auto& row:rows_) {
        if(const auto* box=qobject_cast<QCheckBox*>(row.field))result[row.key]=box->isChecked();
        else if(const auto* combo=qobject_cast<QComboBox*>(row.field))result[row.key]=Json::parse(combo->currentData().toString().toStdString());
        else if(const auto* number=qobject_cast<QDoubleSpinBox*>(row.field)) {
            if(row.type=="integer")result[row.key]=static_cast<std::int64_t>(number->value());else result[row.key]=number->value();
        }else result[row.key]=qobject_cast<QLineEdit*>(row.field)->text().toStdString();
    }
    return result;
}
void ParameterEditor::setValues(const Json& values) {
    for(const auto& row:rows_) {
        if(!values.contains(row.key))continue;const auto& value=values.at(row.key);const QSignalBlocker guard(row.field);
        if(auto* box=qobject_cast<QCheckBox*>(row.field)) {if(value.is_boolean())box->setChecked(value.get<bool>());}
        else if(auto* combo=qobject_cast<QComboBox*>(row.field)) {
            const auto data=QString::fromStdString(value.dump());int index=combo->findData(data);
            if(index<0){combo->addItem(QString::fromStdString(value.is_string()?value.get<std::string>():value.dump()),data);index=combo->count()-1;}
            combo->setCurrentIndex(index);
        }else if(auto* number=qobject_cast<QDoubleSpinBox*>(row.field)) {
            if(value.is_number()) {
                const auto v=value.get<double>();
                // Preserve invalid saved values so plugin validation can explain them.
                number->setRange(std::min(number->minimum(),v),std::max(number->maximum(),v));number->setValue(v);
            }
        }
        else if(value.is_string())qobject_cast<QLineEdit*>(row.field)->setText(QString::fromStdString(value.get<std::string>()));
    }
}
void ParameterEditor::updateDescriptors(const Json& descriptors) {
    descriptors_=descriptors;
    const auto selected=values();
    for(auto& d:descriptors_)if(d.contains("enabled_when")) {
        const auto& rule=d.at("enabled_when");d["enabled"]=selected.value(rule.at("key").get<std::string>(),Json())==rule.at("equals");
    }
    for(const auto& row:rows_)for(const auto& d:descriptors_)if(d.value("key",std::string())==row.key) {
        const QSignalBlocker blocked(row.field);
        row.label->setText(parameterLabel(d));row.field->setEnabled(d.value("enabled",true) && !d.value("read_only",false));
        row.field->parentWidget()->setVisible(d.value("visible",true));row.label->setVisible(d.value("visible",true));
        if(d.contains("description"))row.field->setToolTip(parameterLabel({{"label",d.at("description")}}));
        if(auto* number=qobject_cast<QDoubleSpinBox*>(row.field)) {
            // Providers may change numeric limits after evaluating the whole
            // pending configuration. Preserve an invalid current value so the
            // provider can explain it instead of silently clamping saved data.
            const auto current=number->value();
            number->setRange(std::min(d.value("minimum",-1e12),current),std::max(d.value("maximum",1e12),current));
            number->setSingleStep(d.value("step",1.));
        }
        if(auto* combo=qobject_cast<QComboBox*>(row.field)) {
            const auto selected=combo->currentData().toString();combo->clear();
            for(const auto& option:d.at("choices"))combo->addItem(choiceLabel(d,option),QString::fromStdString(option.dump()));
            auto index=combo->findData(selected);
            if(index<0){combo->addItem(selected,selected);index=combo->count()-1;}
            combo->setCurrentIndex(index);
        }
    }
}
std::vector<std::pair<std::string,QString>> ParameterEditor::problems() const {
    const auto settings=values();std::vector<std::pair<std::string,QString>> result;
    for(const auto& d:descriptors_) {
        const auto key=d.at("key").get<std::string>();if(!settings.contains(key))continue;
        const auto& value=settings.at(key);bool valid=true;
        if(d.contains("choices"))valid=std::find(d.at("choices").begin(),d.at("choices").end(),value)!=d.at("choices").end();
        if(value.is_number())valid=valid && value.get<double>()>=d.value("minimum",-1e300) && value.get<double>()<=d.value("maximum",1e300);
        if(!d.value("enabled",true) && !d.value("inactive_allowed",false))valid=valid && value==d.value("default",Json());
        if(!valid)result.emplace_back(key,QCoreApplication::translate("PluginParameters","Invalid or conflicting value for %1.").arg(parameterLabel(d)));
    }
    return result;
}
void ParameterEditor::showIssues(const std::vector<std::pair<std::string,QString>>& issues) {
    for(const auto& row:rows_) {
        QStringList text;for(const auto& [key,message]:issues)if(key==row.key || key=="settings."+row.key)text<<message;
        row.issue->setText(text.join(u'\n'));row.issue->setVisible(!text.empty());
    }
}
}
