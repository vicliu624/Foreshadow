#pragma once
#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include <QList>
#include <QString>
namespace signalscope::desktop {
struct ModulationAssessment {
    QString id,name,status,parsing,impact;
    QString parsingSummary,impactSummary;
    bool enabled=false,compatible=false,covered=true;
};
QList<ModulationAssessment> assessReceiverModulations(const ReceiverCapabilities&,const Config&,const LoRaConfig&,
    bool lora,NarrowbandSelection,SnapshotSelection);
}
