#pragma once

#include "signalscope/adapters/modulation/psk.hpp"
#include <map>

namespace signalscope {
struct SnapshotSelection {
    PskSelection psk;
    bool four_fsk=true;
    bool ofdm=false;
    bool css=false;
    std::map<std::string,bool> extensions;
    bool any() const;
};

struct SnapshotDetectorOption {
    std::string id;
    std::string label;
    std::function<bool&(SnapshotSelection&)> enabled;
};

enum class SnapshotTrigger { energy, periodic };
struct SnapshotObservation {
    SnapshotTrigger trigger=SnapshotTrigger::energy;
    double duration_seconds=.25;
    double interval_seconds=1;
    std::uint32_t minimum_input_rate=0;
};
// Enabled providers own their observation requirements. Consumers coalesce
// compatible policies without knowing detector identities.
std::vector<SnapshotObservation> snapshotObservations(const SnapshotSelection&,std::uint32_t input_rate);
SnapshotSelection snapshotObservationSelection(const SnapshotSelection&,const SnapshotObservation&,std::uint32_t input_rate);

std::vector<SnapshotDetectorOption> snapshotDetectorOptions();

Json detectSnapshot(std::span<const std::uint8_t> bytes,SampleFormat format,
                    std::uint32_t rate,double center,double width,std::uint64_t first,
                    SnapshotSelection selection={});
}
