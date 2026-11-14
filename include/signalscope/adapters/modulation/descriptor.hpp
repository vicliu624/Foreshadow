#pragma once
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include <functional>

namespace signalscope {
enum class DetectorStage { lora_stream, narrowband_stream, snapshot };
struct DetectorSelection {
    bool lora=true;
    NarrowbandSelection narrowband;
    SnapshotSelection snapshot;
};
struct DetectorContext {
    Config receiver;
    LoRaConfig lora;
    double maximum_span_hz=0;
    std::size_t analysis_channels=0;
    std::uint32_t analysis_rate=0;
};
struct ModulationDescriptor {
    std::string id,preference_key;
    DetectorStage stage;
    std::function<bool&(DetectorSelection&)> enabled;
    // Returns text references/arguments and coverage facts. All detector-specific
    // parsing claims and RF requirements belong to this provider, not its UI.
    std::function<Json(const DetectorContext&)> describe;
};
std::vector<ModulationDescriptor> modulationDescriptors();
void registerModulationDescriptor(ModulationDescriptor);
}
