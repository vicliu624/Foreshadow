#include "event_presentation.hpp"
#include "signalscope/desktop/number_format.hpp"
#include "plugin_text.hpp"

#include <QCoreApplication>
#include <cmath>

namespace signalscope::desktop {
namespace {
const auto missing=QStringLiteral("—");

QString decimal(const Json& value,int decimals,double scale=1) {
    if(!value.is_number())return missing;
    const auto scaled=value.get<double>()*scale;
    if(!std::isfinite(scaled))return missing;
    return displayDecimal(scaled,decimals);
}

QString integer(const Json& value) {
    if(value.is_number_unsigned())return QString::number(value.get<qulonglong>());
    if(value.is_number_integer())return QString::number(value.get<qlonglong>());
    return missing;
}

QString modulation(const Json& event,const std::string& level) {
    if(event.contains("display_label"))return pluginText(event.at("display_label"));
    if(level=="broadcast_fm_stereo")return QCoreApplication::translate("MonitorWindow", "Broadcast FM (stereo)");
    if(level=="lora_structure_match")return QCoreApplication::translate("MonitorWindow", "LoRa structure");
    if(level=="fsk_candidate")return QStringLiteral("2-FSK / GFSK");
    if(level=="ook_candidate")return QStringLiteral("OOK");
    if(level=="ask_candidate")return QStringLiteral("ASK");
    if(level=="energy_transient")return QCoreApplication::translate("MonitorWindow", "Energy candidate");
    if(level=="chirp_probe")return QCoreApplication::translate("MonitorWindow", "Chirp candidate");
    if(level=="css_candidate")return QCoreApplication::translate("MonitorWindow", "%1 candidate").arg(QStringLiteral("CSS"));
    if(level=="extended_candidate") {
        const auto name=event.value("modulation",std::string());
        if(!name.empty())return QCoreApplication::translate("MonitorWindow", "%1 candidate").arg(QString::fromStdString(name));
    }
    const auto name=event.value("modulation",event.value("modulation_id",std::string()));
    if(!name.empty())return QCoreApplication::translate("MonitorWindow", "%1 candidate").arg(QString::fromStdString(name));
    return QCoreApplication::translate("MonitorWindow", "Unclassified event");
}

QString parameters(const Json& event,const std::string& level) {
    if(event.contains("features") && event.at("features").is_array()) {
        QStringList values;
        for(const auto& feature:event.at("features")) {
            if(!feature.is_object() || !feature.contains("value"))continue;
            const auto& value=feature.at("value");
            const auto label=pluginText(feature.value("label",Json()),QString::fromStdString(feature.value("key",std::string())));
            auto formatted=value.is_number_float()?decimal(value,feature.value("decimals",3)):
                value.is_number_integer()?integer(value):QString::fromStdString(value.is_string()?value.get<std::string>():value.dump());
            if(feature.contains("unit"))formatted+=' '+QString::fromStdString(feature.at("unit").get<std::string>());
            values<<QCoreApplication::translate("MonitorWindow", "%1: %2").arg(label,formatted);
        }
        if(!values.empty())return values.join(QStringLiteral(" · "));
    }
    if(level=="broadcast_fm_stereo")return QCoreApplication::translate("MonitorWindow", "19 kHz pilot");
    if(event.value("prefix_seconds",0.)>0)
        return QCoreApplication::translate("MonitorWindow", "Cyclic prefix %1 µs").arg(decimal(event.at("prefix_seconds"),1,1e6));
    if(event.value("repeated_chirps",0u)>0)
        return QCoreApplication::translate("MonitorWindow", "Repeated chirps: %1").arg(integer(event.at("repeated_chirps")));
    if(level=="extended_candidate")return QCoreApplication::translate("MonitorWindow", "Snapshot analysis");
    if(level=="fsk_candidate" && event.contains("tone_separation_hz"))
        return QCoreApplication::translate("MonitorWindow", "Tone spacing %1 kHz").arg(decimal(event.at("tone_separation_hz"),1,1e-3));
    if((level=="ook_candidate" || level=="ask_candidate") && event.contains("amplitude_runs"))
        return QCoreApplication::translate("MonitorWindow", "Level runs %1").arg(integer(event.at("amplitude_runs")));
    const auto bandwidth=event.value("bandwidth_hz",Json());
    const auto spreading=event.value("sf_candidate",Json());
    if(bandwidth.is_number() && spreading.is_number_integer())
        return QCoreApplication::translate("MonitorWindow", "%1 kHz / SF%2").arg(decimal(bandwidth,0,1e-3),integer(spreading));
    return missing;
}
}

QStringList eventColumns() {
    return {
        QCoreApplication::translate("MonitorWindow", "Modulation"),
        QCoreApplication::translate("MonitorWindow", "Center (MHz)"),
        QCoreApplication::translate("MonitorWindow", "Parameters"),
        QCoreApplication::translate("MonitorWindow", "Component (dBFS)"),
        QCoreApplication::translate("MonitorWindow", "Peak ratio (dB)"),
        QCoreApplication::translate("MonitorWindow", "First sample"),
    };
}

QStringList eventCells(const Json& event) {
    const auto level=event.value("evidence_level",std::string());
    const auto ratio=event.value("pilot_contrast_db",event.value("preamble_peak_to_mean_db",event.value("peak_to_mean_db",Json())));
    return {modulation(event,level),decimal(event.value("center_frequency_hz",Json()),6,1e-6),
            parameters(event,level),decimal(event.value("matched_component_dbfs",Json()),1),
            decimal(ratio,1),integer(event.value("first_observed_sample",Json()))};
}

QString eventEvidence(const Json& event) {
    const auto fields=eventColumns();
    const auto values=eventCells(event);
    QStringList lines;
    for(qsizetype i=0;i<fields.size();++i) {
        if(values[i]!=missing)
            lines.push_back(QCoreApplication::translate("MonitorWindow", "%1: %2").arg(fields[i],values[i]));
    }
    lines << QString() << QCoreApplication::translate("MonitorWindow", "Raw event data (JSON)")
          << QString::fromStdString(event.dump(2));
    return lines.join(u'\n');
}
}
