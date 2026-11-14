#pragma once
#include "research_audio.hpp"
#include <QAudioFormat>
#include <QIODevice>

namespace signalscope::desktop {
// A finite presentation stream. Conversion is incremental and does not change
// the canonical WAV or allocate a resampled copy of the entire analysis result.
class ResearchPcmSource final:public QIODevice {
public:
    ResearchPcmSource(const ResearchAudio&,QAudioFormat output);
    bool isSequential() const override{return true;}
    bool atEnd() const override{return offset_==bytes_;}
    qint64 bytesAvailable() const override{return bytes_-offset_+QIODevice::bytesAvailable();}
    qint64 size() const override{return bytes_;}
protected:
    qint64 readData(char*,qint64) override;
    qint64 writeData(const char*,qint64) override{return -1;}
private:
    ResearchAudio input_;
    QAudioFormat output_;
    qint64 offset_=0,bytes_=0;
    unsigned stride_=0;
};
}
