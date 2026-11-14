#include "plugins/desktop/research/waveform/analysis_save_worker.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QCoreApplication>
#include <iostream>
#include <numbers>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot read saved fixture");return {std::istreambuf_iterator<char>(file),{}};}
Json json(const std::filesystem::path& path){return Json::parse(read(path));}
std::shared_ptr<const WaveformSaveCompletion> finish(WaveformAnalysisSaveWorker& worker){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<until){if(auto done=worker.takeLatest())return done;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Waveform save did not complete");}
std::shared_ptr<const SignalWindow> fixture() {
    auto window=std::make_shared<SignalWindow>();window->stream={1000000,433920000,SampleFormat::ci16_le};window->source_id="independent.save.truth";
    window->processing=Json::array({{{"method","independent-capture"},{"receiver_context",{{"requested",{{"gain",17}}},{"applied",{{"gain",19.7}}},{"unknown_diagnostic",23}}}}});
    const auto segment=[&](std::uint64_t first,std::size_t count,bool discontinuous){SignalSegment result;result.first_sample=first;result.discontinuous_before=discontinuous;result.samples.reserve(count);
        Bytes bytes(bytes_per_pair(window->stream.format));for(std::size_t n=0;n<count;++n){const auto value=std::polar(.5,.3+2*std::numbers::pi*.07*n);write_iq_pair(bytes.data(),window->stream.format,{float(value.real()),float(value.imag())});result.samples.push_back(read_iq_pair(bytes.data(),window->stream.format));}return result;};
    constexpr auto first=(1ULL<<54)+81;window->segments={segment(first,128,false),segment(first+1024,64,true)};return window;
}
Json configuration(const SignalWindow& window){return {{"format","org.amc.waveform-analysis"},{"version",1},{"source",{{"kind","memory-capture"}}},
    {"parameters",{{"minimum_amplitude",1e-8},{"relative_amplitude_floor",.05},{"frequency_smoothing_samples",9}}},
    {"view",{{"first",std::to_string(window.first_sample()+64)},{"count","32"},{"cursor_a",std::to_string(window.first_sample())},{"cursor_b",std::to_string(window.first_sample()+80)}}},
    {"event",{{"modulation_id","independent.unknown"},{"future_field",17}}}};}
void exercise(const std::filesystem::path& root) {
    const auto source=fixture();const auto config=configuration(*source);WaveformAnalysisSaveWorker worker;
    WaveformSaveRequest request;request.configuration_path=root/path_from_utf8("冻结恢复 📡.amc-analysis.json");request.configuration=config;request.capture=source;
    request.capture_identity=waveform_source_identity(Json::object(),*source);const auto generation=worker.request(request);auto done=finish(worker);
    require(done->generation==generation && done->configuration_committed && done->verified_capture && !done->failure && !worker.busy(),"A frozen source and configuration were not committed and verified");
    const auto saved=json(request.configuration_path);require(saved.at("version")==2 && saved.at("view")==config.at("view") && saved.at("event")==config.at("event"),"Save lost source-clock precision, view or unknown evidence");
    const auto& reference=saved.at("source");const auto relative=path_from_utf8(reference.at("metadata_path").get<std::string>());require(!relative.is_absolute() && reference.at("path_base")=="analysis-directory","A captured source copy was not portable with its configuration");
    const auto metadata=request.configuration_path.parent_path()/relative;auto info=inspect_signal_source(metadata);const auto samples=read_signal_source(info,0,192);
    require(samples.stream.format==source->stream.format && samples.segments[0].samples==source->segments[0].samples && samples.segments[1].samples==source->segments[1].samples && samples.segments[1].first_sample==source->segments[1].first_sample,"Saved data changed normalized samples, encoding facts or gaps");
    const auto identity=waveform_source_identity(info.storage.metadata,samples);require(reference.at("metadata_sha256")==identity.metadata_sha256 && reference.at("normalized_window_sha256")==identity.normalized_window_sha256,"Saved source identity did not describe the actual persisted samples");
    const auto destination=root/path_from_utf8("移动后的分析 📡");std::filesystem::create_directory(destination);std::filesystem::copy_file(request.configuration_path,destination/request.configuration_path.filename());
    std::filesystem::create_directory(destination/relative.parent_path());std::filesystem::copy_file(metadata,destination/relative);std::filesystem::copy_file(info.storage.data_path,destination/relative.parent_path()/"samples.cf32");
    require(read_signal_window(destination/relative,120,16).segments.size()==2,"Moving configuration and capture together broke the range mapping");
    request.configuration["version"]=3;request.configuration["channel"]={{"version",1},{"enabled",true},{"offset_hz",1000},{"passband_hz",120000},{"transition_hz",60000},{"output_rate_hz",400001}};
    request.configuration_path=root/"processed.amc-analysis.json";worker.request(request);done=finish(worker);
    require(done->configuration_committed && json(request.configuration_path).at("version")==3 && json(request.configuration_path).at("channel")==request.configuration.at("channel"),"Persisting raw observations silently downgraded a processed analysis configuration");
    request.configuration["version"]=4;request.configuration["window"]={{"version",1},{"enabled",true},{"fft_size",256},{"hop_samples",128},{"processed",true}};request.configuration_path=root/"window.amc-analysis.json";worker.request(request);done=finish(worker);
    require(done->configuration_committed && json(request.configuration_path).at("version")==4 && json(request.configuration_path).at("window")==request.configuration.at("window"),"Source persistence downgraded a window research configuration");
    request.configuration["version"]=5;request.configuration["channel"]["version"]=2;request.configuration["channel"]["input"]={{"kind","file-range"},{"first_file_sample","0"},{"sample_count","192"}};request.configuration_path=root/"file-channel.amc-analysis.json";worker.request(request);done=finish(worker);
    require(done->configuration_committed && json(request.configuration_path).at("version")==5 && json(request.configuration_path).at("channel")==request.configuration.at("channel"),"Persistence downgraded an independently scoped recorded channel");
    request.configuration["version"]=7;request.configuration["recovery"]={{"version",1},{"enabled",true},{"provider_id","independent.recovery"},{"future_metadata",71}};request.configuration_path=root/"recovery.amc-analysis.json";worker.request(request);done=finish(worker);
    require(done->configuration_committed && json(request.configuration_path).at("version")==7 && json(request.configuration_path).at("recovery")==request.configuration.at("recovery"),"Frozen persistence downgraded or dropped recovery configuration");
    auto recovery=std::make_shared<RecoveryResult>();recovery->analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));recovery->range={source->first_sample(),source->end_sample()-source->first_sample()};recovery->provider_id="independent.recovery";recovery->resource_namespace="independent.resources";recovery->method_version=1;recovery->kind=RecoveryKind::digital;recovery->stages={{"custom-stage","stage",{},Json::object(),Json::object(),Json::object()}};
    RecoveryValue byte;byte.value=0x4b;byte.known=true;byte.support={source->first_sample(),1};recovery->sequences={{"custom-byte","bytes",0,RecoveryValueKind::byte,{byte}}};
    WaveformSaveRequest exported;exported.purpose=WaveformFilePurpose::recovery_json;exported.recovery=recovery;exported.configuration=saved;exported.configuration_path=root/path_from_utf8("后台证据 📡.json");worker.request(exported);done=finish(worker);require(done->export_committed && !done->configuration_committed && !done->failure && json(exported.configuration_path).at("sequences")[0].at("values")[0].at("value")=="75","Background recovery JSON export failed or pretended to save an analysis");
    const auto evidenceBefore=read(exported.configuration_path);exported.export_maximum_bytes=64;worker.request(exported);done=finish(worker);require(done->failure && done->error_key=="recovery.export.budget" && !done->export_committed && read(exported.configuration_path)==evidenceBefore,"Over-budget export partially replaced an existing file");
    exported.export_maximum_bytes=recoveryExportMaximumBytes;exported.protected_paths={exported.configuration_path};worker.request(exported);done=finish(worker);require(done->failure && read(exported.configuration_path)==evidenceBefore,"Recovery export overwrote a protected original");exported.protected_paths.clear();
    exported.purpose=WaveformFilePurpose::recovery_bytes;exported.configuration_path=root/path_from_utf8("后台字节 📡.bin");exported.selection=RecoveryLink{0,0,1};worker.request(exported);done=finish(worker);require(done->export_committed && read(exported.configuration_path)=="K","Background binary export changed the selected byte");
    auto unknown=std::make_shared<RecoveryResult>(*recovery);unknown->sequences[0].values[0].known=false;unknown->sequences[0].values[0].issue_key="independent.unknown";exported.recovery=unknown;worker.request(exported);done=finish(worker);require(done->failure && done->error_key=="recovery.export.unknown" && read(exported.configuration_path)=="K","Unknown binary export wrote a placeholder over an existing file");
    exported.recovery=recovery;worker.request(exported);worker.cancel();done=finish(worker);require(done->canceled || done->export_committed,"Export/cancel race had no coherent outcome");require(read(exported.configuration_path)=="K","Canceled export damaged a previously committed binary");
    const auto original=root/"keep-source.json";write_json(original,{{"sentinel","preserve"}});const auto before=read(original);WaveformSaveRequest protectedRequest;
    protectedRequest.configuration_path=original;protectedRequest.configuration=saved;protectedRequest.protected_paths={original};worker.request(protectedRequest);done=finish(worker);
    require(done->failure && !done->configuration_committed && read(original)==before,"Save overwrote its protected source");
    const auto occupied=root/"occupied.json";std::filesystem::create_directory(occupied);request.configuration_path=occupied;worker.request(request);done=finish(worker);
    require(done->failure && done->verified_capture && !done->configuration_committed && std::filesystem::exists(done->capture_metadata),"Configuration failure lost a verified copy or pretended the entire save succeeded");
    request.configuration_path=original;worker.request(request);worker.cancel();done=finish(worker);
    require(done->canceled || done->configuration_committed,"A save/cancel race had no coherent outcome");if(done->canceled)require(read(original)==before,"Canceled save partially overwrote an existing configuration");else require(json(original).at("format")=="org.amc.waveform-analysis","Committed save/cancel race produced an invalid configuration");
    WaveformSaveRequest excessive;excessive.configuration_path=root/"too-large.json";excessive.configuration=saved;excessive.configuration["event"]["large_field"]=std::string(8*1024*1024,'x');worker.request(std::move(excessive));done=finish(worker);
    require(done->failure && done->error_key=="save.error.configuration_budget" && !std::filesystem::exists(root/"too-large.json"),"Configuration budget failure published a partial file");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto root=std::filesystem::temp_directory_path()/path_from_utf8("amc-save-保存-📡-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));bool owned=false;
    try{owned=std::filesystem::create_directory(root);require(owned,"Save fixture directory already exists");exercise(root);std::filesystem::remove_all(root);std::cout<<"Background verified capture persistence, relative references, Unicode paths, source-clock gaps, protected originals, atomic cancel, failure recovery and budgets passed.\n";return 0;}
    catch(const std::exception& error){if(owned)std::filesystem::remove_all(root);std::cerr<<error.what()<<'\n';return 1;}
}
