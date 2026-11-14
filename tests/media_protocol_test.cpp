#include "signalscope/backend/media/protocol.hpp"
#include <iostream>
using namespace signalscope;
using namespace signalscope::media;
namespace {void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}}
int main() {
    try {
        // A literal, independently written little-endian header, including a
        // negative timestamp, validates both parser and serializer boundaries.
        const Bytes literal{'A','M','S','M',1,0,4,0,2,0,0,0,4,0,0,0,0xfe,0xff,0xff,0xff,0xff,0xff,0xff,0xff,'{','}',0,1,2,3};
        std::vector<Message> messages;MessageParser parser([&](Message value){messages.push_back(std::move(value));});
        for(const auto byte:literal)parser.feed({&byte,1});parser.finish();
        require(messages.size()==1 && messages[0].type==MessageType::audio && messages[0].pts_us==-2 && messages[0].payload==Bytes({0,1,2,3}),"Literal IPC frame was not decoded");
        require(encode(messages[0])==literal,"IPC header or payload byte order is incorrect");
        const auto first=encode({MessageType::catalog,77,{{"programs",Json::array()}}});
        auto joined=first;joined.insert(joined.end(),literal.begin(),literal.end());
        messages.clear();parser.feed(joined);parser.finish();require(messages.size()==2 && messages[0].pts_us==77,"Joined message boundaries were lost");
        for(const auto [offset,value]:std::array<std::pair<std::size_t,std::uint8_t>,4>{{{0,'X'},{4,2},{6,99},{11,127}}}) {
            auto bad=literal;bad[offset]=value;bool rejected=false;
            try{MessageParser invalid([](auto){});invalid.feed(bad);}catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"Malformed, incompatible or oversized IPC was accepted");
        }
        bool rejected=false;try{MessageParser truncated([](auto){});truncated.feed(std::span(literal).first(27));truncated.finish();}
        catch(const std::invalid_argument&){rejected=true;}require(rejected,"Truncated payload was reported as a complete frame");
        rejected=false;try{encode({MessageType::status,0,Json::array()});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Unstructured IPC metadata was accepted");
        std::cout<<"Media protocol v1 literals, byte chunks, message boundaries, budgets and version rejection passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
