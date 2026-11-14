#include "signalscope/backend/media/protocol.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <numbers>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace signalscope;
using namespace signalscope::media;
namespace {void send(Message value){const auto wire=encode(value);std::cout.write(reinterpret_cast<const char*>(wire.data()),wire.size());std::cout.flush();}}
int main(int argc,char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    unsigned program=101;
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--program")program=std::stoul(argv[i+1]);
    // This is an independently framed media-protocol fixture, not a codec or
    // DTMB validation input. The real-codec acceptance uses actual TS samples.
    if(std::cin.get()==EOF)return 1;
    if(program==303)return 7;
    if(program==404){std::cout<<"invalid framed output";return 0;}
    const auto tracks=Json::array({Json{{"pid",200},{"media_type","video"},{"codec_name","fixture-video"},{"decoder_available",true}},
        Json{{"pid",201},{"media_type","audio"},{"codec_name","fixture-audio"},{"decoder_available",true}},
        Json{{"pid",202},{"stream_type",159},{"codec_name","future-metadata"},{"decoder_available",false}}});
    send({MessageType::catalog,0,{{"programs",Json::array({Json{{"program_id",program},{"name","Fixture"},{"streams",tracks}}})},{"selected_program",program}}});
    const auto start=std::chrono::steady_clock::now();
    for(unsigned index=0;index<40;++index) {
        if(program==505 && index==20)std::this_thread::sleep_for(std::chrono::milliseconds(800));
        Bytes pcm(960*4);
        for(unsigned sample=0;sample<960;++sample) {
            const auto value=static_cast<std::int16_t>(3000*std::sin(2*std::numbers::pi*1000*(index*960+sample)/48000.));
            for(unsigned channel=0;channel<2;++channel){const auto at=sample*4+channel*2;pcm[at]=value&255;pcm[at+1]=static_cast<std::uint16_t>(value)>>8;}
        }
        const auto pts=1000000+index*20000;
        send({MessageType::audio,pts,{{"format","s16_le"},{"sample_rate",48000},{"channels",2},{"frames",960},{"pid",201}},std::move(pcm)});
        if(index%2==0) {
            Bytes rgb(640*360*3);for(std::size_t at=0;at<rgb.size();at+=3){rgb[at]=program==202?200:16;rgb[at+1]=64;rgb[at+2]=128;}
            send({MessageType::video,pts,{{"width",640},{"height",360},{"stride",640*3},{"format","rgb24"},{"duration_us",40000},
                {"sample_aspect_num",1},{"sample_aspect_den",1},{"pid",200}},std::move(rgb)});
        }
        std::this_thread::sleep_until(start+std::chrono::milliseconds((index+1)*20));
    }
    if(program==606) {
        // Audio has deliberately ended before this last video frame. This is
        // different from the mid-program outage above, which later resumes.
        std::this_thread::sleep_for(std::chrono::milliseconds(800));Bytes rgb(640*360*3,64);
        send({MessageType::video,2600000,{{"width",640},{"height",360},{"stride",640*3},{"format","rgb24"},{"duration_us",40000}},std::move(rgb)});
    }
    send({MessageType::ended,0,{{"video_frames",program==606?21:20},{"audio_frames",38400},{"decode_errors",0}}});return 0;
}
