#include "signalscope/adapters/television/dtmb_receiver.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QCoreApplication>
#include <chrono>
#include <iostream>
#include <random>
#include <map>

using namespace signalscope;
using namespace signalscope::television;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void reference(const QStringList& args) {
    // Manual invocation on private, independently generated I/Q recordings.
    // The normal CTest does not download media or require a physical receiver.
    const bool capture=args[1]=="--capture";
    require(capture?(args.size()==7 || args.size()==8):(args.size()==6 || args.size()==7),"--reference INPUT RATE DATATYPE OUTPUT [MAX_ITERATIONS] or --capture INPUT RATE DATATYPE OUTPUT CENTER_HZ [MAX_ITERATIONS]");
    const auto format=sample_format_from_name(args[4].toStdString());
    DtmbReceiveOptions options{{args[3].toUInt(),capture?args[6].toDouble():538000000,format}};options.workers=8;
    if(args.size()==(capture?8:7))options.maximum_iterations=args[capture?7:6].toUInt();
    std::ifstream input(path_from_utf8(args[2].toUtf8().toStdString()),std::ios::binary);
    std::ofstream output(path_from_utf8(args[5].toUtf8().toStdString()),std::ios::binary);
    require(bool(input) && bool(output),"Cannot open reference files");
    DtmbStreamDecoder decoder(options,[&](auto bytes){output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());},
        [](const Json& state){std::cerr<<state.at("state")<<' '<<state.at("input_pairs")<<' '<<state.at("transport_bytes")<<'\n';});
    Bytes bytes(65536*bytes_per_pair(format));std::uint64_t first=0;
    try {while(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || input.gcount()) {
        const auto count=static_cast<std::size_t>(input.gcount());
        decoder.feed({std::span(bytes).first(count),first,options.input,false});first+=count/bytes_per_pair(format);
    }
    decoder.finish();}catch(...){std::cout<<decoder.snapshot().dump(2)<<'\n';throw;}
    const auto state=decoder.snapshot();std::cout<<state.dump(2)<<'\n';
    require(state.at("transport_bytes").get<std::uint64_t>()>0,"Reference produced no transport stream");
    if(capture) {
        const auto& programs=state.at("transport").at("programs");
        require(std::any_of(programs.begin(),programs.end(),[](const auto& row){return row.value("ready",false);}),"No CRC-validated PAT/PMT programme was recovered from the capture");return;
    }
    require(state.at("mode").at("pn_symbols")==945 && state.at("mode").at("system_info")==22 &&
            state.at("mode").at("origin")=="detected","Independent PN945/profile22 was not automatically discovered");
    require(state.at("transport").at("section_crc_errors")==0,"Reference program table CRC errors");
    require(state.at("transport").at("programs").size()==1 && state.at("transport").at("programs")[0].at("program_id")==35,
        "Independent reference's actual program was not recovered");
}
void recoveryReference(const QStringList& args) {
    require(args.size()==5,"--recover-reference INPUT RATE DATATYPE");
    const auto format=sample_format_from_name(args[4].toStdString());DtmbReceiveOptions options{{args[3].toUInt(),538000000,format}};options.workers=6;
    std::map<std::uint64_t,std::uint64_t> output;std::uint64_t epoch=0,first=0;
    DtmbStreamDecoder decoder(options,[&](auto bytes){output[epoch]+=bytes.size();},[&](const Json& state){epoch=state.at("epoch");});
    const auto replay=[&]{
        std::ifstream input(path_from_utf8(args[2].toUtf8().toStdString()),std::ios::binary);require(bool(input),"Cannot open independent recovery reference");
        Bytes bytes(65536*bytes_per_pair(format));
        while(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()) || input.gcount()) {
            const auto size=static_cast<std::size_t>(input.gcount());decoder.feed({std::span(bytes).first(size),first,options.input,false});first+=size/bytes_per_pair(format);
        }
    };
    try {
    replay();
    const auto count=static_cast<std::uint64_t>(options.input.sample_rate*2.4);Bytes zeros(65536*bytes_per_pair(format));
    for(std::size_t i=0;i<65536;++i)write_iq_pair(zeros.data()+i*bytes_per_pair(format),format,{0,0});
    decoder.feed({zeros,first,options.input,false});first+=65536;replay();
    std::mt19937 random(431);std::normal_distribution<float> noise(0,.01);
    for(std::size_t i=0;i<65536;++i)write_iq_pair(zeros.data()+i*bytes_per_pair(format),format,{noise(random),noise(random)});
    for(std::uint64_t offset=0;offset<count;) {
        const auto pairs=std::min<std::uint64_t>(65536,count-offset);decoder.feed({std::span(zeros).first(pairs*bytes_per_pair(format)),first,options.input,false});first+=pairs;offset+=pairs;
    }
    replay();replay();decoder.finish();
    }catch(...){std::cerr<<decoder.snapshot().dump(2)<<'\n';throw;}
    const auto state=decoder.snapshot();std::cout<<state.dump(2)<<'\n';
    require(state.at("input_gaps")==0 && state.at("lock_losses").get<unsigned>()>=2 && state.at("silent_input_pairs")==65536 && output[0]>0 && output[epoch]>0,
        "Loss of the waveform without a source-coordinate gap did not restart acquisition and recover actual TS");
    require(state.at("transport").at("programs").size()==1 && state.at("transport").at("programs")[0].at("program_id")==35,
        "The recovered generation did not discover the actual program again");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        if(app.arguments().contains("--recover-reference")){recoveryReference(app.arguments());return 0;}
        if(app.arguments().size()>1){reference(app.arguments());return 0;}
        const auto nr=dtmbMode(420,3),four=dtmbMode(595,6,"c1"),sixteen=dtmbMode(945,14),sixtyfour=dtmbMode(945,22);
        require(nr.qam=="4qam-nr" && nr.fec_rate==3 && nr.interleaver=="mode1" && nr.codewords==1,"NR profile declaration");
        require(four.qam=="4qam" && four.fec_rate==1 && four.interleaver=="mode2" && four.codewords==1,"C1 profile declaration");
        require(sixteen.qam=="16qam" && sixteen.fec_rate==2 && sixteen.codewords==2,"16QAM profile declaration");
        require(sixtyfour.qam=="64qam" && sixtyfour.fec_rate==2 && sixtyfour.codewords==3,"64QAM profile declaration");
        bool rejected=false;try{dtmbMode(945,22,"c1");}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"An unsupported C1/PN/modulation combination was accepted");
        DtmbReceiveOptions options{{10000000,554000000,SampleFormat::cf32_le}};
        options.pn_symbols=945;options.system_info=22;options.workers=2;
        bool cancelled=true;DtmbStreamDecoder stopped(options,[](auto){}, {},[&]{return cancelled;});
        const auto start=std::chrono::steady_clock::now();rejected=false;
        try{stopped.finish();}catch(const std::runtime_error& error){rejected=std::string(error.what())=="dtmb.cancelled";}
        require(rejected && std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"Cancellation did not interrupt the receiver");
        std::uint64_t transport=0;
        DtmbStreamDecoder noise(options,[&](auto bytes){transport+=bytes.size();});
        Bytes bytes(80000*8);std::mt19937 random(712);std::normal_distribution<float> distribution(0,.1);
        for(std::size_t i=0;i<bytes.size();i+=8)write_iq_pair(bytes.data()+i,SampleFormat::cf32_le,{distribution(random),distribution(random)});
        noise.feed({bytes,40000000,options.input,false});
        noise.feed({bytes,40200000,options.input,true});
        require(noise.snapshot().at("input_gaps")==1 && noise.snapshot().at("epoch")==1,"Source gaps did not reset the receiver generation");
        rejected=false;try{noise.finish();}catch(const std::runtime_error&){rejected=true;}
        require(rejected && transport==0,"Noise or empty transport was reported as a successful decode");
        options.frequency_hz=560000000;rejected=false;
        try{DtmbStreamDecoder invalid(options,[](auto){});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"An entire TV channel outside the input's Nyquist band was accepted");
        std::cout<<"DTMB mode combinations, native pipeline, empty-output rejection, gaps and cancellation passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
