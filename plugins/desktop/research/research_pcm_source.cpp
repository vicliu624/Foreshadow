#include "research_pcm_source.hpp"
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace signalscope::desktop {
ResearchPcmSource::ResearchPcmSource(const ResearchAudio& input,QAudioFormat output)
    :input_(readResearchAudio(input.wav)),output_(output) {
    if(!output.isValid() || output.sampleRate()<8000 || output.sampleRate()>384000 ||
       output.channelCount()<1 || output.channelCount()>32 || output.bytesPerSample()<1 || output.bytesPerSample()>4)
        throw std::runtime_error("Invalid PCM presentation format");
    stride_=output.bytesPerFrame();
    const auto frames=(std::uint64_t(input_.frames)*output.sampleRate()+input_.rate-1)/input_.rate;
    bytes_=static_cast<qint64>(frames*stride_);
    open(QIODevice::ReadOnly|QIODevice::Unbuffered);
}
qint64 ResearchPcmSource::readData(char* destination,qint64 requested) {
    const auto count=std::min({requested,bytes_-offset_,qint64(64*1024)});
    if(count<=0)return 0;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    if(output_.sampleRate()==static_cast<int>(input_.rate) && output_.channelCount()==input_.channels &&
       output_.sampleFormat()==QAudioFormat::Int16) {
        std::memcpy(destination,input_.wav.constData()+44+offset_,count);offset_+=count;return count;
    }
#endif
    std::array<char,32*4> frame{};
    qint64 copied=0;
    while(copied<count) {
        const auto frameIndex=std::uint64_t(offset_)/stride_;
        const auto phase=frameIndex*input_.rate;
        const auto first=std::min<std::uint64_t>(phase/output_.sampleRate(),input_.frames-1);
        const auto next=std::min<std::uint64_t>(first+1,input_.frames-1);
        const double fraction=double(phase%output_.sampleRate())/output_.sampleRate();
        const auto sample=[&](unsigned channel){
            const auto at=[&](std::uint64_t index){return double(qFromLittleEndian<qint16>(input_.wav.constData()+44+(index*input_.channels+channel)*2))/32768.;};
            return at(first)+(at(next)-at(first))*fraction;
        };
        for(int channel=0;channel<output_.channelCount();++channel) {
            double value=0;
            if(output_.channelCount()==1)value=input_.channels==1?sample(0):(sample(0)+sample(1))*.5;
            else if(channel<2)value=sample(input_.channels==1?0:channel);
            auto* target=frame.data()+channel*output_.bytesPerSample();
            switch(output_.sampleFormat()) {
            case QAudioFormat::UInt8:{const auto v=static_cast<quint8>(std::clamp(std::lround(value*128+128),0L,255L));std::memcpy(target,&v,sizeof v);break;}
            case QAudioFormat::Int16:{const auto v=static_cast<qint16>(std::clamp(std::lround(value*32768),-32768L,32767L));std::memcpy(target,&v,sizeof v);break;}
            case QAudioFormat::Int32:{const auto v=static_cast<qint32>(std::clamp(std::llround(value*2147483648.),-2147483648LL,2147483647LL));std::memcpy(target,&v,sizeof v);break;}
            case QAudioFormat::Float:{const auto v=static_cast<float>(value);std::memcpy(target,&v,sizeof v);break;}
            default:throw std::runtime_error("Unsupported PCM presentation sample type");
            }
        }
        const auto begin=offset_%stride_;
        const auto length=std::min<qint64>(stride_-begin,count-copied);
        std::memcpy(destination+copied,frame.data()+begin,length);copied+=length;offset_+=length;
    }
    return count;
}
}
