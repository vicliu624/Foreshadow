#pragma once
namespace signalscope::desktop {
// Call before the first Qt audio-device query. The desktop consumes PCM only;
// system codec plugins must not expand its runtime/dependency closure.
void initializePcmAudioBackend();
}
