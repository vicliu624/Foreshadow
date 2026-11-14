#pragma once

#include "signalscope/adapters/modulation/fm/live_fm_audio.hpp"
#include <stdexcept>
#include <utility>

namespace signalscope::desktop {
struct AudioOutputDevice {std::string id,name;bool system_default=false,supports_pcm=false;bool operator==(const AudioOutputDevice&) const=default;};
struct FmPlaybackStatus {
    std::string state="stopped",device_name,device_id,error_key,error_detail;
    LiveFmAudioStatus input;
    std::uint64_t underruns=0,written_frames=0;
    double volume=1,output_level_dbfs=-120,queued_seconds=0;
    bool muted=false,squelched=false;
};
class FmPlaybackError:public std::runtime_error {
public:
    FmPlaybackError(std::string key,std::string detail={}):std::runtime_error(detail.empty()?key:detail),key(std::move(key)){}
    const std::string key;
};
class FmPlayback {
public:
    FmPlayback();
    ~FmPlayback();
    static bool supported();
    static std::vector<AudioOutputDevice> outputDevices();
    void start(const FmAudioConfig&,std::size_t maximumBlockBytes,const std::string& outputDevice={});
    void setOutputDevice(const std::string&);
    void setVolume(double);
    void setMuted(bool);
    void setSquelch(std::optional<double> snrThreshold);
    FmPlaybackStatus status() const;
    void stop();
    bool takeFailure();
    void offer(std::span<const std::uint8_t>,std::uint64_t first) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
