#include "signalscope/backend/iq/signal_source.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/dsp/waveform.hpp"
#include <iostream>
#include <numbers>
#include <thread>

using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejected(F action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"Cannot read source fixture");return {std::istreambuf_iterator<char>(input),{}};}
void write(const std::filesystem::path& file,const std::string& value){std::ofstream output(file,std::ios::binary);output.write(value.data(),static_cast<std::streamsize>(value.size()));require(bool(output),"Cannot write source fixture");}
SignalWindow truth(SampleFormat format) {
    SignalWindow window;window.stream={1000000,433920000,format};window.source_id="independent.capture";
    window.processing=Json::array({{{"method","independent-truth"},{"requested",{{"gain_mode","manual"}}},{"applied",{{"gain_tenths",197}}},{"future_field",{{"value",17},{"meaning","unknown"}}}}});
    constexpr auto origin=(1ULL<<54)+91;std::vector<std::uint8_t> bytes(bytes_per_pair(format));
    const auto segment=[&](std::uint64_t first,std::size_t count,bool discontinuous){SignalSegment result;result.first_sample=first;result.discontinuous_before=discontinuous;
        for(std::size_t n=0;n<count;++n){const auto value=std::polar(.62,.29+2*std::numbers::pi*.071*((first-origin+n)%10000));write_iq_pair(bytes.data(),format,{float(value.real()),float(value.imag())});result.samples.push_back(read_iq_pair(bytes.data(),format));}return result;};
    window.segments={segment(origin,257,false),segment(origin+4096,96,true),segment(origin+4192,33,true)};return window;
}
void archiveRoundtrip(const std::filesystem::path& root,SampleFormat format) {
    const auto original=truth(format);const auto directory=root/path_from_utf8(std::string("捕获 📡-")+sample_datatype(format));const auto metadata=write_signal_archive(directory,original);
    const auto info=inspect_signal_source(metadata);require(info.storage.format==SampleFormat::cf32_le && info.stream.format==format && info.storage.pairs==386,"Archive confused normalized storage and original input encoding");
    require(info.source_id==original.source_id && info.time_origin==original.first_sample() && info.storage.metadata.at("absolute_arrival_utc").is_null(),"Archive invented an arrival time or lost source identity");
    const auto restored=read_signal_source(info,0,386);require(restored.processing==original.processing && restored.segments.size()==3,"Archive lost processing, unknown metadata or source segments");
    for(std::size_t n=0;n<3;++n){const auto& expected=original.segments[n];const auto& actual=restored.segments[n];require(actual.first_sample==expected.first_sample && actual.discontinuous_before==expected.discontinuous_before && actual.samples==expected.samples,"Normalized archive changed samples, gaps or a source position beyond 2^53");}
    const auto subset=read_signal_window(metadata,250,80);require(subset.segments.size()==2 && subset.segments[0].samples.size()==7 && subset.segments[1].samples.size()==73 && subset.segments[1].discontinuous_before,"Stored-sample range selection did not preserve a gap");
    require(info.stored_index(original.first_sample()+4100)==261 && !info.stored_index(original.first_sample()+300),"Source-clock to stored-sample mapping invented a position in a gap");
    const auto analysis=analyze_waveform(std::make_shared<SignalWindow>(restored));require(std::isnan(analysis.trace("frequency").values[257]) && std::isnan(analysis.trace("frequency").values[353]),"Archive reopening bridged a known or unquantified discontinuity");
    const auto manifest=read(metadata),samples=read(info.storage.data_path);rejected([&]{write_signal_archive(directory,original);},"Saving an archive overwrote an existing directory");require(read(metadata)==manifest && read(info.storage.data_path)==samples,"Rejected save modified existing user data");
    auto invalid=info.storage.metadata;invalid["version"]=2;write_json(metadata,invalid);rejected([&]{inspect_signal_source(metadata);},"Unknown archive version was accepted");
    invalid=info.storage.metadata;invalid["storage"]["file"]="../unrelated.cf32";write_json(metadata,invalid);rejected([&]{inspect_signal_source(metadata);},"Archive data escaped its declared storage directory");
    invalid=info.storage.metadata;invalid["segments"][1]["first_source_sample"]=std::to_string(original.first_sample()+20);write_json(metadata,invalid);rejected([&]{inspect_signal_source(metadata);},"Archive overlapping source positions were accepted");
    write(metadata,manifest);write(info.storage.data_path,samples.substr(0,samples.size()-1));rejected([&]{inspect_signal_source(metadata);},"Truncated archive data was accepted");write(info.storage.data_path,samples);
    rejected([&]{read_signal_source(info,0,386,{},16);},"Archive read exceeded the normalized payload budget");
    std::stop_source canceled;canceled.request_stop();require(read_signal_source(info,0,386,canceled.get_token()).segments.empty(),"Canceled archive read returned partial samples");
    const auto canceledDirectory=root/(std::string("canceled-")+sample_datatype(format));rejected([&]{write_signal_archive(canceledDirectory,original,canceled.get_token());},"A canceled archive write succeeded");require(!std::filesystem::exists(canceledDirectory),"Pre-canceled archive write created files");
}
void independentReader(const std::filesystem::path& root) {
    const auto path=root/path_from_utf8("独立 📡.independent-iq");const Json values={{"first","18014398509482077"},{"count",4}};write_json(path,values);
    const std::vector<std::complex<float>> iq{{.2f,-.1f},{.4f,-.3f},{.6f,-.5f},{.8f,-.7f}};const auto stride=bytes_per_pair(SampleFormat::cf32_le);Bytes bytes(iq.size()*stride);
    for(std::size_t n=0;n<iq.size();++n)write_iq_pair(bytes.data()+n*stride,SampleFormat::cf32_le,iq[n]);std::ofstream payload(root/"independent.cf32",std::ios::binary);payload.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());payload.close();
    SignalSourceProvider provider{"independent.normalized-json","independent-source","source.title",{".independent-iq"},[](const auto& file){
        SignalSourceInfo info;info.metadata_path=std::filesystem::absolute(file);info.storage.data_path=info.metadata_path.parent_path()/"independent.cf32";std::ifstream input(file);input>>info.storage.metadata;info.storage.pairs=info.storage.metadata.at("count").template get<std::uint64_t>();
        info.stream={1000000,434000000,SampleFormat::cf32_le};info.source_id="independent.reader";info.time_origin=std::stoull(info.storage.metadata.at("first").template get<std::string>());info.segments.push_back({0,info.time_origin,info.storage.pairs,false});
        info.read=[](const SignalSourceInfo& source,SignalSampleRange range,std::stop_token,std::size_t){SignalWindow result;result.stream=source.stream;SignalSegment segment;segment.first_sample=source.time_origin+range.first;segment.samples.reserve(range.count);
            std::ifstream payload(source.storage.data_path,std::ios::binary);const auto stride=bytes_per_pair(source.storage.format);payload.seekg(range.first*stride);Bytes bytes(stride);
            for(auto n=0ULL;n<range.count;++n){payload.read(reinterpret_cast<char*>(bytes.data()),bytes.size());if(!payload)throw std::runtime_error("Truncated independent payload");segment.samples.push_back(read_iq_pair(bytes.data(),source.storage.format));}result.segments.push_back(std::move(segment));return result;};return info;
    }};
    const auto before=signal_source_providers().size();register_signal_source_provider(provider);require(signal_source_providers().size()==before+1,"Independent source descriptor was not registered");
    const auto selected=read_signal_window(path,1,2);require(selected.source_id=="independent.reader" && selected.first_sample()==18014398509482078ULL && selected.segments[0].samples==std::vector<std::complex<float>>{{.4f,-.3f},{.6f,-.5f}},"Independent source could not use the unchanged window consumer");
    auto info=inspect_signal_source(path);const auto valid=info.read;info.read=[valid](const auto& source,auto range,auto stop,auto maximum){auto result=valid(source,range,stop,maximum);result.segments[0].first_sample+=1;return result;};
    rejected([&]{read_signal_source(info,1,2);},"Reader changed source coordinates without upgrading its contract");
    rejected([&]{register_signal_source_provider(provider);},"Duplicate source registration was accepted");provider.id="independent.unsupported";provider.filename_suffixes={".unsupported"};provider.protocol_version=2;rejected([&]{register_signal_source_provider(provider);},"An unsupported source protocol was registered");
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/path_from_utf8("amc-来源-📡-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));bool owned=false;
    try {owned=std::filesystem::create_directory(root);require(owned,"Source fixture directory already exists");for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le,SampleFormat::cf32_le})archiveRoundtrip(root,format);independentReader(root);std::filesystem::remove_all(root);std::cout<<"Segmented archive codecs, native Unicode paths, large source clocks, gaps, range mapping, budgets, cancellation, invalid inputs and independent source provider passed.\n";return 0;}
    catch(const std::exception& error){if(owned)std::filesystem::remove_all(root);std::cerr<<error.what()<<'\n';return 1;}
}
