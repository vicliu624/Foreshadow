#include "signalscope/backend/transport/mpeg_ts.hpp"
#include <array>
#include <map>
#include <optional>
#include <set>

namespace signalscope::transport {
Bytes completePackets(std::span<const std::uint8_t> bytes) {
    std::size_t first=0;
    while(first+3*tsPacketBytes<=bytes.size() && !(bytes[first]==0x47 && bytes[first+188]==0x47 && bytes[first+376]==0x47))++first;
    if(first+3*tsPacketBytes>bytes.size())return {};
    auto last=first;
    while(last+tsPacketBytes<=bytes.size() && bytes[last]==0x47)last+=tsPacketBytes;
    return {bytes.begin()+static_cast<std::ptrdiff_t>(first),bytes.begin()+static_cast<std::ptrdiff_t>(last)};
}
std::uint32_t mpegSectionCrc(std::span<const std::uint8_t> bytes) {
    std::uint32_t value=0xffffffff;
    for(const auto byte:bytes) {
        value^=std::uint32_t(byte)<<24;
        for(unsigned i=0;i<8;++i)value=value&0x80000000?(value<<1)^0x04c11db7:value<<1;
    }
    return value;
}
namespace {
unsigned word(std::span<const std::uint8_t> bytes,std::size_t at){return (unsigned(bytes[at])<<8)|bytes[at+1];}
unsigned pid(std::span<const std::uint8_t> bytes,std::size_t at){return word(bytes,at)&0x1fff;}
struct Fragments {
    int extension=-1,version=-1,last=-1;
    std::map<unsigned,Bytes> parts;
    bool add(std::span<const std::uint8_t> section) {
        const auto id=static_cast<int>(word(section,3));
        const auto v=(section[5]>>1)&31;
        const auto tail=section[7];
        if(id!=extension || v!=version || tail!=last){parts.clear();extension=id;version=v;last=tail;}
        parts[section[6]]=Bytes(section.begin(),section.end());
        return parts.size()==static_cast<std::size_t>(last+1);
    }
};
struct SectionAssembly {Bytes bytes;std::size_t target=0;};
struct Continuity {int counter=-1;std::array<std::uint8_t,188> previous{};};
struct Service {Bytes name,provider;};
struct Program {unsigned number=0,pmt=0;int version=-1;unsigned pcr=0x1fff;Json descriptors=Json::array(),streams=Json::array();Bytes section;};
Json descriptors(std::span<const std::uint8_t> input) {
    Json result=Json::array();
    while(!input.empty()) {
        if(input.size()<2 || std::size_t(input[1])+2>input.size())throw std::invalid_argument("Invalid TS descriptor length");
        const auto length=input[1];const auto body=input.subspan(2,length);
        result.push_back({{"tag",input[0]},{"data",Bytes(body.begin(),body.end())}});
        input=input.subspan(2+length);
    }
    return result;
}
bool conditionalAccess(const Json& descriptors) {
    return std::any_of(descriptors.begin(),descriptors.end(),[](const auto& value){return value.at("tag")==0x09;});
}
}
class MpegTsCatalog::Impl {
public:
    explicit Impl(BroadcastTextDecoder decoder):text(std::move(decoder)){}
    void feed(std::span<const std::uint8_t> bytes) {
        inputBytes+=bytes.size();
        while(!bytes.empty()) {
            const auto count=std::min<std::size_t>(564-pending.size(),bytes.size());
            pending.insert(pending.end(),bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count));bytes=bytes.subspan(count);
            parse(false);
        }
    }
    void parse(bool final) {
        while(!pending.empty()) {
            if(!aligned) {
                const auto required=final?188u:377u;
                if(pending.size()<required)return;
                std::size_t start=0;
                while(start+required<=pending.size() && !(pending[start]==0x47 &&
                      (final || (pending[start+188]==0x47 && pending[start+376]==0x47))))++start;
                if(start){discardedBytes+=start;pending.erase(pending.begin(),pending.begin()+static_cast<std::ptrdiff_t>(start));}
                if(pending.size()<required)return;
                aligned=true;
            }
            if(pending.size()<188)return;
            if(pending[0]!=0x47){aligned=false;++syncLosses;assemblies.clear();resetCounters();continue;}
            packet(std::span(pending).first(188));pending.erase(pending.begin(),pending.begin()+188);
        }
    }
    void packet(std::span<const std::uint8_t> bytes) {
        ++packets;const auto id=pid(bytes,1);const auto control=(bytes[3]>>4)&3;
        if(!control){++malformed;assemblies.erase(id);return;}
        if(bytes[1]&0x80){++transportErrors;assemblies.erase(id);continuities[id].counter=-1;return;}
        std::size_t offset=4;bool gap=false;
        if(control&2) {
            const auto length=bytes[4];
            if((control==2 && length!=183) || (control==3 && length>182)){++malformed;assemblies.erase(id);return;}
            if(length && (bytes[5]&0x80)){gap=true;assemblies.erase(id);continuities[id].counter=-1;}
            offset=5+length;
        }
        if(!(control&1) || offset==188 || id==0x1fff)return;
        auto& state=continuities[id];const auto counter=bytes[3]&15;
        if(state.counter>=0 && counter==state.counter && std::equal(bytes.begin(),bytes.end(),state.previous.begin())){++duplicates;return;}
        if(state.counter>=0 && counter!=((state.counter+1)&15)) {
            ++continuityErrors;gap=true;assemblies.erase(id);
        }
        state.counter=counter;std::copy(bytes.begin(),bytes.end(),state.previous.begin());
        if(bytes[3]&0xc0){++scrambled;++scrambledByPid[id];assemblies.erase(id);return;}
        const auto knownPmt=std::any_of(programs.begin(),programs.end(),[&](const auto& item){return item.second.pmt==id;});
        if(id!=0 && id!=0x11 && !knownPmt)return;
        auto payload=bytes.subspan(offset);const auto starts=bool(bytes[1]&0x40);
        if(starts) {
            const auto pointer=payload[0];payload=payload.subspan(1);
            if(pointer>payload.size()){++malformed;assemblies.erase(id);return;}
            if(!gap && pointer && assemblies.contains(id))append(id,payload.first(pointer),false);
            assemblies.erase(id);payload=payload.subspan(pointer);
            append(id,payload,true);
        }else if(!gap && assemblies.contains(id))append(id,payload,false);
    }
    void append(unsigned id,std::span<const std::uint8_t> bytes,bool starts) {
        auto& assembly=assemblies[id];
        while(!bytes.empty()) {
            if(assembly.bytes.empty() && (!starts || bytes[0]==0xff))return;
            if(assembly.bytes.size()<3) {
                const auto count=std::min<std::size_t>(3-assembly.bytes.size(),bytes.size());
                assembly.bytes.insert(assembly.bytes.end(),bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count));bytes=bytes.subspan(count);
                if(assembly.bytes.size()<3)return;
                assembly.target=3+((assembly.bytes[1]&15)<<8)+assembly.bytes[2];
                if(assembly.target<12 || assembly.target>4096){++malformed;assembly={};return;}
            }
            const auto count=std::min(assembly.target-assembly.bytes.size(),bytes.size());
            assembly.bytes.insert(assembly.bytes.end(),bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(count));bytes=bytes.subspan(count);
            if(assembly.bytes.size()<assembly.target)return;
            const auto completed=std::move(assembly.bytes);assembly={};
            section(id,completed);starts=true;
        }
    }
    void section(unsigned id,std::span<const std::uint8_t> bytes) {
        if(mpegSectionCrc(bytes)){++crcErrors;return;}
        if(!(bytes[1]&0x80) || !(bytes[5]&1) || bytes[6]>bytes[7])return;
        try {
            if(id==0 && bytes[0]==0) {
                if((bytes.size()-12)%4)throw std::invalid_argument("Invalid PAT size");
                if(!pat.add(bytes))return;
                if(pat.parts==acceptedPat)return;
                std::map<unsigned,Program> next;
                for(const auto& [number,part]:pat.parts)for(std::size_t i=8;i+4<=part.size()-4;i+=4) {
                    const auto program=word(part,i);if(!program)continue;const auto pmt=pid(part,i+2);
                    if(pmt==0 || pmt==0x11 || pmt==0x1fff || next.size()>=1024 || next.contains(program))throw std::invalid_argument("Invalid or excessive PAT programs");
                    const auto old=programs.find(program);
                    next[program]=transportId==pat.extension && old!=programs.end() && old->second.pmt==pmt
                        ?old->second:Program{program,pmt};
                }
                if(transportId!=pat.extension && sdtTransportId!=pat.extension)services.clear();
                transportId=pat.extension;patVersion=pat.version;programs=std::move(next);acceptedPat=pat.parts;++revision;
            }else if(bytes[0]==2) {
                const auto program=word(bytes,3);const auto found=programs.find(program);
                if(found==programs.end() || found->second.pmt!=id)return;
                if(std::equal(bytes.begin(),bytes.end(),found->second.section.begin(),found->second.section.end()))return;
                if(bytes.size()<16 || bytes[6] || bytes[7])throw std::invalid_argument("Invalid PMT header");
                const auto infoLength=word(bytes,10)&0xfff;std::size_t offset=12+infoLength,end=bytes.size()-4;
                if(offset>end)throw std::invalid_argument("Invalid PMT information length");
                auto next=found->second;next.descriptors=descriptors(bytes.subspan(12,infoLength));next.pcr=pid(bytes,8);next.version=(bytes[5]>>1)&31;next.streams=Json::array();
                std::set<unsigned> seen;
                while(offset<end) {
                    if(end-offset<5)throw std::invalid_argument("Incomplete PMT stream");
                    const auto length=word(bytes,offset+3)&0xfff,streamPid=pid(bytes,offset+1);
                    if(offset+5+length>end || streamPid==0x1fff || !seen.insert(streamPid).second)throw std::invalid_argument("Invalid PMT stream length or PID");
                    next.streams.push_back({{"pid",streamPid},{"stream_type",bytes[offset]},
                        {"descriptors",descriptors(bytes.subspan(offset+5,length))}});offset+=5+length;
                }
                next.section=Bytes(bytes.begin(),bytes.end());found->second=std::move(next);++revision;
            }else if(id==0x11 && bytes[0]==0x42) {
                if(bytes.size()<15)return;
                if(!sdt.add(bytes))return;
                if(sdt.parts==acceptedSdt)return;
                std::map<unsigned,Service> next;
                for(const auto& [number,part]:sdt.parts) {
                    std::size_t offset=11;const auto end=part.size()-4;
                    while(offset<end) {
                        if(end-offset<5)throw std::invalid_argument("Incomplete SDT service");
                        const auto service=word(part,offset),length=word(part,offset+3)&0xfff;
                        if(offset+5+length>end)throw std::invalid_argument("Invalid SDT service length");
                        const auto list=descriptors(std::span(part).subspan(offset+5,length));
                        for(const auto& item:list)if(item.at("tag")==0x48) {
                            const auto data=item.at("data").get<Bytes>();
                            if(data.size()<3 || 2u+data[1]>=data.size())throw std::invalid_argument("Invalid service descriptor");
                            const auto nameAt=std::size_t(2+data[1]);
                            const auto nameLength=data[nameAt];
                            if(nameAt+1+nameLength!=data.size())throw std::invalid_argument("Invalid service name length");
                            next[service]={{data.begin()+static_cast<std::ptrdiff_t>(nameAt+1),data.end()},
                                {data.begin()+2,data.begin()+2+data[1]}};
                        }
                        offset+=5+length;
                    }
                }
                if(transportId<0 || transportId==sdt.extension){services=std::move(next);sdtTransportId=sdt.extension;originalNetworkId=word(sdt.parts.begin()->second,8);sdtVersion=sdt.version;acceptedSdt=sdt.parts;++revision;}
            }
        }catch(const std::invalid_argument&){++malformed;}
    }
    void discontinuity() {
        ++inputGaps;pending.clear();aligned=false;assemblies.clear();resetCounters();pat={};sdt={};
        programs.clear();services.clear();scrambledByPid.fill(0);transportId=patVersion=sdtVersion=sdtTransportId=-1;
        acceptedPat.clear();acceptedSdt.clear();++revision;
    }
    void resetCounters(){for(auto& state:continuities)state.counter=-1;}
    Json snapshot() const {
        Json rows=Json::array();
        for(const auto& [id,program]:programs) {
            Json row={{"program_id",id},{"pmt_pid",program.pmt},{"pcr_pid",program.pcr},{"pmt_version",program.version},
                {"ready",program.version>=0},{"descriptors",program.descriptors},{"streams",program.streams}};
            row["conditional_access"]=conditionalAccess(program.descriptors);
            for(auto& stream:row["streams"]) {
                stream["scrambled_packets"]=scrambledByPid[stream.at("pid").template get<unsigned>()];
                stream["conditional_access"]=conditionalAccess(program.descriptors) || conditionalAccess(stream.at("descriptors"));
            }
            if(const auto service=services.find(id);service!=services.end() && transportId==sdtTransportId) {
                row["name_raw"]=service->second.name;row["provider_raw"]=service->second.provider;
                if(text){row["name"]=text(service->second.name);row["provider"]=text(service->second.provider);}
            }
            rows.push_back(std::move(row));
        }
        return {{"revision",revision},{"transport_stream_id",transportId},{"original_network_id",transportId==sdtTransportId?originalNetworkId:-1},{"pat_version",patVersion},{"sdt_version",sdtVersion},{"programs",std::move(rows)},
            {"input_bytes",inputBytes},{"packets",packets},{"discarded_bytes",discardedBytes},{"malformed_packets_or_sections",malformed},
            {"transport_error_packets",transportErrors},{"scrambled_packets",scrambled},{"continuity_errors",continuityErrors},
            {"duplicate_packets",duplicates},{"section_crc_errors",crcErrors},{"sync_losses",syncLosses},{"input_discontinuities",inputGaps}};
    }
    BroadcastTextDecoder text;
    Bytes pending;bool aligned=false;
    std::array<Continuity,8192> continuities{};
    std::array<std::uint64_t,8192> scrambledByPid{};
    std::map<unsigned,SectionAssembly> assemblies;
    Fragments pat,sdt;int transportId=-1,patVersion=-1,sdtVersion=-1,sdtTransportId=-1,originalNetworkId=-1;
    std::map<unsigned,Bytes> acceptedPat,acceptedSdt;std::uint64_t revision=0;
    std::map<unsigned,Program> programs;std::map<unsigned,Service> services;
    std::uint64_t inputBytes=0,packets=0,discardedBytes=0,malformed=0,transportErrors=0,scrambled=0,
        continuityErrors=0,duplicates=0,crcErrors=0,syncLosses=0,inputGaps=0;
};
MpegTsCatalog::MpegTsCatalog(BroadcastTextDecoder text):impl_(std::make_unique<Impl>(std::move(text))){}
MpegTsCatalog::~MpegTsCatalog()=default;
void MpegTsCatalog::feed(std::span<const std::uint8_t> bytes){impl_->feed(bytes);}
void MpegTsCatalog::finish(){impl_->parse(true);impl_->discardedBytes+=impl_->pending.size();impl_->pending.clear();}
void MpegTsCatalog::discontinuity(){impl_->discontinuity();}
Json MpegTsCatalog::snapshot() const{return impl_->snapshot();}
std::uint64_t MpegTsCatalog::revision() const{return impl_->revision;}
}
