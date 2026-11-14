#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include <bit>
#include <iostream>
using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Bytes floating(float i,float q) {
    Bytes result(8);const float values[]{i,q};
    for(unsigned c=0;c<2;++c){const auto word=std::bit_cast<std::uint32_t>(values[c]);for(unsigned n=0;n<4;++n)result[c*4+n]=word>>(8*n);}
    return result;
}
}
int main() {
    try {
        const auto bytes=floating(.5f,-.25f);
        require(sample_format_from_name("cf32_le")==SampleFormat::cf32_le && bytes_per_pair(SampleFormat::cf32_le)==8,"Float codec metadata mismatch");
        const auto decoded=read_iq_pair(bytes.data(),SampleFormat::cf32_le);
        require(decoded.real()==.5f && decoded.imag()==-.25f,"Little-endian float samples decoded incorrectly");
        const Bytes integer{0,64,0,224};
        require(read_iq_pair(integer.data(),SampleFormat::ci16_le)==decoded,"Codec normalization differs between CI16 and CF32");
        PowerStats floats(SampleFormat::cf32_le),integers(SampleFormat::ci16_le);floats.add(bytes);integers.add(integer);
        require(floats.json().at("mean_power_dbfs")==integers.json().at("mean_power_dbfs"),"Encoding changed physical sample power");
        bool unknown=false,incomplete=false,nonfinite=false;
        try{sample_format_from_name("unknown_future_format");}catch(const std::invalid_argument&){unknown=true;}
        try{floats.add(Bytes(7));}catch(const std::invalid_argument&){incomplete=true;}
        try{const auto invalid=floating(std::numeric_limits<float>::infinity(),0);floats.add(invalid);}catch(const std::invalid_argument&){nonfinite=true;}
        require(unknown && incomplete && nonfinite,"Codec silently interpreted unknown, incomplete or non-finite data");
        Bytes data;for(unsigned n=0;n<2048;++n)data.insert(data.end(),bytes.begin(),bytes.end());
        Spectrometer spectrum(SampleFormat::cf32_le);const auto frame=spectrum.measure(data,0,2000000,100000000);
        require(!frame.bins_dbfs.empty(),"Float samples did not reach spectrum processing");
        std::cout<<"Codec metadata, normalization, validation and spectrum integration passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
