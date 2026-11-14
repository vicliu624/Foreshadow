#pragma once

#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "../common/snapshot_detectors.hpp"
#include "signalscope/adapters/modulation/snapshot_extension.hpp"

namespace signalscope {
std::span<const SnapshotDetectorOption> legacySnapshotOptions();
std::span<const SnapshotExtension> builtinSnapshotExtensions();
}

namespace signalscope::snapshot {
using WidebandInput=SnapshotInput;

struct WidebandDetector {
    std::function<bool(const SnapshotSelection&)> enabled;
    std::function<void(const WidebandInput&,Json&)> analyze;
    SnapshotObservation observation;
};

struct BurstDetector {
    bool (*enabled)(const SnapshotSelection&);
    Json (*identify)(std::span<const Complex>,double,const SnapshotSelection&);
};

std::vector<WidebandDetector> widebandDetectors();
std::span<const BurstDetector> burstDetectors();
}
