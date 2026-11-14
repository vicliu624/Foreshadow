#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string_view>

namespace signalscope {
enum class SampleFormat { cu8, ci16_le, cf32_le };

constexpr int read_ci16_le(const std::uint8_t* bytes) {
    const int value=bytes[0]+256*bytes[1];
    return value<32768 ? value : value-65536;
}
struct SampleCodec {
    SampleFormat format;
    const char* datatype;
    std::size_t pair_bytes;
    std::complex<float> (*decode)(const std::uint8_t*);
    bool (*clipped)(const std::uint8_t*);
    const char* power_reference;
    void (*encode)(std::uint8_t*,std::complex<float>)=nullptr;
};
inline float read_cf32_le(const std::uint8_t* p) {
    const std::uint32_t bits=std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
    const auto value=std::bit_cast<float>(bits);
    if(!std::isfinite(value))throw std::invalid_argument("Non-finite floating-point I/Q sample");
    return value;
}
// This is the codec composition point. Consumers use its contract and do not
// branch on storage formats. Normalized full-scale component amplitude is 1.
inline constexpr std::array sample_codecs{
    SampleCodec{SampleFormat::cu8,"cu8",2,
        [](const std::uint8_t* p){return std::complex<float>((p[0]-127.5f)/128,(p[1]-127.5f)/128);},
        [](const std::uint8_t* p){return p[0]==0 || p[0]==255 || p[1]==0 || p[1]==255;},
        "mean |(I+jQ)/128|^2; midpoint=127.5; full input band; not RF dBm",
        [](std::uint8_t* p,std::complex<float> value){
            const auto component=[](float x){return static_cast<std::uint8_t>(std::clamp(std::round(x*128.+127.5),0.,255.));};
            p[0]=component(value.real());p[1]=component(value.imag());
        }},
    SampleCodec{SampleFormat::ci16_le,"ci16_le",4,
        [](const std::uint8_t* p){return std::complex<float>(read_ci16_le(p)/32768.f,read_ci16_le(p+2)/32768.f);},
        [](const std::uint8_t* p){return read_ci16_le(p)==-32768 || read_ci16_le(p)==32767 || read_ci16_le(p+2)==-32768 || read_ci16_le(p+2)==32767;},
        "mean |(I+jQ)/32768|^2; full input band; not RF dBm",
        [](std::uint8_t* p,std::complex<float> value){
            for(const auto x:{value.real(),value.imag()}) {
                const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(std::round(x*32768.),-32768.,32767.)));
                *p++=static_cast<std::uint8_t>(word);*p++=static_cast<std::uint8_t>(word>>8);
            }
        }},
    SampleCodec{SampleFormat::cf32_le,"cf32_le",8,
        [](const std::uint8_t* p){return std::complex<float>(read_cf32_le(p),read_cf32_le(p+4));},
        [](const std::uint8_t* p){return std::abs(read_cf32_le(p))>=1 || std::abs(read_cf32_le(p+4))>=1;},
        "mean |I+jQ|^2; normalized float full scale=1; full input band; not RF dBm",
        [](std::uint8_t* p,std::complex<float> value){
            for(const auto x:{value.real(),value.imag()}) {
                const auto bits=std::bit_cast<std::uint32_t>(x);
                for(unsigned byte=0;byte<4;++byte)*p++=static_cast<std::uint8_t>(bits>>(8*byte));
            }
        }},
};
constexpr const SampleCodec& sample_codec(SampleFormat format) {
    for(const auto& codec:sample_codecs)if(codec.format==format)return codec;
    throw std::invalid_argument("Unknown I/Q sample format");
}
inline SampleFormat sample_format_from_name(std::string_view name) {
    for(const auto& codec:sample_codecs)if(name==codec.datatype)return codec.format;
    throw std::invalid_argument("Unsupported I/Q sample datatype: "+std::string(name));
}
constexpr std::size_t bytes_per_pair(SampleFormat format){return sample_codec(format).pair_bytes;}
constexpr const char* sample_datatype(SampleFormat format){return sample_codec(format).datatype;}
inline std::complex<float> read_iq_pair(const std::uint8_t* p,SampleFormat format){return sample_codec(format).decode(p);}
inline void write_iq_pair(std::uint8_t* output,SampleFormat format,std::complex<float> value) {
    const auto& codec=sample_codec(format);
    if(!codec.encode || !std::isfinite(value.real()) || !std::isfinite(value.imag()))
        throw std::invalid_argument("Unsupported encoder or non-finite I/Q sample");
    codec.encode(output,value);
}
}
