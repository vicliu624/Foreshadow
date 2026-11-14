#include "status_presentation.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"

#include <QCoreApplication>
#include <QDateTime>

namespace signalscope::desktop {
namespace {
QString recordingLabel(std::string_view code) {
    if(code=="external")
        return QCoreApplication::translate("MonitorWindow", "External recording");
    if(code=="event_complete" || code=="complete")
        return QCoreApplication::translate("MonitorWindow", "Saved");
    if(code=="recording")
        return QCoreApplication::translate("MonitorWindow", "Recording in progress");
    if(code=="size_or_time_limit")
        return QCoreApplication::translate("MonitorWindow", "Clip limit reached");
    if(code=="provisional_not_confirmed")
        return QCoreApplication::translate("MonitorWindow", "Unconfirmed candidate");
    if(code=="analysis_gap")
        return QCoreApplication::translate("MonitorWindow", "Partial recording with gaps");
    if(code=="interrupted" || code=="monitor_interrupted")
        return QCoreApplication::translate("MonitorWindow", "Recording interrupted");
    if(code=="monitor_stopped")
        return QCoreApplication::translate("MonitorWindow", "Clip closed when listening stopped");
    if(code=="cancelled" || code=="monitor_cancelled")
        return QCoreApplication::translate("MonitorWindow", "Recording cancelled");
    if(code=="incomplete")
        return QCoreApplication::translate("MonitorWindow", "Incomplete recording");
    if(code=="analysis_error")
        return QCoreApplication::translate("MonitorWindow", "Clip closed after an analysis error");
    if(code=="source_error")
        return QCoreApplication::translate("MonitorWindow", "Clip closed after an input error");
    return QCoreApplication::translate("MonitorWindow", "Unknown recording status");
}
}

QString sessionStatus(std::string_view code) {
    if(code=="listening")return QCoreApplication::translate("MonitorWindow", "Listening");
    if(code=="replaying")return QCoreApplication::translate("MonitorWindow", "Replaying");
    if(code=="replay_finished")return QCoreApplication::translate("MonitorWindow", "Replay finished");
    if(code=="stopped")return QCoreApplication::translate("MonitorWindow", "Stopped");
    if(code=="analysis_finished")return QCoreApplication::translate("MonitorWindow", "Finishing");
    if(code=="starting")return QCoreApplication::translate("MonitorWindow", "Starting");
    if(code=="source_error")return QCoreApplication::translate("MonitorWindow", "Input error");
    if(code=="error")return QCoreApplication::translate("MonitorWindow", "Session failed");
    return QCoreApplication::translate("MonitorWindow", "Unknown session status");
}

NarrowbandStatus narrowbandStatus(const Json& status) {
    const auto analysis=status.find("fsk_analysis");
    if(analysis==status.end() || !analysis->is_object())return {};
    const auto skipped=analysis->value("skipped_blocks",0ULL);
    const auto unavailable=analysis->value("unavailable",false);
    NarrowbandStatus result;
    if(unavailable)
        result.suffix=QCoreApplication::translate("MonitorWindow", " · Narrowband detection unavailable");
    else if(skipped) {
        if(analysis->contains("skipped_pairs"))
            result.suffix=QCoreApplication::translate("MonitorWindow", " · Narrowband analysis discarded %1 I/Q pairs")
                .arg(qulonglong(analysis->value("skipped_pairs",0ULL)));
        else result.suffix=QCoreApplication::translate("MonitorWindow", " · Narrowband blocks skipped %1").arg(qulonglong(skipped));
    }
    result.degraded=unavailable || skipped!=0;
    result.detail=QCoreApplication::translate("MonitorWindow", "Narrowband analysis scanned %1 blocks and skipped %2.")
        .arg(qulonglong(analysis->value("scanned_blocks",0ULL))).arg(qulonglong(skipped));
    if(const auto capacity=analysis->value("queue_capacity_bytes",0ULL);capacity) {
        result.detail+=u'\n';
        result.detail+=QCoreApplication::translate("MonitorWindow", "Analysis queue peak %1 / %2 MiB (%3 s). Software analysis discarded %4 I/Q pairs; this is separate from driver loss.")
            .arg(QLocale().toString(analysis->value("queue_high_water_bytes",0ULL)/1048576.,'f',2))
            .arg(QLocale().toString(capacity/1048576.,'f',2))
            .arg(QLocale().toString(analysis->value("queue_capacity_seconds",0.),'f',3))
            .arg(qulonglong(analysis->value("skipped_pairs",0ULL)));
    } else if(const auto capacity=analysis->value("queue_capacity_blocks",0ULL);capacity) {
        result.detail+=u'\n';
        result.detail+=QCoreApplication::translate("MonitorWindow", "Narrowband queue peaked at %1 of %2 blocks.")
            .arg(qulonglong(analysis->value("queue_high_water_blocks",0ULL))).arg(qulonglong(capacity));
    }
    return result;
}

QString receiverReadSummary(const Json& status) {
    const auto source=status.find("source_result");
    if(source==status.end() || !source->is_object())return {};
    const auto text=[](const std::string& key,const Json& args=Json::object()){
        Json value={{"label_key",key},{"arguments",args}};TextCatalog::resource("desktop-status").resolve(value);return localizedText(value.at("label"),QLocale().name());
    };
    QStringList lines;
    auto queue=source->value("input_queue",Json::object());
    if(source->value("stage",std::string())=="monitor_input")queue={{"input_pairs",source->value("input_pairs",0ULL)},{"rejected_pairs",source->value("rejected_pairs",0ULL)},{"capacity_bytes",source->value("queue_capacity_bytes",0ULL)},{"high_water_bytes",source->value("queue_high_water_bytes",0ULL)}};
    if(!queue.empty())lines<<text("input_queue",{{"input",queue.value("input_pairs",0ULL)},{"rejected",queue.value("rejected_pairs",0ULL)},{"peak",queue.value("high_water_bytes",0ULL)},{"capacity",queue.value("capacity_bytes",0ULL)}});
    if(!source->value("driver_report_final",true))lines<<text("driver_report_pending");
    const auto stats=source->find("read_stats");
    if(stats==source->end() || !stats->is_object())return lines.join(u'\n');
    const auto counts=QCoreApplication::translate("MonitorWindow", "Driver reads %L1 · Short reads %L2 · Timeouts %L3")
        .arg(qulonglong(stats->value("reads",0ULL))).arg(qulonglong(stats->value("short_reads",0ULL)))
        .arg(qulonglong(stats->value("timeouts",0ULL)));
    const auto sizes=QCoreApplication::translate("MonitorWindow", "Read sizes %L1–%L2 I/Q pairs · Delivered %L3 blocks")
        .arg(qulonglong(stats->value("min_read_pairs",0ULL))).arg(qulonglong(stats->value("max_read_pairs",0ULL)))
        .arg(qulonglong(stats->value("delivered_blocks",0ULL)));
    lines<<counts<<sizes;return lines.join(u'\n');
}

RecordingStatus recordingStatus(const Json& metadata) {
    const auto global=metadata.find("global");
    if(global==metadata.end() || !global->is_object())return {recordingLabel({}),{}};
    const auto status=global->find("signalscope:status");
    if(status==global->end())return {recordingLabel("external"),{}};
    if(!status->is_string())return {recordingLabel({}),{}};
    const auto& code=status->get_ref<const std::string&>();
    return {recordingLabel(code),QString::fromStdString(code)};
}

QString recordingTimeTooltip(const Json& metadata,const QLocale& locale,const QTimeZone& zone) {
    const auto format=[&](const Json& value,const QString& heading) -> QString {
        if(!value.is_string())return {};
        const auto original=QString::fromStdString(value.get<std::string>());
        const auto date=QDateTime::fromString(original,Qt::ISODateWithMs);
        if(!date.isValid() || date.timeSpec()==Qt::LocalTime || !zone.isValid())return {};
        const auto local=heading.arg(locale.toString(date.toTimeZone(zone),QLocale::ShortFormat));
        return local+'\n'+QCoreApplication::translate("MonitorWindow","Original timestamp: %1").arg(original);
    };
    const auto global=metadata.find("global");
    const bool hasGlobal=global!=metadata.end() && global->is_object();
    const bool hostObservation=hasGlobal &&
        global->contains("signalscope:time_source") &&
        global->at("signalscope:time_source")=="host observation; no hardware timestamp";
    const auto captures=metadata.find("captures");
    if(captures!=metadata.end() && captures->is_array() && !captures->empty() && captures->front().is_object()) {
        const auto& first=captures->front();
        if(const auto stamp=first.find("core:datetime");stamp!=first.end()) {
            const auto heading=hostObservation
                ?QCoreApplication::translate("MonitorWindow","Host observation time: %1")
                :QCoreApplication::translate("MonitorWindow","Capture metadata time: %1");
            if(const auto tooltip=format(*stamp,heading);!tooltip.isEmpty())return tooltip;
        }
    }
    if(hasGlobal)
        if(const auto stamp=global->find("signalscope:saved_utc");stamp!=global->end())
            return format(*stamp,QCoreApplication::translate("MonitorWindow","File saved at: %1"));
    return {};
}
}
