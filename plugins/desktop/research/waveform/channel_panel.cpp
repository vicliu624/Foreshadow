#include "channel_panel.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QFormLayout>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <charconv>

namespace signalscope::desktop {
namespace {
std::uint64_t index(const Json& value) {
    const auto s=value.get<std::string>();std::uint64_t result=0;
    const auto read=std::from_chars(s.data(),s.data()+s.size(),result);
    if(s.empty() || read.ec!=std::errc{} || read.ptr!=s.data()+s.size())throw std::invalid_argument("Invalid channel view index");
    return result;
}
}
WaveformChannelPanel::WaveformChannelPanel(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("waveform-laboratory")) {
    setObjectName("waveform-channel");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    controls_=new QWidget(this);auto* controlsLayout=new QVBoxLayout(controls_);
    enabled_=new QCheckBox(text("channel.enabled"));enabled_->setObjectName("waveform-channel-enabled");controlsLayout->addWidget(enabled_);
    auto* fields=new QFormLayout;fields->setRowWrapPolicy(QFormLayout::WrapLongRows);fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto spin=[&](const char* name,double low,double high,double value){auto* field=new QDoubleSpinBox;field->setObjectName(name);field->setRange(low,high);field->setDecimals(6);field->setValue(value);return field;};
    offset_=spin("waveform-channel-offset",-50000,50000,0);passband_=spin("waveform-channel-passband",.000001,100000,120);
    transition_=spin("waveform-channel-transition",.000001,50000,60);rate_=new QSpinBox;rate_->setObjectName("waveform-channel-rate");rate_->setRange(1,100000000);rate_->setValue(400000);
    fields->addRow(text("channel.offset"),offset_);fields->addRow(text("channel.passband"),passband_);
    fields->addRow(text("channel.transition"),transition_);fields->addRow(text("channel.rate"),rate_);
    controlsLayout->addLayout(fields);apply_=new QPushButton(text("channel.apply"));apply_->setObjectName("waveform-channel-apply");controlsLayout->addWidget(apply_);
    file_options_=new QWidget;auto* fileLayout=new QVBoxLayout(file_options_);fileLayout->setContentsMargins(0,0,0,0);
    file_scope_=new QCheckBox(text("channel.file_scope"));file_scope_->setObjectName("waveform-channel-file-scope");fileLayout->addWidget(file_scope_);
    auto* fileFields=new QFormLayout;fileFields->setRowWrapPolicy(QFormLayout::WrapLongRows);fileFields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    file_first_=new QLineEdit("0");file_count_=new QLineEdit("1");file_first_->setObjectName("waveform-channel-file-first");file_count_->setObjectName("waveform-channel-file-count");
    for(auto* field:{file_first_,file_count_})field->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,20}"),field));
    fileFields->addRow(text("channel.file_first"),file_first_);fileFields->addRow(text("channel.file_count"),file_count_);fileLayout->addLayout(fileFields);
    file_total_=new QLabel;file_total_->setTextFormat(Qt::PlainText);file_total_->setWordWrap(true);fileLayout->addWidget(file_total_);
    auto* useLoaded=new QPushButton(text("channel.file_use_loaded"));connect(useLoaded,&QPushButton::clicked,this,[this]{file_first_->setText(QString::number(loaded_file_range_.first));file_count_->setText(QString::number(loaded_file_range_.count));});fileLayout->addWidget(useLoaded);
    auto* fileHelp=new QLabel(text("channel.file_help"));fileHelp->setWordWrap(true);fileHelp->setTextFormat(Qt::PlainText);fileLayout->addWidget(fileHelp);file_options_->hide();controlsLayout->insertWidget(2,file_options_);
    connect(apply_,&QPushButton::clicked,this,[this]{if(apply)apply();});
    auto* help=new QLabel(text("channel.help"));help->setWordWrap(true);help->setTextFormat(Qt::PlainText);controlsLayout->addWidget(help);controlsLayout->addStretch();
    status_=new QLabel(text("channel.empty"));status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);status_->setObjectName("waveform-channel-status");layout->addWidget(status_);
    cursor_=new QLabel;cursor_->setWordWrap(true);cursor_->setTextFormat(Qt::PlainText);cursor_->setObjectName("waveform-channel-cursor");layout->addWidget(cursor_);
    reveal_=new QPushButton(text("channel.reveal_original"));reveal_->setObjectName("waveform-channel-reveal");reveal_->setEnabled(false);layout->addWidget(reveal_);
    connect(reveal_,&QPushButton::clicked,this,[this]{if(analysis_ && b_ && analysis_->value_index(*b_) && revealOriginal)revealOriginal(analysis_->source_position(*b_));});
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);auto* body=new QWidget;auto* tracks=new QVBoxLayout(body);tracks->setContentsMargins(0,0,0,0);
    const std::array<const char*,4> titles{"plot.iq","plot.amplitude","plot.phase","plot.frequency"};
    for(std::size_t i=0;i<views_.size();++i) {
        tracks->addWidget(new QLabel(text(titles[i])));views_[i]=new TraceView;views_[i]->setObjectName("waveform-channel-track-"+QString::number(i));views_[i]->setMinimumHeight(70);tracks->addWidget(views_[i]);
        views_[i]->rangeChanged=[this](auto range){changeRange(range);};
        views_[i]->cursorChanged=[this](auto sample,bool a){if(a)a_=sample;else b_=sample;refreshCursors();if(sourceCursorChanged && analysis_){QScopedValueRollback guard(synchronizing_,true);sourceCursorChanged(analysis_->source_position(sample).sample,a);}};
    }
    scroll->setWidget(body);layout->addWidget(scroll,1);setAvailable(false);
}
WaveformChannelPanel::~WaveformChannelPanel(){delete controls_.data();}
QString WaveformChannelPanel::text(const std::string& key,const Json& arguments) const {
    Json value={{"label_key",key},{"arguments",arguments}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));
}
std::optional<ChannelExtractionOptions> WaveformChannelPanel::draft() const {
    if(!enabled_->isChecked())return {};
    return ChannelExtractionOptions{offset_->value()*1000,passband_->value()*1000,transition_->value()*1000,static_cast<unsigned>(rate_->value())};
}
void WaveformChannelPanel::restoreDraft(const Json& config) {
    file_scope_->setChecked(false);
    if(config.is_null()){enabled_->setChecked(false);return;}
    if(config.at("version")!=1 && config.at("version")!=2)throw std::invalid_argument("Unsupported channel configuration");
    const auto enabled=config.at("enabled").get<bool>();if(!enabled){enabled_->setChecked(false);return;}
    const auto restore=[](QDoubleSpinBox* field,const Json& value){const auto number=value.get<double>();if(!std::isfinite(number))throw std::invalid_argument("Non-finite channel parameter");field->setValue(number/1000);if(std::abs(field->value()*1000-number)>1e-6)throw std::invalid_argument("Unsupported channel parameter precision");};
    restore(offset_,config.at("offset_hz"));restore(passband_,config.at("passband_hz"));restore(transition_,config.at("transition_hz"));
    const auto& rateValue=config.at("output_rate_hz");if(!rateValue.is_number_integer() || rateValue<1 || rateValue>100000000)throw std::invalid_argument("Invalid channel rate");
    rate_->setValue(rateValue.get<int>());enabled_->setChecked(true);
    if(config.at("version")==2) {
        const auto& input=config.at("input");if(input.at("kind")!="file-range")throw std::invalid_argument("Unsupported channel source scope");
        const auto first=index(input.at("first_file_sample")),count=index(input.at("sample_count"));if(!count || first>UINT64_MAX-count)throw std::invalid_argument("Invalid channel file range");
        file_first_->setText(QString::number(first));file_count_->setText(QString::number(count));file_scope_->setChecked(true);
    }
}
void WaveformChannelPanel::setAvailable(bool enabled){available_=enabled;apply_->setEnabled(enabled);reveal_->setEnabled(enabled && analysis_ && b_ && analysis_->value_index(*b_).has_value());}
void WaveformChannelPanel::selectBand(double offset,double passband) {
    if(!std::isfinite(offset) || !std::isfinite(passband) || passband<=0)throw std::invalid_argument("Invalid selected frequency band");
    const auto transition=std::max(1.,passband*.1);offset_->setValue(offset/1000);passband_->setValue(passband/1000);transition_->setValue(transition/1000);
    rate_->setValue(static_cast<int>(std::clamp(std::ceil((passband+2*transition)*1.25),1.,100000000.)));enabled_->setChecked(true);
}
std::optional<SignalSampleRange> WaveformChannelPanel::draftFileRange() const {
    if(!enabled_->isChecked() || !file_scope_->isChecked())return {};
    if(file_source_path_.empty() || !file_first_->hasAcceptableInput() || !file_count_->hasAcceptableInput())throw std::invalid_argument("Recorded channel source or range is unavailable");
    const auto first=index(Json(file_first_->text().toStdString())),count=index(Json(file_count_->text().toStdString()));
    if(!count || first>UINT64_MAX-count)throw std::invalid_argument("Invalid channel file interval");
    return SignalSampleRange{first,count};
}
void WaveformChannelPanel::resetFileScope(){file_scope_->setChecked(false);}
void WaveformChannelPanel::setRecordedSource(std::shared_ptr<const SignalSourceInfo> source,SignalSampleRange loaded,bool preserve) {
    file_options_->setVisible(bool(source));loaded_file_range_=loaded;
    if(!source){file_source_path_.clear();return;}
    if(!preserve && file_source_path_!=source->metadata_path){file_scope_->setChecked(false);file_first_->setText(QString::number(loaded.first));file_count_->setText(QString::number(loaded.count));}
    file_source_path_=source->metadata_path;file_total_->setText(text("channel.file_total",{{"count",std::to_string(source->storage.pairs)}}));
}
void WaveformChannelPanel::setResult(std::shared_ptr<const WaveformAnalysis> result,std::optional<ChannelExtractionOptions> applied,bool preserve) {
    const auto previous=views_[0]->range();const auto old=analysis_;const auto old_a=a_,old_b=b_;analysis_=std::move(result);applied_=std::move(applied);QScopedValueRollback guard(synchronizing_,true);
    if(!analysis_){for(auto* view:views_)view->setAnalysis({},{});a_.reset();b_.reset();cursor_->clear();reveal_->setEnabled(false);status_->setText(text("channel.empty"));return;}
    if(!analysis_->processed || !analysis_->sample_count())throw std::invalid_argument("Missing processed channel result");
    const auto primary=palette().base().color().lightness()<128?QColor("#65d9c5"):QColor("#087f78");const QColor secondary("#b78638");
    const auto series=[&](const char* id,bool dashed=false,std::optional<double> wrap={}){const auto& trace=analysis_->trace(id);return TraceViewSeries{std::shared_ptr<const AnalysisTrace>(analysis_,&trace),dashed?secondary:primary,text(trace.label_key),wrap,dashed};};
    views_[0]->setAnalysis(analysis_,{series("i"),series("q",true)});views_[1]->setAnalysis(analysis_,{series("amplitude")});
    views_[2]->setAnalysis(analysis_,{series("unwrapped_phase")});views_[3]->setAnalysis(analysis_,{series("frequency"),series("smoothed_frequency",true)});
    const bool same_clock=old && old->processed->clock==analysis_->processed->clock;
    if(preserve && old && previous.count && same_clock)for(auto* view:views_)view->setRange(previous);
    else if(preserve && old && previous.count) {
        const auto first=old->source_position(previous.first),last=old->source_position(previous.first+previous.count-1);
        const auto new_first=analysis_->source_position(analysis_->first_sample()).sample,new_end=analysis_->source_position(analysis_->end_sample()-1).sample+1;
        if(first.sample<new_end && last.sample+1>new_first) {
            const auto& clock=analysis_->processed->clock;const auto begin=clock.index_at_or_after(first.sample<clock.source_origin?SourceSamplePosition{clock.source_origin,0,1}:first);
            const auto end=clock.index_at_or_after(std::min(last.sample+1,new_end));
            for(auto* view:views_)view->setRange({begin,std::max<std::uint64_t>(1,end>begin?end-begin:1)});
        }
    }
    const auto remap=[&](auto cursor)->std::optional<std::uint64_t>{
        if(!cursor || !old)return {};const auto position=old->source_position(*cursor);if(position.sample<analysis_->processed->clock.source_origin)return {};
        const auto index=same_clock?*cursor:analysis_->processed->clock.index_at_or_after(position);return index>=analysis_->first_sample() && index<analysis_->end_sample()?std::optional<std::uint64_t>(index):std::nullopt;
    };
    if(preserve && old){a_=remap(old_a);b_=remap(old_b);}else {a_=analysis_->first_sample();b_=analysis_->first_sample();}refreshCursors();
    refreshStatus();
}
void WaveformChannelPanel::refreshStatus() {
    if(!analysis_)return;const auto& signal=*analysis_->processed;
    status_->setText(text(signal.recording?"channel.file_ready":"channel.ready",{{"count",analysis_->sample_count()},{"rate",analysis_->sample_rate()},
        {"center",displayDecimal(signal.center_frequency_hz/1e6,6,locale()).toStdString()},
        {"delay",signal.method.at("causal_group_delay_source_samples")},{"first",std::to_string(signal.file_range.first)},{"input_count",std::to_string(signal.file_range.count)}}));
}
void WaveformChannelPanel::setSourceRange(SignalSampleRange range) {
    if(synchronizing_ || !analysis_ || !range.count)return;QScopedValueRollback guard(synchronizing_,true);
    const auto& clock=analysis_->processed->clock;
    if(range.first>UINT64_MAX-range.count)throw std::invalid_argument("Source selection overflows its clock");
    const auto source_first=clock.source_position(analysis_->first_sample()).sample,source_end=clock.source_position(analysis_->end_sample()-1).sample+1;
    if(range.first+range.count<=source_first || range.first>=source_end){status_->setText(text("channel.selection_outside"));return;}
    const auto first=clock.index_at_or_after(std::max(source_first,range.first));
    const auto end=clock.index_at_or_after(std::min(source_end,range.first+range.count));
    for(auto* view:views_)view->setRange({first,std::max<std::uint64_t>(1,end>first?end-first:1)});
    refreshStatus();
}
void WaveformChannelPanel::selectAnalysisRange(SignalSampleRange range) {
    if(!analysis_ || !range.count || range.first<analysis_->first_sample() || range.first>=analysis_->end_sample() || range.count>analysis_->end_sample()-range.first)throw std::invalid_argument("Invalid processed evidence selection");
    changeRange(range);a_=range.first;b_=range.first+range.count-1;refreshCursors();
}
void WaveformChannelPanel::changeRange(SignalSampleRange range) {
    if(synchronizing_ || !analysis_)return;QScopedValueRollback guard(synchronizing_,true);
    for(auto* view:views_)view->setRange(range);
    const auto first=analysis_->source_position(range.first).sample,last=analysis_->source_position(range.first+range.count-1).sample;
    if(sourceRangeChanged)sourceRangeChanged({first,last-first+1});
}
void WaveformChannelPanel::setSourceCursors(std::optional<std::uint64_t> a,std::optional<std::uint64_t> b) {
    if(synchronizing_ || !analysis_)return;
    const auto convert=[&](auto position)->std::optional<std::uint64_t>{if(!position || *position<analysis_->processed->clock.source_origin)return {};const auto at=analysis_->processed->clock.index_at_or_after(*position);return at>=analysis_->first_sample() && at<analysis_->end_sample()?std::optional<std::uint64_t>(at):std::nullopt;};
    a_=convert(a);b_=convert(b);refreshCursors();
}
void WaveformChannelPanel::refreshCursors() {
    for(auto* view:views_)view->setCursors(a_,b_);reveal_->setEnabled(available_ && analysis_ && b_ && analysis_->value_index(*b_).has_value());if(!analysis_ || !b_){cursor_->setText(text("unavailable"));return;}
    const auto position=analysis_->source_position(*b_);cursor_->setText(text("channel.cursor",{{"index",std::to_string(*b_)},{"sample",std::to_string(position.sample)},
        {"numerator",position.numerator},{"denominator",position.denominator},{"available",analysis_->value_index(*b_)?text("channel.observed").toStdString():text("unavailable").toStdString()}}));
}
Json WaveformChannelPanel::configuration() const {
    if(!applied_ || !analysis_)return {{"version",1},{"enabled",false}};
    const auto& value=*applied_;const auto range=views_[0]->range();Json result={{"version",analysis_->processed->recording?2:1},{"enabled",true},
        {"offset_hz",value.offset_hz},{"passband_hz",value.passband_hz},{"transition_hz",value.transition_hz},{"output_rate_hz",value.output_rate},
        {"view",{{"first",std::to_string(range.first)},{"count",std::to_string(range.count)},
            {"cursor_a",a_?Json(std::to_string(*a_)):Json()},{"cursor_b",b_?Json(std::to_string(*b_)):Json()}}}};
    if(analysis_->processed->recording)result["input"]={{"kind","file-range"},{"first_file_sample",std::to_string(analysis_->processed->file_range.first)},{"sample_count",std::to_string(analysis_->processed->file_range.count)}};
    return result;
}
void WaveformChannelPanel::restoreView(const Json& config) {
    if(!analysis_ || !config.contains("view"))return;QScopedValueRollback guard(synchronizing_,true);const auto& view=config.at("view");
    const SignalSampleRange range{index(view.at("first")),index(view.at("count"))};
    if(!range.count || range.first<analysis_->first_sample() || range.first>=analysis_->end_sample() || range.count>analysis_->end_sample()-range.first)throw std::invalid_argument("Invalid saved processed range");
    for(auto* item:views_)item->setRange(range);
    const auto cursor=[&](const Json& input)->std::optional<std::uint64_t>{if(input.is_null())return {};const auto value=index(input);if(value<analysis_->first_sample() || value>=analysis_->end_sample())throw std::invalid_argument("Invalid processed cursor");return value;};
    a_=cursor(view.at("cursor_a"));b_=cursor(view.at("cursor_b"));refreshCursors();
}
Json WaveformChannelPanel::snapshot() const {
    Json result={{"configuration",configuration()}};if(analysis_)result.update({{"samples",analysis_->sample_count()},{"trace_payload_bytes",analysis_->payload_bytes()},{"method",analysis_->processed->method}});return result;
}
}
