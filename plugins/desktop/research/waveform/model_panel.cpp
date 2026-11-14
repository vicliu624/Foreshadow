#include "model_panel.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QFormLayout>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <charconv>

namespace signalscope::desktop {
namespace {
std::uint64_t index(const Json& value) {
    const auto text=value.get<std::string>();std::uint64_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid model sample index");return result;
}
void localize(Json& value,const QString& language) {
    if(value.is_object())for(auto& [key,item]:value.items()) {
        if((key=="label" || key=="description" || key=="message") && item.is_object())item=localizedText(item,language).toStdString();else localize(item,language);
    }else if(value.is_array())for(auto& item:value)localize(item,language);
}
Json fitJson(const ReferenceFitOptions& value) {
    return {{"complex_gain",value.fit_complex_gain},{"dc_offset",value.fit_dc_offset},{"time_offset_seconds",value.time_offset_seconds},
        {"minimum_delay_samples",value.minimum_delay_samples},{"maximum_delay_samples",value.maximum_delay_samples},
        {"maximum_delay_candidates",value.maximum_delay_candidates},{"minimum_observations",value.minimum_observations},{"delay_tie_power_fraction",value.delay_tie_power_fraction},
        {"maximum_duration_seconds",value.maximum_duration_seconds},{"maximum_model_evaluations",value.maximum_model_evaluations},{"maximum_span_visits",value.maximum_span_visits},
        {"result_payload_bytes",value.result_payload_bytes},{"workspace_payload_bytes",value.workspace_payload_bytes}};
}
ReferenceFitOptions readFit(const Json& json) {
    ReferenceFitOptions value,limit;
    const auto integer=[&](const char* key,std::int64_t low,std::uint64_t high)->std::int64_t {
        const auto& v=json.at(key);if(!v.is_number_integer() || v<low || v>high)throw std::invalid_argument("Invalid model fit limit");return v.get<std::int64_t>();
    };
    const auto number=[&](const char* key,double low,double high){const auto v=json.at(key).get<double>();if(!std::isfinite(v) || v<low || v>high)throw std::invalid_argument("Invalid model fit parameter");return v;};
    value.fit_complex_gain=json.at("complex_gain").get<bool>();value.fit_dc_offset=json.at("dc_offset").get<bool>();
    value.time_offset_seconds=number("time_offset_seconds",-600,600);
    // Signed bounds are compared as JSON numbers, before any narrowing cast.
    const auto delay=[&](const char* key){const auto& v=json.at(key);if(!v.is_number_integer() || v<INT32_MIN || v>INT32_MAX)throw std::invalid_argument("Invalid model delay");return v.get<std::int32_t>();};
    value.minimum_delay_samples=delay("minimum_delay_samples");value.maximum_delay_samples=delay("maximum_delay_samples");
    value.maximum_delay_candidates=static_cast<unsigned>(integer("maximum_delay_candidates",1,limit.maximum_delay_candidates));
    if(std::int64_t(value.maximum_delay_samples)-value.minimum_delay_samples+1<1 || std::int64_t(value.maximum_delay_samples)-value.minimum_delay_samples+1>value.maximum_delay_candidates)throw std::invalid_argument("Invalid model delay search");
    value.minimum_observations=static_cast<unsigned>(integer("minimum_observations",1,UINT32_MAX));
    value.delay_tie_power_fraction=number("delay_tie_power_fraction",0,1e-3);
    value.maximum_duration_seconds=number("maximum_duration_seconds",1e-9,limit.maximum_duration_seconds);
    value.maximum_model_evaluations=static_cast<std::uint64_t>(integer("maximum_model_evaluations",1,limit.maximum_model_evaluations));
    value.maximum_span_visits=static_cast<std::uint64_t>(integer("maximum_span_visits",1,limit.maximum_span_visits));
    value.result_payload_bytes=static_cast<std::size_t>(integer("result_payload_bytes",1,limit.result_payload_bytes));
    value.workspace_payload_bytes=static_cast<std::size_t>(integer("workspace_payload_bytes",1,limit.workspace_payload_bytes));return value;
}
}
WaveformModelPanel::WaveformModelPanel(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("waveform-laboratory")) {
    setObjectName("waveform-model");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    const auto label=[&](const QString& value,const char* name){auto* item=new QLabel(value);item->setTextFormat(Qt::PlainText);item->setWordWrap(true);item->setObjectName(name);return item;};
    scope_=label(text("model.empty"),"waveform-model-scope");layout->addWidget(scope_);
    pages_=new QTabWidget;pages_->setObjectName("waveform-model-pages");auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body=new QWidget;auto* plots=new QVBoxLayout(body);plots->setContentsMargins(0,0,0,0);const std::array<const char*,3> titles{"model.plot_i","model.plot_q","model.plot_residual"};
    for(std::size_t i=0;i<views_.size();++i){plots->addWidget(label(text(titles[i]),""));views_[i]=new TraceView;views_[i]->setObjectName("waveform-model-track-"+QString::number(i));views_[i]->setAccessibleName(text(titles[i]));views_[i]->setMinimumHeight(100);plots->addWidget(views_[i],1);
        views_[i]->rangeChanged=[this](auto range){changeRange(range);};views_[i]->cursorChanged=[this](auto sample,bool a){if(a)a_=sample;else b_=sample;refreshCursors();if(result_ && sourceCursorChanged){QScopedValueRollback guard(synchronizing_,true);sourceCursorChanged(result_->analysis->source_position(sample).sample,a);}};}
    scroll->setWidget(body);pages_->addTab(scroll,text("model.traces"));plane_=new TracePlaneView;plane_->setObjectName("waveform-model-residual-plane");pages_->addTab(plane_,text("model.residual_plane"));layout->addWidget(pages_,1);
    cursor_=label(text("unavailable"),"waveform-model-cursor");layout->addWidget(cursor_);
    controls_=new QWidget(this);auto* controls=new QVBoxLayout(controls_);enabled_=new QCheckBox(text("model.enabled"));enabled_->setObjectName("waveform-model-enabled");controls->addWidget(enabled_);
    auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    input_=new QComboBox;input_->setObjectName("waveform-model-input");input_->addItem(text("channel.raw_tab"),false);input_->addItem(text("channel.tab"),true);form->addRow(text("model.input"),input_);
    provider_=new QComboBox;provider_->setObjectName("waveform-model-provider");for(const auto& provider:reference_model_providers()) {
        Json name={{"label_key",provider.label_key}};try{TextCatalog::resource(provider.resource_namespace).resolve(name);}catch(...){}provider_->addItem(localizedText(name.value("label",Json()),language_,QString::fromStdString(provider.id)),QString::fromStdString(provider.id));
    }form->addRow(text("model.provider"),provider_);controls->addLayout(form);
    about_=label({},"waveform-model-about");controls->addWidget(about_);parameters_=new ParameterEditor(nullptr,"waveform-model-param-");controls->addWidget(parameters_);
    auto* fitting=new QFormLayout;fitting->setRowWrapPolicy(QFormLayout::WrapLongRows);fitting->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    gain_=new QCheckBox(text("model.fit_gain"));gain_->setObjectName("waveform-model-fit-gain");gain_->setChecked(true);fitting->addRow(gain_);
    dc_=new QCheckBox(text("model.fit_dc"));dc_->setObjectName("waveform-model-fit-dc");fitting->addRow(dc_);
    offset_=new QDoubleSpinBox;offset_->setDecimals(9);offset_->setRange(-600000,600000);offset_->setObjectName("waveform-model-time-offset");fitting->addRow(text("model.time_offset_ms"),offset_);
    delay_min_=new QSpinBox;delay_max_=new QSpinBox;for(auto* item:{delay_min_,delay_max_})item->setRange(INT32_MIN,INT32_MAX);delay_min_->setObjectName("waveform-model-delay-min");delay_max_->setObjectName("waveform-model-delay-max");fitting->addRow(text("model.delay_min"),delay_min_);fitting->addRow(text("model.delay_max"),delay_max_);controls->addLayout(fitting);
    controls->addWidget(label(text("model.alignment_help"),"waveform-model-alignment-help"));
    apply_=new QPushButton(text("model.apply"));apply_->setObjectName("waveform-model-apply");connect(apply_,&QPushButton::clicked,this,[this]{if(apply)apply();});controls->addWidget(apply_);
    issues_=label({},"waveform-model-issues");controls->addWidget(issues_);fit_=label(text("unavailable"),"waveform-model-fit");controls->addWidget(fit_);controls->addWidget(label(text("model.help"),"waveform-model-help"));controls->addStretch();
    connect(provider_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{populate();});connect(input_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshDescriptors();});
    connect(offset_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refreshDescriptors();});for(auto* item:{delay_min_,delay_max_})connect(item,qOverload<int>(&QSpinBox::valueChanged),this,[this]{refreshDescriptors();});
    parameters_->changed=[this]{refreshDescriptors();};populate();setAvailable(false);
}
WaveformModelPanel::~WaveformModelPanel(){delete controls_.data();}
QString WaveformModelPanel::text(const std::string& key,const Json& args) const {Json item={{"label_key",key},{"arguments",args}};catalog_.resolve(item);return localizedText(item.at("label"),language_,QString::fromStdString(key));}
void WaveformModelPanel::setAvailable(bool available){apply_->setEnabled(available);}
void WaveformModelPanel::setInputs(std::shared_ptr<const WaveformAnalysis> raw,std::shared_ptr<const WaveformAnalysis> processed){raw_=std::move(raw);processed_=std::move(processed);raw_range_=raw_?SignalSampleRange{raw_->first_sample(),raw_->end_sample()-raw_->first_sample()}:SignalSampleRange{};processed_range_=processed_?SignalSampleRange{processed_->first_sample(),processed_->end_sample()-processed_->first_sample()}:SignalSampleRange{};refreshDescriptors();}
void WaveformModelPanel::setSelection(SignalSampleRange raw,SignalSampleRange processed){raw_range_=raw;processed_range_=processed;refreshDescriptors();}
ReferenceFitOptions WaveformModelPanel::fitOptions() const {ReferenceFitOptions value;value.fit_complex_gain=gain_->isChecked();value.fit_dc_offset=dc_->isChecked();value.time_offset_seconds=offset_->value()/1000;value.minimum_delay_samples=delay_min_->value();value.maximum_delay_samples=delay_max_->value();return value;}
ReferenceModelContext WaveformModelPanel::context() const {
    const auto analysis=input_->currentData().toBool()?processed_:raw_;const auto range=input_->currentData().toBool()?processed_range_:raw_range_;const auto fit=fitOptions();const auto rate=analysis?analysis->sample_rate():1;
    return {rate,range,fit.time_offset_seconds+double(fit.minimum_delay_samples)/rate,double(range.count?range.count-1:0)/rate+fit.time_offset_seconds+double(fit.maximum_delay_samples)/rate,fit.workspace_payload_bytes};
}
Json WaveformModelPanel::descriptors(const ReferenceModelProvider& provider,const Json& pending) const {auto value=reference_model_parameters(provider,context(),pending);try{TextCatalog::resource(provider.resource_namespace).resolve(value);}catch(...){}localize(value,language_);return value;}
void WaveformModelPanel::populate(const Json& values) {
    if(refreshing_ || provider_->currentIndex()<0)return;QScopedValueRollback guard(refreshing_,true);
    try{const auto provider=reference_model_provider(provider_->currentData().toString().toStdString());parameters_->populate(descriptors(provider,values),values);Json description={{"label_key",provider.description_key}};try{TextCatalog::resource(provider.resource_namespace).resolve(description);}catch(...){}about_->setText(provider.description_key.empty()?QString():localizedText(description.value("label",Json()),language_,QString::fromStdString(provider.description_key)));issues_->clear();}
    catch(...){issues_->setText(text("model.error.configuration"));}
}
void WaveformModelPanel::refreshDescriptors() {
    if(refreshing_ || provider_->currentIndex()<0)return;QScopedValueRollback guard(refreshing_,true);
    try{const auto provider=reference_model_provider(provider_->currentData().toString().toStdString());parameters_->updateDescriptors(descriptors(provider,parameters_->values()));parameters_->showIssues(parameters_->problems());issues_->clear();}catch(...){issues_->setText(text("model.error.configuration"));}
}
std::optional<WaveformModelRequest> WaveformModelPanel::draft() {
    if(!enabled_->isChecked())return {};refreshDescriptors();const auto problems=parameters_->problems();parameters_->showIssues(problems);if(!problems.empty())throw std::invalid_argument("Invalid model fields");
    const auto provider=reference_model_provider(provider_->currentData().toString().toStdString());WaveformModelRequest request;request.provider_id=provider.id;request.method_version=provider.method_version;request.parameters=parameters_->values();request.fit=fitOptions();request.processed=input_->currentData().toBool();request.range=request.processed?processed_range_:raw_range_;return request;
}
std::optional<WaveformModelRequest> WaveformModelPanel::readConfiguration(const Json& config) {
    if(config.is_null())return {};if(config.at("version")!=1)throw std::invalid_argument("Unsupported model configuration");if(!config.at("enabled").get<bool>())return {};
    WaveformModelRequest request;request.provider_id=config.at("provider_id").get<std::string>();(void)reference_model_provider(request.provider_id);
    const auto& version=config.at("method_version");if(!version.is_number_integer() || version<1 || version>UINT32_MAX)throw std::invalid_argument("Invalid model method version");request.method_version=version.get<unsigned>();
    request.parameters=config.at("parameters");if(!request.parameters.is_object())throw std::invalid_argument("Invalid model parameters");request.processed=config.at("processed").get<bool>();request.fit=readFit(config.at("fit"));
    const auto& range=config.at("range");request.range=SignalSampleRange{index(range.at("first")),index(range.at("count"))};if(!request.range->count || request.range->first>UINT64_MAX-request.range->count)throw std::invalid_argument("Invalid model range");
    const auto& clock=config.at("input_clock");const auto rate=[&](const char* key){const auto& value=clock.at(key);if(!value.is_number_integer() || value<1 || value>UINT32_MAX)throw std::invalid_argument("Invalid model clock");return value.get<std::uint32_t>();};
    request.expected_clock=SignalSampleClock{index(clock.at("source_origin")),rate("source_rate_hz"),rate("rate_hz")};return request;
}
void WaveformModelPanel::restoreDraft(const Json& config) {
    const auto request=readConfiguration(config);if(!request){enabled_->setChecked(false);return;}
    const QSignalBlocker block(provider_);const auto selected=provider_->findData(QString::fromStdString(request->provider_id));if(selected<0)throw std::invalid_argument("Unavailable model provider");provider_->setCurrentIndex(selected);
    input_->setCurrentIndex(request->processed?1:0);gain_->setChecked(request->fit.fit_complex_gain);dc_->setChecked(request->fit.fit_dc_offset);offset_->setValue(request->fit.time_offset_seconds*1000);
    if(std::abs(offset_->value()/1000-request->fit.time_offset_seconds)>1e-15)throw std::invalid_argument("Unsupported saved alignment precision");delay_min_->setValue(request->fit.minimum_delay_samples);delay_max_->setValue(request->fit.maximum_delay_samples);populate(request->parameters);
    if(parameters_->values()!=request->parameters)throw std::invalid_argument("Model parameters could not be represented by the installed editor");enabled_->setChecked(true);
}
QString WaveformModelPanel::failureText(std::exception_ptr failure,const std::string& fallback) {
    auto message=text(fallback);try{if(failure)std::rethrow_exception(failure);}catch(const ReferenceModelError& error){
        auto problems=error.issues;try{TextCatalog::resource(error.resource_namespace).resolve(problems);localize(problems,language_);std::vector<std::pair<std::string,QString>> fields;QStringList messages;
            for(const auto& problem:problems){const auto detail=localizedText(problem.value("message",problem.value("label",Json())),language_,text("model.error.configuration"));fields.emplace_back(problem.value("field",std::string()),detail);messages.push_back(detail);}parameters_->showIssues(fields);if(!messages.empty())message=messages.join(u'\n');}catch(...){}
    }catch(...){}issues_->setText(message);return message;
}
void WaveformModelPanel::setResult(std::shared_ptr<const ReferenceComparison> result,std::optional<WaveformModelRequest> request) {
    if(result && (!request || !request->expected_clock || request->provider_id!=result->provider_id || request->method_version!=result->method_version || !request->range || *request->range!=result->range))throw std::invalid_argument("A model result requires its actual applied request and clock");
    if(result_==result)return;result_=std::move(result);applied_=std::move(request);issues_->clear();
    if(!result_){applied_.reset();scope_->setText(text("model.empty"));fit_->setText(text("unavailable"));cursor_->setText(text("unavailable"));for(auto* view:views_)view->setAnalysis({},{});plane_->setAnalysis({},{},{});return;}
    if(!applied_)throw std::invalid_argument("A model result requires its applied request");const auto& analysis=result_->analysis;
    const auto dark=palette().base().color().lightness()<128;const QColor observed=dark?QColor("#65d9c5"):QColor("#087f78"),reference=dark?QColor("#efb766"):QColor("#a86b13"),other=dark?QColor("#b8a0ee"):QColor("#7854b8");
    const auto modelTrace=[&](const char* id){return std::shared_ptr<const AnalysisTrace>(result_,&result_->trace(id));};
    for(std::size_t i=0;i<2;++i){const auto id=i==0?"i":"q";views_[i]->setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace(id)),observed,text("model.observed"),{},false},{modelTrace(i==0?"reference_i":"reference_q"),reference,text("model.reference"),{},true}});}
    views_[2]->setAnalysis(analysis,{{modelTrace("residual_i"),observed,text("model.residual_i"),{},false},{modelTrace("residual_q"),other,text("model.residual_q"),{},true}});plane_->setAnalysis(analysis,modelTrace("residual_i"),modelTrace("residual_q"));
    const auto provider=reference_model_provider(result_->provider_id);Json name={{"label_key",provider.label_key}};try{TextCatalog::resource(provider.resource_namespace).resolve(name);}catch(...){}
    scope_->setText(text("model.scope",{{"model",localizedText(name.value("label",Json()),language_,QString::fromStdString(provider.id)).toStdString()},{"source",text(applied_->processed?"channel.tab":"channel.raw_tab").toStdString()},{"first",std::to_string(result_->range.first)},{"count",std::to_string(result_->range.count)},{"observed",result_->observations}}));
    const auto number=[this](double value){return displayMeasurement(value,locale()).toStdString();};
    fit_->setText(text("model.fit_result",{{"rms",number(result_->residual_rms)},{"normalized",result_->normalized_rms_error?number(*result_->normalized_rms_error):text("unavailable").toStdString()},
        {"gain_i",number(result_->gain.real())},{"gain_q",number(result_->gain.imag())},{"dc_i",number(result_->dc_offset.real())},{"dc_q",number(result_->dc_offset.imag())},{"delay",result_->delay_samples},{"ties",result_->tied_delays.size()},
        {"timing",text(result_->options.minimum_delay_samples==result_->options.maximum_delay_samples?"model.timing_not_searched":result_->tied_delays.size()>1?"model.timing_ambiguous":"model.timing_bounded").toStdString()}}));
    a_=result_->range.first;b_=result_->range.first;{QScopedValueRollback guard(synchronizing_,true);for(auto* view:views_)view->setRange(result_->range);plane_->setRange(result_->range);}refreshCursors();
}
void WaveformModelPanel::changeRange(SignalSampleRange range) {
    if(synchronizing_ || !result_ || !range.count)return;QScopedValueRollback guard(synchronizing_,true);const auto end=result_->range.first+result_->range.count;
    const auto first=std::clamp(range.first,result_->range.first,end-1);range={first,std::min(range.count,end-first)};for(auto* view:views_)view->setRange(range);plane_->setRange(range);
    if(sourceRangeChanged){const auto firstSource=result_->analysis->source_position(range.first).sample,last=result_->analysis->source_position(range.first+range.count-1).sample;sourceRangeChanged({firstSource,last-firstSource+1});}
}
void WaveformModelPanel::setSourceCursors(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b) {
    if(synchronizing_ || !result_)return;const auto convert=[&](auto value)->std::optional<std::uint64_t>{if(!value)return {};if(!result_->analysis->processed)return value;const auto& clock=result_->analysis->processed->clock;if(*value<clock.source_origin)return {};const auto at=clock.index_at_or_after(*value);return at>=result_->analysis->first_sample() && at<result_->analysis->end_sample()?std::optional<std::uint64_t>(at):std::nullopt;};a_=convert(a);b_=convert(b);refreshCursors();
}
void WaveformModelPanel::refreshCursors() {
    for(auto* view:views_)view->setCursors(a_,b_);plane_->setCursors(a_,b_);if(!result_ || !b_){cursor_->setText(text("unavailable"));return;}
    const auto position=result_->analysis->source_position(*b_);const auto at=analysis_trace_value_index(*result_->analysis,result_->trace("residual_i"),*b_);
    cursor_->setText(text("model.cursor",{{"index",std::to_string(*b_)},{"sample",std::to_string(position.sample)},{"numerator",position.numerator},{"denominator",position.denominator},{"residual_i",at?Json(displayMeasurement(result_->trace("residual_i").values[*at],locale()).toStdString()):Json(text("unavailable").toStdString())},{"residual_q",at?Json(displayMeasurement(result_->trace("residual_q").values[*at],locale()).toStdString()):Json(text("unavailable").toStdString())}}));
}
Json WaveformModelPanel::configuration() const {
    if(!result_ || !applied_)return {{"version",1},{"enabled",false}};const auto& clock=*applied_->expected_clock;const auto range=views_[0]->range();
    return {{"version",1},{"enabled",true},{"provider_id",result_->provider_id},{"method_version",result_->method_version},{"parameters",result_->parameters},{"method",result_->method},{"fit",fitJson(result_->options)},{"processed",applied_->processed},
        {"range",{{"first",std::to_string(result_->range.first)},{"count",std::to_string(result_->range.count)}}},
        {"input_clock",{{"source_origin",std::to_string(clock.source_origin)},{"source_rate_hz",clock.source_rate},{"rate_hz",clock.rate}}},
        {"view",{{"first",std::to_string(range.first)},{"count",std::to_string(range.count)},{"cursor_a",a_?Json(std::to_string(*a_)):Json()},{"cursor_b",b_?Json(std::to_string(*b_)):Json()},{"page",pages_->currentIndex()}}}};
}
void WaveformModelPanel::restoreView(const Json& config) {
    if(!result_ || !config.contains("view"))return;const auto& view=config.at("view");const SignalSampleRange range{index(view.at("first")),index(view.at("count"))};const auto end=result_->range.first+result_->range.count;
    if(!range.count || range.first<result_->range.first || range.first>=end || range.count>end-range.first || !view.at("page").is_number_integer() || view.at("page")<0 || view.at("page")>=pages_->count())throw std::invalid_argument("Invalid model view");
    const auto cursor=[&](const Json& v)->std::optional<std::uint64_t>{if(v.is_null())return {};const auto value=index(v);if(value<result_->analysis->first_sample() || value>=result_->analysis->end_sample())throw std::invalid_argument("Invalid model cursor");return value;};a_=cursor(view.at("cursor_a"));b_=cursor(view.at("cursor_b"));
    {QScopedValueRollback guard(synchronizing_,true);for(auto* item:views_)item->setRange(range);plane_->setRange(range);}pages_->setCurrentIndex(view.at("page").get<int>());refreshCursors();
}
Json WaveformModelPanel::snapshot() const {Json value={{"configuration",configuration()}};if(result_)value.update({{"method",result_->method},{"observations",result_->observations},{"result_payload_bytes",result_->payload_bytes()},{"residual_rms",result_->residual_rms},{"normalized_rms_error",result_->normalized_rms_error?Json(*result_->normalized_rms_error):Json()}});return value;}
}
