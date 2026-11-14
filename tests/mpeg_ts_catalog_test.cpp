#include "signalscope/backend/transport/mpeg_ts.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
using namespace signalscope;
using namespace signalscope::transport;
namespace {
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void put(Bytes& out,unsigned value){out.push_back(value>>8);out.push_back(value&255);}
// Independent transmitter CRC in long division form. A published CRC-32/MPEG-2
// check vector below separately anchors the wire polynomial and initialization.
void seal(Bytes& input) {
    std::uint64_t reg=0xffffffff;
    for(auto byte:input) {
        reg^=std::uint64_t(byte)<<24;
        for(unsigned i=0;i<8;++i){reg<<=1;if(reg&(1ULL<<32))reg^=0x104c11db7ULL;}
    }
    for(int i=3;i>=0;--i)input.push_back((reg>>(8*i))&255);
}
Bytes section(unsigned table,unsigned extension,unsigned version,unsigned number,unsigned last,const Bytes& body) {
    Bytes out{static_cast<std::uint8_t>(table),static_cast<std::uint8_t>(0xb0|((body.size()+9)>>8)),static_cast<std::uint8_t>(body.size()+9)};
    put(out,extension);out.push_back(0xc1|((version&31)<<1));out.push_back(number);out.push_back(last);
    out.insert(out.end(),body.begin(),body.end());seal(out);return out;
}
Bytes packet(unsigned id,unsigned counter,bool starts,Bytes payload,bool gap=false) {
    require(payload.size()<=184,"Oversized fixture packet");Bytes out(188,0xff);out[0]=0x47;out[1]=(id>>8)|(starts?0x40:0);out[2]=id&255;
    std::size_t offset=4;
    if(payload.size()<184){out[3]=0x30|(counter&15);const auto length=183-payload.size();out[4]=static_cast<std::uint8_t>(length);if(length)out[5]=gap?0x80:0;offset=5+length;}
    else out[3]=0x10|(counter&15);
    std::copy(payload.begin(),payload.end(),out.begin()+static_cast<std::ptrdiff_t>(offset));return out;
}
Bytes psi(unsigned id,unsigned cc,Bytes body){body.insert(body.begin(),0);return packet(id,cc,true,std::move(body));}
void offer(MpegTsCatalog& catalog,const Bytes& data){catalog.feed(data);const auto padding=packet(0x1fff,0,false,Bytes(184,0xff));catalog.feed(padding);catalog.feed(padding);}
Bytes patBody(unsigned program,unsigned pmt){Bytes body;put(body,program);put(body,0xe000|pmt);return body;}
Bytes pmtBody(unsigned pcr,unsigned streamType,unsigned elementary,const Bytes& description={}) {
    Bytes body;put(body,0xe000|pcr);put(body,0xf000);body.push_back(streamType);put(body,0xe000|elementary);put(body,0xf000|description.size());body.insert(body.end(),description.begin(),description.end());return body;
}
Bytes sdtBody(unsigned service,const std::string& provider,const std::string& name) {
    Bytes description{0x48,static_cast<std::uint8_t>(3+provider.size()+name.size()),1,static_cast<std::uint8_t>(provider.size())};
    description.insert(description.end(),provider.begin(),provider.end());description.push_back(static_cast<std::uint8_t>(name.size()));description.insert(description.end(),name.begin(),name.end());
    Bytes body{0,1,0xff};put(body,service);body.push_back(0xfc);put(body,0x8000|description.size());body.insert(body.end(),description.begin(),description.end());return body;
}
std::string text(std::span<const std::uint8_t> input){if(!input.empty() && input[0]==0x15)input=input.subspan(1);return {input.begin(),input.end()};}
}
int main(int argc,char** argv) {
    try {
        if(argc>1) {
            MpegTsCatalog catalog(text);std::ifstream input(path_from_utf8(argv[1]),std::ios::binary);require(bool(input),"Cannot open TS reference");
            Bytes block(12347);while(input){input.read(reinterpret_cast<char*>(block.data()),block.size());catalog.feed(std::span(block).first(static_cast<std::size_t>(input.gcount())));}catalog.finish();
            std::cout<<catalog.snapshot().dump(2)<<'\n';return 0;
        }
        const std::string check="123456789";require(mpegSectionCrc({reinterpret_cast<const std::uint8_t*>(check.data()),check.size()})==0x0376e6e7,"Wrong MPEG section CRC convention");
        MpegTsCatalog catalog(text);
        // Names may precede PAT. Non-ASCII bytes are delegated to the supplied
        // broadcast text codec, rather than guessed in the transport parser.
        offer(catalog,psi(0x11,0,section(0x42,1,0,0,0,sdtBody(101,"Fixture",std::string("\x15")+"Test TV"))));
        offer(catalog,psi(0,0,section(0,1,0,0,1,patBody(101,0x100))));
        require(catalog.snapshot().at("programs").empty(),"An incomplete multi-section PAT replaced the program table");
        offer(catalog,psi(0,1,section(0,1,0,1,1,patBody(202,0x101))));
        require(catalog.snapshot().at("programs").size()==2,"Complete PAT did not enumerate both programs");
        offer(catalog,psi(0x100,0,section(2,101,0,0,0,pmtBody(0x200,0x1b,0x200))));
        // A future elementary-stream declaration is preserved without any
        // codec/receiver ID branch in this stable transport consumer.
        offer(catalog,psi(0x101,0,section(2,202,0,0,0,pmtBody(0x201,0x9f,0x201,{5,4,'T','E','S','T'}))));
        auto result=catalog.snapshot();require(result.at("programs")[0].at("ready")==true && result.at("programs")[0].at("name")=="Test TV","Valid service name or PMT was lost");
        require(result.at("transport_stream_id")==1 && result.at("original_network_id")==1,"Broadcast service identity lost its transport/network tuple");
        require(result.at("programs")[1].at("streams")[0].at("stream_type")==0x9f && result.at("programs")[1].at("streams")[0].at("descriptors")[0].at("data")==Json(Bytes{'T','E','S','T'}),"An unknown stream's metadata was hidden");
        auto encrypted=packet(0x201,0,false,Bytes(184,0));encrypted[3]|=0x80;offer(catalog,encrypted);
        require(catalog.snapshot().at("programs")[1].at("streams")[0].at("scrambled_packets")==1,
            "An encrypted elementary stream was presented without its transport evidence");
        auto damaged=section(2,101,1,0,0,pmtBody(0x202,2,0x202));damaged.back()^=1;
        offer(catalog,psi(0x100,1,damaged));result=catalog.snapshot();require(result.at("section_crc_errors")==1 && result.at("programs")[0].at("pcr_pid")==0x200,"Bad CRC changed an established program");
        auto malformed=pmtBody(0x202,2,0x202);malformed.back()=100;
        offer(catalog,psi(0x100,2,section(2,101,1,0,0,malformed)));result=catalog.snapshot();require(result.at("malformed_packets_or_sections")==1 && result.at("programs")[0].at("pcr_pid")==0x200,"Malformed PMT partially changed its program");
        // Arbitrary byte chunks, leading garbage, split PSI sections and
        // duplicate packets must retain source framing and not invent loss.
        MpegTsCatalog split;const auto whole=section(0,7,0,0,0,patBody(303,0x103));Bytes first(whole.begin(),whole.begin()+9);first.insert(first.begin(),0);
        const auto a=packet(0,0,true,first),b=packet(0,1,false,Bytes(whole.begin()+9,whole.end()));
        Bytes wire{1,2,3,4,5};for(const auto& p:{a,b,b,packet(0x1fff,7,false,Bytes(184,0xff))})wire.insert(wire.end(),p.begin(),p.end());
        for(std::size_t i=0;i<wire.size();i+=17)split.feed(std::span(wire).subspan(i,std::min<std::size_t>(17,wire.size()-i)));split.finish();
        result=split.snapshot();require(result.at("programs").size()==1 && result.at("discarded_bytes")==5 && result.at("continuity_errors")==0 && result.at("duplicate_packets")==1,"Arbitrary packet chunks, duplicate CC or PSI reassembly are incorrect");
        split.discontinuity();offer(split,a);offer(split,packet(0,3,false,Bytes(whole.begin()+9,whole.end())));
        result=split.snapshot();require(result.at("programs").empty() && result.at("continuity_errors")==1 && result.at("input_discontinuities")==1,"PSI bytes were concatenated across a gap");
        offer(split,psi(0,4,whole));require(split.snapshot().at("programs").size()==1,"Program discovery did not recover after a gap");
        auto next=patBody(404,0x104);offer(split,psi(0,5,section(0,7,1,0,0,next)));require(split.snapshot().at("programs")[0].at("program_id")==404,"PAT version change left a removed program active");
        offer(catalog,psi(0,2,section(0,8,0,0,0,patBody(101,0x100))));
        require(catalog.snapshot().at("programs")[0].at("ready")==false,
            "A different transport stream reused an unrelated program's PMT");
        std::cout<<"TS framing, CRC, transactional PAT/PMT/SDT, unknown streams, chunking, continuity and recovery passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
