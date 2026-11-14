#pragma once

#include "signalscope/backend/sessions/iq_subscribers.hpp"
#include "signalscope/adapters/modulation/snapshot_detection.hpp"
#include <memory>

namespace signalscope {
struct ModulationSnapshot {
    Bytes bytes;
    std::uint64_t first=0;
    std::uint32_t rate=0;
    double center=0,width=0;
    SampleFormat format=SampleFormat::cu8;
    SnapshotObservation observation;
};

class SnapshotCollector {
public:
    SnapshotCollector(std::uint32_t rate,double center,double width,SampleFormat format,SnapshotObservation observation={});
    ModulationSnapshot feed(std::span<const std::uint8_t> bytes,std::uint64_t first);
private:
    std::uint32_t rate_;
    double center_,width_,baseline_=0;
    SampleFormat format_;
    SnapshotObservation observation_;
    std::uint64_t next_=0,start_=0,eligible_=0;
    Bytes previous_,snapshot_;
};

class SnapshotAnalysis {
public:
    SnapshotAnalysis();
    ~SnapshotAnalysis();

    // The consumer thread owns lifecycle, polling and results. Reset or destroy
    // only after the thread using the previous feed has joined.
    void reset(SnapshotSelection selection);
    void poll();
    bool available() const;
    const Json& events() const;
    Json status() const;

    // One producer calls the returned sink. Busy or contended submissions
    // are dropped rather than making the producer wait for the consumer.
    IqBlockSubscriber makeFeed(double width);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
