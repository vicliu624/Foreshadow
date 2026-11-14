#include "receiver_probe.hpp"
#include "plugin_text.hpp"
#include "signalscope/adapters/receivers/receiver.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <QLocale>

namespace signalscope::desktop {
QString receiverProbeSummary(const QJsonObject& result) {
    QStringList lines;
    const auto model=result.value("hardware").toString().simplified();
    if(!model.isEmpty())
        lines.push_back(QCoreApplication::translate("ReceiverDialog","Model: %1").arg(model));
    for(const auto& item:result.value("information").toArray()) {
        const auto field=item.toObject();
        const auto data=Json::parse(QJsonDocument(field).toJson().toStdString());
        const auto label=pluginText(data.value("label",Json()),QString::fromStdString(data.value("label_key",std::string())));
        lines<<label+QStringLiteral(": ")+field.value("value").toString();
    }
    return lines.join(u'\n');
}

QString receiverSettingsPrefix(const QString& backend) {
    for(const auto& plugin:receiver_plugin_diagnostics())
        if(plugin.value("id",std::string())==backend.toStdString() && plugin.contains("settings_prefix"))
            return QString::fromStdString(plugin.at("settings_prefix").get<std::string>());
    return QStringLiteral("receiver/")+backend+'/';
}

std::vector<std::uint32_t> receiverMonitorRates(const QJsonObject& result,std::span<const std::uint32_t> presets) {
    const auto field=result.value("sample_rates_hz");
    if(field.isUndefined())return {presets.begin(),presets.end()};
    const auto reported=field.toArray();
    std::vector<std::uint32_t> rates;
    for(const auto& value:reported) {
        const auto rate=value.toDouble();
        if(value.isDouble() && std::isfinite(rate) && rate>=1 && rate<=100000000 && std::floor(rate)==rate)
            rates.push_back(static_cast<std::uint32_t>(rate));
    }
    std::sort(rates.begin(),rates.end());rates.erase(std::unique(rates.begin(),rates.end()),rates.end());
    return rates;
}

QString receiverUnavailableMessage(const QJsonObject& result) {
    if(result.contains("message")) {
        const auto data=Json::parse(QJsonDocument(result).toJson().toStdString());
        return pluginText(data.at("message"),QString::fromStdString(data.value("reason_code",std::string())));
    }
    const auto code=result.value("reason_code").toString();
    if(code==QLatin1String(receiver_backend_not_built))
        return QCoreApplication::translate("ReceiverDialog","This build does not include the selected receiver backend.");
    if(code==QLatin1String(receiver_driver_missing))
        return QCoreApplication::translate("ReceiverDialog","The receiver driver is missing or could not load. Check the vendor driver and SoapySDR module.");
    return {};
}

std::optional<std::vector<int>> probeGainPresets(const QJsonObject& result) {
    const auto field=result.value("gain_tenths_db");
    if(!field.isArray())return std::nullopt;
    std::vector<int> gains;
    for(const auto& value:field.toArray()) {
        const double gain=value.toDouble(1001);
        if(gain>=-1000 && gain<=1000 && gain==std::trunc(gain))
            gains.push_back(static_cast<int>(gain));
    }
    std::sort(gains.begin(),gains.end());
    gains.erase(std::unique(gains.begin(),gains.end()),gains.end());
    return gains;
}

QString configurationIssueText(const ConfigurationIssue& i) {
    if(i.values.contains("message")) {
        const auto& messages=i.values.at("message");
        return pluginText(messages,QString::fromStdString(i.code));
    }
        if(i.code=="receiver_rate")return QCoreApplication::translate("ReceiverConfiguration","This sample rate is outside the device's reported capability.");
        else if(i.code=="receiver_frequency")return QCoreApplication::translate("ReceiverConfiguration","This frequency is outside the selected tuner's supported ranges.");
        else if(i.code=="receiver_antenna")return QCoreApplication::translate("ReceiverConfiguration","This antenna port is not available on the selected device.");
        else if(i.code=="antenna_frequency")return QCoreApplication::translate("ReceiverConfiguration","This antenna port supports frequencies up to %1 MHz.").arg(i.values.at("maximum_hz").get<double>()/1e6);
        else if(i.code=="receiver_gain")return QCoreApplication::translate("ReceiverConfiguration","This manual gain is not supported by the selected device.");
        else if(i.code=="search_passband")return QCoreApplication::translate("ReceiverConfiguration","Search width %1 kHz exceeds the sampled span of %2 kHz. Increase the sample rate or reduce Search width.")
            .arg(i.values.value("requested_hz",0.)/1000,0,'f',0).arg(i.values.value("maximum_hz",0.)/1000,0,'f',0);
        else if(i.code=="receiver_passband")return QCoreApplication::translate("ReceiverConfiguration","The required passband exceeds the receiver's available passband.");
        else if(i.code=="lora_bandwidth")return QCoreApplication::translate("ReceiverConfiguration","The selected LoRa bandwidths do not fit the sampled span or cannot share an analysis clock.");
        else if(i.code=="receiver_input_mode")return QCoreApplication::translate("ReceiverConfiguration","This RF input mode is unavailable on the selected receiver.");
        else if(i.code=="receiver_if_bandwidth")return QCoreApplication::translate("ReceiverConfiguration","This IF filter is not reported by the receiver. Select Automatic filter or a listed bandwidth.");
        else if(i.code=="detector_edge_coverage")return QCoreApplication::translate("ReceiverConfiguration","The full sampled span is searched. Complete signals of the largest selected LoRa bandwidth fit within the central %1 kHz; signals at the outer edges may be truncated.").arg(i.values.value("complete_centers_hz",0.)/1000,0,'f',0);
        else if(i.code=="filter_rolloff")return QCoreApplication::translate("ReceiverConfiguration","The IF filter's 3 dB bandwidth is %1 MHz. The wider sampled span remains searchable, with reduced sensitivity near its edges.").arg(i.values.value("filter_hz",0.)/1e6,0,'f',3);
        else if(i.code=="filter_above_sample_span")return QCoreApplication::translate("ReceiverConfiguration","The manual IF filter is wider than the sampled span; out-of-band signals may alias. Automatic filter follows the driver's sample-rate-compatible selection.");
        else if(i.code=="rtl_usb_rate_risk")return QCoreApplication::translate("ReceiverConfiguration","Rates above 2.56 MS/s can lose samples on RTL-SDR. This rate remains available; check dropped samples during capture.");
        else if(i.code=="throughput_unverified")return QCoreApplication::translate("ReceiverConfiguration","Real-time throughput depends on this computer and the selected detectors.");
    return QCoreApplication::translate("ReceiverConfiguration","Configuration detail: %1 (%2)").arg(QString::fromStdString(i.code),QString::fromStdString(i.field));
}
QString receiverPlanProblemSummary(const ReceiverPlan& plan) {
    for(const auto& issue:plan.issues)if(issue.blocking) {
        const auto message=configurationIssueText(issue);
        if(!message.isEmpty())return message;
    }
    return {};
}
QString receiverPlanSummary(const ReceiverCapabilities& caps,const Config& cfg,const ReceiverPlan& plan) {
    QStringList lines;
    lines<<QCoreApplication::translate("ReceiverConfiguration","I/Q format: %1 · Raw data: %2 MB/s").arg(QString::fromStdString(sample_datatype(caps.format)))
        .arg(plan.bytes_per_second/1e6,0,'f',2);
    lines<<(plan.nominal_adc_bits?QCoreApplication::translate("ReceiverConfiguration","Nominal ADC: %1 bit (specification-derived; not ENOB)").arg(*plan.nominal_adc_bits):
             QCoreApplication::translate("ReceiverConfiguration","ADC operating bits: not reported"));
    lines<<(plan.filter_bandwidth_hz>0?QCoreApplication::translate("ReceiverConfiguration","IF filter: %1 MHz at 3 dB (predicted; confirmed on apply)").arg(plan.filter_bandwidth_hz/1e6,0,'f',3):
             QCoreApplication::translate("ReceiverConfiguration","IF filter bandwidth: not reported"));
    lines<<QCoreApplication::translate("ReceiverConfiguration","Maximum search width for these detectors: %1 kHz").arg(plan.maximum_search_width_hz/1000,0,'f',0);
    if(plan.lora_channels)lines<<QCoreApplication::translate("ReceiverConfiguration","LoRa analysis: %1 overlapping channel(s) at %2 MS/s. Acquisition and recordings retain the source rate.")
        .arg(plan.lora_channels).arg(plan.lora_analysis_rate_hz/1e6,0,'f',3);
    const auto input=receiverInputMode(caps,cfg);
    if(input=="direct_i" || input=="direct_q")lines<<QCoreApplication::translate("ReceiverConfiguration","HF direct sampling (%1): tuner gain controls are inactive. RF sensitivity and alias rejection depend on the board and antenna.").arg(QString::fromStdString(input));
    lines<<(caps.frequency_source=="driver"?QCoreApplication::translate("ReceiverConfiguration","Frequency limits: driver-reported"):
             caps.frequency_source=="unknown"?QCoreApplication::translate("ReceiverConfiguration","Frequency limits: unknown; checked when applied"):
             QCoreApplication::translate("ReceiverConfiguration","Frequency limits: nominal hardware specification; RF passband unverified"));
    for(const auto& issue:plan.issues) {
        const auto message=configurationIssueText(issue);
        if(!message.isEmpty())lines<<message;
    }
    if(cfg.auto_gain && input=="tuner")lines<<QCoreApplication::translate("ReceiverConfiguration","Automatic gain is enabled; the manual gain value is inactive.");
    return lines.join(u'\n');
}
QString receiverAppliedSummary(const Json& applied) {
    if(!applied.is_object() || !applied.contains("actual"))return {};
    const auto& a=applied.at("actual");
    auto text=QCoreApplication::translate("ReceiverConfiguration","Applied and read back: %1 MS/s · %2 MHz")
        .arg(a.at("sample_rate_hz").get<double>()/1e6,0,'f',3).arg(a.at("frequency_hz").get<double>()/1e6,0,'f',6);
    if(a.contains("filter_bandwidth_hz") && a.at("filter_bandwidth_hz").is_number())
        text+=QCoreApplication::translate("ReceiverConfiguration"," · IF %1 MHz").arg(a.at("filter_bandwidth_hz").get<double>()/1e6,0,'f',3);
    if(applied.value("gain_mode_confirmation",std::string())=="driver_acknowledged")
        text+=QCoreApplication::translate("ReceiverConfiguration"," · Gain mode acknowledged; readback unavailable");
    if(applied.value("activation_confirmed",false))
        text+=QCoreApplication::translate("ReceiverConfiguration"," · Receiver startup confirmed");
    return text;
}
}
