#include "window_panel.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QFormLayout>
#include <QVBoxLayout>
#include <limits>

namespace signalscope::desktop {
WaveformWindowPanel::WaveformWindowPanel(const QString& language,QWidget* parent):QWidget(parent),language_(language),catalog_(TextCatalog::resource("waveform-laboratory")) {
    setObjectName("waveform-window-research");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    scope_=new QLabel(text("window.empty"));scope_->setTextFormat(Qt::PlainText);scope_->setWordWrap(true);scope_->setObjectName("waveform-window-scope");layout->addWidget(scope_);
    pages_=new QTabWidget;pages_->setObjectName("waveform-window-pages");spectrogram_=new ResearchSpectrogramView;spectrogram_->setObjectName("waveform-stft");
    psd_=new ResearchCurveView;psd_->setObjectName("waveform-psd");correlation_=new ResearchCurveView;correlation_->setObjectName("waveform-correlation");histogram_=new ResearchCurveView;histogram_->setObjectName("waveform-frequency-histogram");
    pages_->addTab(spectrogram_,text("window.stft"));pages_->addTab(psd_,text("window.psd"));pages_->addTab(correlation_,text("window.correlation"));pages_->addTab(histogram_,text("window.histogram"));layout->addWidget(pages_,1);
    auto* help=new QLabel(text("window.view_help"));help->setWordWrap(true);help->setTextFormat(Qt::PlainText);layout->addWidget(help);
    controls_=new QWidget(this);auto* controls=new QVBoxLayout(controls_);enabled_=new QCheckBox(text("window.enabled"));enabled_->setObjectName("waveform-window-enabled");controls->addWidget(enabled_);
    auto* form=new QFormLayout;form->setRowWrapPolicy(QFormLayout::WrapLongRows);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    input_=new QComboBox;input_->setObjectName("waveform-window-input");input_->addItem(text("channel.raw_tab"),false);input_->addItem(text("channel.tab"),true);form->addRow(text("window.input"),input_);
    fft_=new QComboBox;fft_->setObjectName("waveform-window-fft");for(unsigned n=16;n<=65536;n*=2)fft_->addItem(QString::number(n),n);fft_->setCurrentIndex(fft_->findData(1024));form->addRow(text("window.fft"),fft_);
    hop_=new QSpinBox;hop_->setRange(1,524288);hop_->setValue(512);hop_->setObjectName("waveform-window-hop");form->addRow(text("window.hop"),hop_);
    window_=new QComboBox;window_->setObjectName("waveform-window-function");for(const auto key:{"window.rectangular","window.hann","window.hamming","window.blackman"})window_->addItem(text(key));window_->setCurrentIndex(1);form->addRow(text("window.function"),window_);
    scaling_=new QComboBox;scaling_->setObjectName("waveform-window-scaling");scaling_->addItem(text("window.power_bin"));scaling_->addItem(text("window.power_density"));scaling_->setCurrentIndex(1);form->addRow(text("window.scaling"),scaling_);
    lags_=new QSpinBox;lags_->setRange(0,65536);lags_->setValue(16);lags_->setObjectName("waveform-window-lags");form->addRow(text("window.lags"),lags_);
    bins_=new QSpinBox;bins_->setRange(1,4096);bins_->setValue(64);bins_->setObjectName("waveform-window-bins");form->addRow(text("window.bins"),bins_);controls->addLayout(form);
    apply_=new QPushButton(text("window.apply"));apply_->setObjectName("waveform-window-apply");controls->addWidget(apply_);connect(apply_,&QPushButton::clicked,this,[this]{if(apply)apply();});
    auto* colors=new QFormLayout;low_=new QDoubleSpinBox;high_=new QDoubleSpinBox;
    for(auto* item:{low_,high_}){item->setRange(-300,120);item->setDecimals(1);connect(item,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refreshColors();});}
    low_->setValue(-100);high_->setValue(-20);low_->setObjectName("waveform-window-color-low");high_->setObjectName("waveform-window-color-high");colors->addRow(text("window.color_low"),low_);colors->addRow(text("window.color_high"),high_);controls->addLayout(colors);
    auto* stats=new QFormLayout;stats->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const std::array<const char*,7> names{"window.observed","window.missing","window.mean_power","window.amplitude_range","window.frequency_mean","window.frequency_std","window.valid_frequencies"};
    for(std::size_t i=0;i<statistics_.size();++i){statistics_[i]=new QLabel(text("unavailable"));statistics_[i]->setTextFormat(Qt::PlainText);statistics_[i]->setWordWrap(true);statistics_[i]->setObjectName(names[i]);stats->addRow(text(names[i]),statistics_[i]);}controls->addLayout(stats);
    auto* note=new QLabel(text("window.statistics_help"));note->setWordWrap(true);note->setTextFormat(Qt::PlainText);controls->addWidget(note);controls->addStretch();
    spectrogram_->timeFrequencySelected=[this](const auto& selection){if(result_ && bandSelected)bandSelected(selection,result_->analysis);};setAvailable(false);
}
WaveformWindowPanel::~WaveformWindowPanel(){delete controls_.data();}
QString WaveformWindowPanel::text(const std::string& key,const Json& arguments) const {Json value={{"label_key",key},{"arguments",arguments}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));}
std::optional<WindowResearchOptions> WaveformWindowPanel::draft() const {
    if(!enabled_->isChecked())return {};WindowResearchOptions value;value.fft_size=fft_->currentData().toUInt();value.hop_samples=static_cast<unsigned>(hop_->value());
    value.window=static_cast<ResearchWindow>(window_->currentIndex());value.scaling=static_cast<SpectralScaling>(scaling_->currentIndex());value.correlation_lags=static_cast<unsigned>(lags_->value());value.frequency_histogram_bins=static_cast<unsigned>(bins_->value());return value;
}
bool WaveformWindowPanel::draftProcessed() const {return input_->currentData().toBool();}
void WaveformWindowPanel::restoreDraft(const Json& config) {
    if(config.is_null()){enabled_->setChecked(false);return;}
    if(config.at("version")!=1)throw std::invalid_argument("Unsupported window research parameters");
    if(!config.at("enabled").get<bool>()){enabled_->setChecked(false);return;}
    const auto integer=[](const Json& v,unsigned low,unsigned high){if(!v.is_number_integer() || v<low || v>high)throw std::invalid_argument("Invalid research parameter range");return v.get<unsigned>();};
    const auto fft=integer(config.at("fft_size"),16,65536);const auto fftIndex=fft_->findData(fft);if(fftIndex<0)throw std::invalid_argument("Unsupported FFT size");fft_->setCurrentIndex(fftIndex);
    hop_->setValue(static_cast<int>(integer(config.at("hop_samples"),1,524288)));window_->setCurrentIndex(static_cast<int>(integer(config.at("window"),0,3)));scaling_->setCurrentIndex(static_cast<int>(integer(config.at("scaling"),0,1)));
    lags_->setValue(static_cast<int>(integer(config.at("correlation_lags"),0,65536)));bins_->setValue(static_cast<int>(integer(config.at("frequency_histogram_bins"),1,4096)));
    input_->setCurrentIndex(config.at("processed").get<bool>()?1:0);enabled_->setChecked(true);
}
void WaveformWindowPanel::setAvailable(bool available){apply_->setEnabled(available);}
void WaveformWindowPanel::refreshColors(){if(low_->value()<high_->value()){applied_low_db_=low_->value();applied_high_db_=high_->value();spectrogram_->setColorRange(applied_low_db_,applied_high_db_);}}
void WaveformWindowPanel::setResult(std::shared_ptr<const WindowResearch> result,bool processed) {
    result_=std::move(result);processed_=processed;
    if(!result_){scope_->setText(text("window.empty"));spectrogram_->setResult({},{});for(auto* curve:{psd_,correlation_,histogram_})curve->setSeries({}, {}, {});for(auto* field:statistics_)field->setText(text("unavailable"));return;}
    const auto& options=result_->options;const auto& stats=result_->statistics;const auto unit=options.scaling==SpectralScaling::power_density?QStringLiteral("dBFS/Hz"):QStringLiteral("dBFS/bin");
    spectrogram_->setResult(result_,unit);refreshColors();
    const auto center=result_->analysis->processed?result_->analysis->processed->center_frequency_hz:result_->analysis->source->stream.center_frequency_hz;
    scope_->setText(text("window.scope",{{"source",text(processed?"channel.tab":"channel.raw_tab").toStdString()},{"first",std::to_string(result_->range.first)},
        {"count",std::to_string(result_->range.count)},{"frames",result_->frame_first_samples.size()},{"center",displayDecimal(center/1e6,6,locale()).toStdString()},{"unit",unit.toStdString()}}));
    const auto format=[&](double number,unsigned precision=6){return displayDecimal(number,static_cast<int>(precision),locale());};
    statistics_[0]->setText(QString::number(stats.observed_samples));statistics_[1]->setText(QString::number(stats.missing_clock_positions));
    statistics_[2]->setText(stats.observed_samples?(stats.mean_power>0?format(10*std::log10(stats.mean_power),3):QStringLiteral("−∞"))+QStringLiteral(" dBFS"):text("unavailable"));
    statistics_[3]->setText(stats.observed_samples?text("window.range_value",{{"low",format(stats.minimum_amplitude).toStdString()},{"high",format(stats.maximum_amplitude).toStdString()}}):text("unavailable"));
    statistics_[4]->setText(stats.valid_frequency_samples?format(stats.mean_frequency_hz,3)+QStringLiteral(" Hz"):text("unavailable"));statistics_[5]->setText(stats.valid_frequency_samples?format(stats.frequency_standard_deviation_hz,3)+QStringLiteral(" Hz"):text("unavailable"));statistics_[6]->setText(QString::number(stats.valid_frequency_samples));
    std::vector<QPointF> spectrum,correlation,histogram;const auto n=result_->average_spectral_power.size();spectrum.reserve(n);
    if(!result_->frame_first_samples.empty())for(std::size_t i=0;i<n;++i)spectrum.emplace_back((double(i)-n/2.)*result_->analysis->sample_rate()/n,10*std::log10(std::max(result_->average_spectral_power[i],1e-30)));
    for(const auto& point:result_->correlation)correlation.emplace_back(double(point.lag_samples)/result_->analysis->sample_rate()*1000,point.pairs?std::abs(point.value):std::numeric_limits<double>::quiet_NaN());
    const auto& distribution=result_->frequency_histogram;if(stats.valid_frequency_samples)for(std::size_t i=0;i<distribution.counts.size();++i)histogram.emplace_back(distribution.low_hz+(i+.5)*(distribution.high_hz-distribution.low_hz)/distribution.counts.size(),double(distribution.counts[i]));
    psd_->setSeries(std::move(spectrum),QStringLiteral("Hz"),unit);correlation_->setSeries(std::move(correlation),QStringLiteral("ms"),text("window.correlation_unit"));histogram_->setSeries(std::move(histogram),QStringLiteral("Hz"),text("window.count_unit"),true);
}
Json WaveformWindowPanel::configuration() const {
    if(!result_)return {{"version",1},{"enabled",false}};const auto& options=result_->options;
    return {{"version",1},{"enabled",true},{"processed",processed_},{"fft_size",options.fft_size},{"hop_samples",options.hop_samples},
        {"window",static_cast<unsigned>(options.window)},{"scaling",static_cast<unsigned>(options.scaling)},{"correlation_lags",options.correlation_lags},{"frequency_histogram_bins",options.frequency_histogram_bins},
        {"range",{{"first",std::to_string(result_->range.first)},{"count",std::to_string(result_->range.count)}}},
        {"view",{{"page",pages_->currentIndex()},{"minimum_db",applied_low_db_},{"maximum_db",applied_high_db_}}}};
}
void WaveformWindowPanel::restoreView(const Json& config) {
    if(!result_ || !config.contains("view"))return;const auto& view=config.at("view");const auto low=view.at("minimum_db").get<double>(),high=view.at("maximum_db").get<double>();
    if(!std::isfinite(low) || !std::isfinite(high) || low< -300 || high>120 || low>=high || view.at("page")<0 || view.at("page")>=pages_->count())throw std::invalid_argument("Invalid saved research view");
    low_->setValue(low);high_->setValue(high);pages_->setCurrentIndex(view.at("page").get<int>());refreshColors();
}
Json WaveformWindowPanel::snapshot() const {
    Json result={{"configuration",configuration()}};if(result_)result.update({{"method",result_->method},{"result_payload_bytes",result_->payload_bytes()},{"observed_samples",result_->statistics.observed_samples},{"missing_clock_positions",result_->statistics.missing_clock_positions},{"mean_power",result_->statistics.mean_power}});return result;
}
}
