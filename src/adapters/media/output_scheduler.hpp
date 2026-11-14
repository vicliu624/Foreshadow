#pragma once
#include "signalscope/backend/media/protocol.hpp"
#include <memory>
#include <functional>
#include <span>
#include <stdexcept>

namespace signalscope::media {
class OutputFailure:public std::runtime_error {public:using std::runtime_error::runtime_error;};

// One decoder producer, one independent writer. A video deadline must not
// prevent the producer from decoding earlier audio or the writer from making
// progress while the demuxer is blocked waiting for live input.
class OutputScheduler {
public:
    using Sink=std::function<void(std::span<const std::uint8_t>)>;
    struct Limits {
        std::size_t video_bytes=64*1024*1024+maximumMetadata+headerBytes;
        std::size_t audio_bytes=1024*1024+maximumMetadata+headerBytes;
        std::size_t control_bytes=1024*1024;
        std::int64_t ahead_us=150000;
    };
    OutputScheduler(bool realtime,Sink);
    OutputScheduler(bool realtime,Sink,Limits);
    ~OutputScheduler();
    OutputScheduler(const OutputScheduler&)=delete;
    OutputScheduler& operator=(const OutputScheduler&)=delete;
    // Returns true when an actual timestamp discontinuity starts a new clock.
    bool submit(Message);
    void drain();
    void finish();
    void cancel() noexcept;
    Json snapshot() const;
private:
    class Impl;std::unique_ptr<Impl> impl_;
};
}
