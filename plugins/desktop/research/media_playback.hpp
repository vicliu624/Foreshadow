#pragma once
#include "signalscope/backend/media/protocol.hpp"
#include <QImage>
#include <memory>

namespace signalscope::desktop {
struct MediaSelection {unsigned program=0;int audio_pid=-1,video_pid=-1;std::string text_encoding="GB18030";};
struct MediaVideoFrame {QImage image;std::int64_t pts_us=0;std::uint64_t sequence=0,generation=0;double aspect_ratio=1;bool timestamp_known=true;};
struct MediaOutputDevice {std::string id,name;bool system_default=false,supported=false;};
class MediaPlayback {
public:
    MediaPlayback();
    ~MediaPlayback();
    static std::vector<MediaOutputDevice> outputDevices();
    void start(MediaSelection={},std::filesystem::path helper={});
    void select(MediaSelection);
    // This is a bounded try operation. A non-capture producer may retry after
    // backpressure; a producer that discards must report its own source gap.
    bool offerTransport(std::span<const std::uint8_t>) noexcept;
    void finishInput();
    void stop();
    void setVolume(double);
    void setMuted(bool);
    void setOutputDevice(std::string);
    Json snapshot() const;
    std::shared_ptr<const MediaVideoFrame> videoFrame() const;
    Bytes cachedTransport() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
