#pragma once
#include "research_audio.hpp"
#include <functional>
#include <memory>

namespace signalscope::desktop {
// Finite, validated ResearchAudio playback. The result protocol owns the WAV
// encoding; the host only starts/stops playback and presents generic errors.
class ResearchAudioPlayback {
public:
    ResearchAudioPlayback();
    ~ResearchAudioPlayback();
    ResearchAudioPlayback(const ResearchAudioPlayback&)=delete;
    ResearchAudioPlayback& operator=(const ResearchAudioPlayback&)=delete;
    void play(const ResearchAudio&);
    void stop();
    bool playing() const;
    void setVolume(float);
    std::function<void(bool)> playingChanged;
    // Diagnostic resource/error key; the consumer supplies its generic notice.
    std::function<void(const QString&)> failed;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
