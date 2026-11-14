#pragma once

#include "signalscope/adapters/modulation/snapshot_detection.hpp"

namespace signalscope {
struct RecordingScanOptions {
    std::uint64_t first=0,count=0;
    double search_width=1000000;
    SnapshotSelection detectors{{true,true},true,true,true};
};

// Emit window-relative evidence with absolute recording sample coordinates.
// Windows overlap. Repeated observations across windows remain separate.
void scanRecording(const ReplayInfo&,const RecordingScanOptions&,
                   const std::function<void(const Json&)>& emitWindow,
                   const std::function<bool()>& cancelled);
}
