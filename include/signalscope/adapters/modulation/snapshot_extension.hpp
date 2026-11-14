#pragma once
#include "signalscope/adapters/modulation/descriptor.hpp"
namespace signalscope {
inline constexpr unsigned snapshotExtensionProtocolVersion=2;
struct SnapshotInput {
    std::span<const std::complex<float>> samples;
    std::uint32_t rate;
    double center,searchWidth;
    std::uint64_t first;
};
struct SnapshotExtension {
    ModulationDescriptor descriptor;
    std::function<void(const SnapshotInput&,Json&)> analyze;
    SnapshotObservation observation;
    unsigned protocol_version=snapshotExtensionProtocolVersion;
};
// Register during composition, before starting analysis threads. The registry
// owns descriptor/callback copies; consumers receive stable snapshots of it.
void registerSnapshotExtension(SnapshotExtension);
}
