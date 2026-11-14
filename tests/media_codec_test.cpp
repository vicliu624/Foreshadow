#include "src/adapters/media/codec_provider.hpp"
#include <iostream>
using namespace signalscope;
using namespace signalscope::media;
namespace signalscope::media {CodecProvider mediaFixtureProvider();}
namespace {void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}}
int main() {
    AVCodecParameters* parameters=avcodec_parameters_alloc();
    try {
        require(parameters,"Codec parameters allocation failed");
        EncodedTrack future{parameters,{1,90000},{{"registration","FUTR"}}};
        require(!resolveCodecProvider(future),"An unknown codec was silently treated as a built-in codec");
        registerCodecProvider(mediaFixtureProvider());const auto provider=resolveCodecProvider(future);
        require(provider && provider->describe(future).at("codec_name")=="future-audio","Additional provider did not enter the actual codec resolver");
        const auto localized=provider->diagnostic("fixture.failure",Json::object()).at("message");
        require(localized.is_object() && localized.at("fr")=="Échec du décodeur indépendant.","An additional locale did not travel with its independent codec provider");
        const auto fallback=provider->diagnostic("fixture.fallback",Json::object()).at("message");
        require(fallback.is_object() && fallback.at("en")=="English fallback for the independent codec.","A missing provider translation lost its English fallback");
        bool decoded=false;auto decoder=provider->create(future,[&](const AVFrame& frame,std::int64_t pts){decoded=frame.nb_samples==16 && pts==987654 && frame.data[0][0]==0x34;});
        AVPacket packet{};packet.size=1;decoder->send(&packet);require(decoded,"Additional codec could not deliver a decoded frame through the shared protocol");
        const auto descriptors=Json::array({Json{{"tag",5},{"data",Bytes{'D','R','A','1'}}}});
        EncodedTrack dra{parameters,{1,90000},{{"stream_type",0x06},{"descriptors",descriptors}}};
        const auto audio=resolveCodecProvider(dra);require(audio && audio->describe(dra).at("media_type")=="audio","Registration descriptor did not select the DRA provider");
        std::uint64_t frames=0;auto draDecoder=audio->create(dra,[&](const AVFrame&,std::int64_t){++frames;});
        Bytes garbage(40000,0x42);packet.data=garbage.data();packet.size=garbage.size();packet.pts=AV_NOPTS_VALUE;draDecoder->send(&packet);
        require(frames==0,"Non-DRA data produced audio frames");
        bool rejected=false;packet.size=2*1024*1024;
        try{draDecoder->send(&packet);}catch(const std::runtime_error&){rejected=true;}require(rejected,"DRA packet allocation budget was not enforced before reading input");
        avcodec_parameters_free(&parameters);
        std::cout<<"Independent codec registration, generic resolution, unknown tracks and DRA input bounds passed\n";return 0;
    }catch(const std::exception& error){avcodec_parameters_free(&parameters);std::cerr<<error.what()<<'\n';return 1;}
}
