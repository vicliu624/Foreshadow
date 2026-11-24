#include "plugins/adsb/core/demod.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <iostream>
int main(int argc,char** argv) {
    using namespace signalscope;if(argc!=4){std::cerr<<"path datatype sample_rate\n";return 2;}
    const auto format=sample_format_from_name(argv[2]);const auto rate=static_cast<std::uint32_t>(std::stoul(argv[3]));const auto stride=bytes_per_pair(format);
    std::ifstream file(path_from_utf8(argv[1]),std::ios::binary);if(!file)throw std::runtime_error("Cannot open validation input");
    adsb::Demodulator demod(rate);Bytes bytes(8192*stride);std::vector<std::complex<float>> samples;std::uint64_t first=0,frames=0;
    while(file){file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));const auto count=static_cast<std::size_t>(file.gcount());if(count%stride)throw std::runtime_error("Incomplete validation I/Q pair");samples.clear();for(std::size_t at=0;at<count;at+=stride)samples.push_back(read_iq_pair(bytes.data()+at,format));
        demod.feed(samples,first,false,[&](const adsb::Frame& frame){++frames;if(frames<=20)std::cout<<frame.first_sample<<' '<<frame.fields.hex<<' '<<frame.fields.callsign<<'\n';});first+=samples.size();}
    std::cout<<"input_pairs="<<first<<" crc_valid_frames="<<frames<<'\n';
}
