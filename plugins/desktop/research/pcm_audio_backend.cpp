#include "pcm_audio_backend.hpp"
#include <QtGlobal>
#include <QByteArrayView>
#include <QByteArray>
#if defined(SIGNALSCOPE_HAS_AUDIO) && QT_VERSION >= QT_VERSION_CHECK(6,8,0)
#include <QCoreApplication>
#include <QAudioDevice>
#include <QMediaDevices>
#endif

namespace signalscope::desktop {
void initializePcmAudioBackend() {
#if defined(SIGNALSCOPE_HAS_AUDIO) && QT_VERSION >= QT_VERSION_CHECK(6,8,0)
    // Qt 6.8 has built-in native audio devices/sinks in its fallback integration.
    // An explicit non-codec backend selection prevents automatic FFmpeg loading
    // even when the operating system has its multimedia plugin installed.
    // Restore the environment after initializing Qt's application singleton:
    // separately launched research tools retain their own backend selection.
    // Upgrade acceptance verifies the actual loaded
    // images, as well as audio output, rather than relying only on this setting.
    if(!QCoreApplication::instance())return;
    static const bool configured=[] {
        struct Restore {
            bool present=qEnvironmentVariableIsSet("QT_MEDIA_BACKEND");
            QByteArray value=qgetenv("QT_MEDIA_BACKEND");
            ~Restore(){if(present)qputenv("QT_MEDIA_BACKEND",value);else qunsetenv("QT_MEDIA_BACKEND");}
        } restore;
        qputenv("QT_MEDIA_BACKEND","none");
        (void)QMediaDevices::audioOutputs();
        return true;
    }();
    (void)configured;
#endif
}
}
