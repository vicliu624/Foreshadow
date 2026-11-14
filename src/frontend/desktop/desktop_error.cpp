#include "desktop_error.hpp"
#include "signalscope/backend/iq/error.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/reception.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"

#include <QCoreApplication>
#include <QLabel>

namespace signalscope::desktop {
namespace {
QString message(ErrorCode code) {
    switch (code) {
    case ErrorCode::receiver_unavailable:
        return QCoreApplication::translate("OperationErrors", "The receiver backend is unavailable. Check the build and installed drivers.");
    case ErrorCode::receiver_open:
        return QCoreApplication::translate("OperationErrors", "Could not open the receiver. Check its connection, USB permissions and whether another app is using it.");
    case ErrorCode::receiver_configuration:
        return QCoreApplication::translate("OperationErrors", "The receiver could not apply the frequency, sample rate or gain.");
    case ErrorCode::receiver_antenna_frequency:
        return QCoreApplication::translate("OperationErrors", "Antenna C supports up to 200 MHz. Select Antenna A or B for this frequency.");
    case ErrorCode::receiver_passband:
        return QCoreApplication::translate("OperationErrors", "The requested search is wider than the sampled span. Increase the sample rate or reduce Search width.");
    case ErrorCode::receiver_stream:
        return QCoreApplication::translate("OperationErrors", "The receiver stopped delivering I/Q. Check the connection and try again.");
    case ErrorCode::recording_open:
        return QCoreApplication::translate("OperationErrors", "Could not access the recording. Check the path, the SigMF file pair and file permissions.");
    case ErrorCode::recording_invalid:
        return QCoreApplication::translate("OperationErrors", "The recording contains invalid SigMF metadata or incomplete I/Q data.");
    case ErrorCode::recording_format:
        return QCoreApplication::translate("OperationErrors", "This recording uses an unsupported SigMF format.");
    case ErrorCode::recording_write:
        return QCoreApplication::translate("OperationErrors", "Could not finish writing the recording. Check disk space and permissions.");
    case ErrorCode::recording_space:
        return QCoreApplication::translate("OperationErrors", "There is not enough free disk space for the recording.");
    case ErrorCode::event_store_unavailable:
        return QCoreApplication::translate("OperationErrors", "The event folder is unavailable. Check its permissions or whether another Foreshadow session is using it.");
    case ErrorCode::export_destination_exists:
        return QCoreApplication::translate("OperationErrors", "The export destination already exists. Choose a different location.");
    case ErrorCode::recording_in_progress:
        return QCoreApplication::translate("OperationErrors", "Wait for the recording to finish before exporting.");
    }
    return {};
}
}

QString error_summary(const std::exception& error,const QString& fallback) {
    if(const auto* reception=dynamic_cast<const ReceptionError*>(&error)) {
        const auto& diagnostic=reception->status.diagnostic;
        const auto key=diagnostic.value("message_key",Json()).is_string()?diagnostic.at("message_key").get<std::string>():"reception."+reception->status.state;
        Json value={{"label_key",key}};TextCatalog::resource("desktop-status").resolve(value);
        return localizedText(value.at("label"),QLocale().name(),fallback);
    }
    if(const auto* source=dynamic_cast<const SourceError*>(&error);source && source->details.is_object() && source->details.value("reason_code",std::string())=="stream_discontinuity")
        return QCoreApplication::translate("OperationErrors","The receiver stream overflowed or became discontinuous. Reception stopped with incomplete data. Check system load and the USB connection.");
    const auto* operation = dynamic_cast<const OperationError*>(&error);
    const auto summary = operation ? message(operation->code()) : QString();
    return summary.isEmpty() ? fallback : summary;
}

void show_error(QLabel& label, const std::exception& error, const QString& fallback) {
    label.setText(error_summary(error,fallback));
    label.setToolTip("<pre>" + QString::fromUtf8(error.what()).toHtmlEscaped() + "</pre>");
}
}
