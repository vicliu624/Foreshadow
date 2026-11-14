#include "research_provider.hpp"
#include "plugins/desktop/common/parameter_editor.hpp"
#include "builtin/lora_study.hpp"
#include "builtin/iq_overview.hpp"
#include "builtin/fm_research.hpp"
#include "signalscope/desktop/decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <mutex>

namespace signalscope::desktop {
namespace {
ResearchControls fmControls(QWidget* parent) {
    auto* widget=new QWidget(parent);auto* layout=new QHBoxLayout(widget);layout->setContentsMargins(0,0,0,0);
    layout->addWidget(new QLabel(QCoreApplication::translate("AnalysisPanel","FM center (MHz)")));
    auto* center=new QLineEdit;center->setPlaceholderText(QCoreApplication::translate("AnalysisPanel","Use recording center"));
    center->setValidator(new QRegularExpressionValidator(decimalInputPattern(parent->locale(),7,6),center));layout->addWidget(center);
    layout->addWidget(new QLabel(QCoreApplication::translate("AnalysisPanel","De-emphasis")));
    auto* deemphasis=new QComboBox;deemphasis->addItem(QStringLiteral("50 µs"),50);deemphasis->addItem(QStringLiteral("75 µs"),75);
    deemphasis->addItem(QCoreApplication::translate("AnalysisPanel","Off"),0);layout->addWidget(deemphasis);
    layout->addWidget(new QLabel(QCoreApplication::translate("AnalysisPanel","Normalization deviation")));
    auto* deviation=new QComboBox;deviation->addItem(QStringLiteral("75 kHz"),75000);deviation->addItem(QStringLiteral("50 kHz"),50000);layout->addWidget(deviation);layout->addStretch();
    return {widget,[center,deemphasis,deviation] {
        Json values={{"deemphasis_us",deemphasis->currentData().toDouble()},{"deviation_hz",deviation->currentData().toDouble()}};
        if(!center->text().isEmpty()) {
            bool valid=false;const auto frequency=canonicalDecimalInput(center->text(),center->locale()).toDouble(&valid)*1e6;
            if(!center->hasAcceptableInput() || !valid || !std::isfinite(frequency) || frequency<=0 || frequency>1e12)
                throw std::invalid_argument(QCoreApplication::translate("AnalysisPanel","Enter a valid FM center frequency.").toStdString());
            values["center_frequency_hz"]=frequency;
        }
        return values;
    },[center](const Json& context){if(context.contains("center_frequency_hz"))center->setText(displayDecimal(context.at("center_frequency_hz").get<double>()/1e6,6,center->locale()));}};
}
std::vector<ResearchProvider> builtins() {
    return {
        {{"lora-structure",QCoreApplication::translate("AnalysisPanel","LoRa structure")},
            [](const QString& path,double threshold,std::stop_token stop,const ResearchOptions& options){return ResearchResult{renderLoRaStudy(path,threshold,stop,options.range,options.parameters)};},true,loRaStudyControls,{}},
        {{"iq-overview",QCoreApplication::translate("AnalysisPanel","I/Q overview")},
            [](const QString& path,double,std::stop_token stop,const ResearchOptions& options){return ResearchResult{renderIqOverview(path,stop,options.range)};},true,{},{}},
        {{"fm-audio",QCoreApplication::translate("AnalysisPanel","FM audio from recording")},
            [](const QString& path,double,std::stop_token stop,const ResearchOptions& options){
                FmResearchOptions fm;if(options.parameters.contains("center_frequency_hz"))fm.frequency=options.parameters.at("center_frequency_hz").get<double>();
                fm.deemphasisUs=options.parameters.value("deemphasis_us",50.);fm.deviationHz=options.parameters.value("deviation_hz",75000.);
                return ResearchResult{renderFmResearch(path,stop,options.range,fm)};
            },true,fmControls,QCoreApplication::translate("AnalysisPanel","Demodulate")},
    };
}
std::mutex providerMutex;
std::vector<ResearchProvider>& registry(){static auto value=builtins();return value;}
}
std::vector<ResearchProvider> researchProviders(const QList<ExternalResearchPlugin>& external) {
    std::lock_guard lock(providerMutex);auto result=registry();
    for(const auto& plugin:external)if(!plugin.input) {
        ResearchProvider provider{plugin.plugin,[plugin](const QString& path,double,std::stop_token stop,const ResearchOptions& options){return plugin.render(path,stop,options.range,options.parameters);},plugin.supportsRange,{},{}};
        if(!plugin.parameterDescriptions.empty())provider.controls=[descriptors=plugin.parameterDescriptions](QWidget* parent) {
            auto* editor=new ParameterEditor(parent,"research-plugin-");editor->populate(descriptors,Json::object());
            editor->changed=[editor,descriptors]{editor->updateDescriptors(descriptors);editor->showIssues(editor->problems());};
            return ResearchControls{editor,[editor] {
                const auto problems=editor->problems();editor->showIssues(problems);
                if(!problems.empty())throw std::invalid_argument(problems.front().second.toStdString());return editor->values();
            },{}};
        };
        result.push_back(std::move(provider));
    }
    return result;
}
void registerResearchProvider(ResearchProvider provider) {
    if(provider.plugin.id.isEmpty() || !provider.render)throw std::invalid_argument("Incomplete research provider");
    std::lock_guard lock(providerMutex);
    for(const auto& existing:registry())if(existing.plugin.id==provider.plugin.id)throw std::invalid_argument("Duplicate research provider");
    registry().push_back(std::move(provider));
}
}
