#include "plugins/desktop/research/research_audio.hpp"
#include <QtEndian>
#include <stdexcept>
#include <utility>

namespace signalscope::desktop {
ResearchAudio readResearchAudio(QByteArray wav) {
    if(wav.size()<44 || wav.first(4)!="RIFF" || wav.sliced(8,8)!="WAVEfmt " || wav.sliced(36,4)!="data")
        throw std::runtime_error("Research audio must be a PCM WAV file");
    const auto u16=[&](int index){return qFromLittleEndian<std::uint16_t>(wav.constData()+index);};
    const auto u32=[&](int index){return qFromLittleEndian<std::uint32_t>(wav.constData()+index);};
    const auto channels=u16(22);
    const auto rate=u32(24);
    const auto dataSize=u32(40);
    const auto stride=channels*2U;
    if(u32(4)!=wav.size()-8 || u32(16)!=16 || u16(20)!=1 || (channels!=1 && channels!=2) ||
       rate<8000 || rate>192000 || u16(34)!=16 || u16(32)!=stride || u32(28)!=rate*stride ||
       !dataSize || dataSize!=wav.size()-44 || dataSize%stride)
        throw std::runtime_error("Research audio must be 16-bit PCM WAV with one or two channels");
    return {std::move(wav),rate,channels,dataSize/stride};
}
}
