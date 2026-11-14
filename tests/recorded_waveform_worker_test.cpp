#include "plugins/desktop/research/waveform/recorded_worker.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <iostream>
#include <numbers>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const RecordedWaveformCompletion> finish(RecordedWaveformWorker& worker) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        if(auto result=worker.takeLatest())return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Recorded waveform job did not complete within its test deadline");
}
void exercise(const std::filesystem::path& root) {
    Config config;config.rate=2'000'000;
    const auto format=SampleFormat::cf32_le;const auto stride=bytes_per_pair(format);
    Bytes bytes(16384*stride);
    for(std::size_t n=0;n<16384;++n) {const auto value=std::polar(.6,2*std::numbers::pi*.031*n);write_iq_pair(bytes.data()+n*stride,format,{float(value.real()),float(value.imag())});}
    const auto capture=root/"recording";
    Recorder recording(capture,config,"independent-worker-truth",format);recording.append(bytes);recording.finish("complete",{{"stream_first_sample",900007}});
    const auto metadata=capture/"capture.sigmf-meta";
    RecordedWaveformWorker worker;RecordedWaveformRequest request;request.metadata=metadata;request.range=SignalSampleRange{123,8192};
    const auto generation=worker.request(request);auto result=finish(worker);
    require(result->generation==generation && !result->failure && result->analysis->source->first_sample()==900130,"Recording worker lost its generation or native source interval");
    require(result->identity.metadata_sha256.size()==64 && result->identity.normalized_window_sha256.size()==64,"Reproducible source digests are missing");
    const auto identity=result->identity;const auto source=result->analysis->source;
    RecordedWaveformRequest cached;cached.metadata=metadata;cached.cached=source;cached.recording=result->recording;cached.cached_identity=identity;cached.options.frequency_smoothing_samples=3;cached.preserve_view=true;
    worker.request(cached);result=finish(worker);
    require(!result->failure && result->preserve_view && result->analysis->source==source && result->analysis->method.at("smoothing_samples")==3,"Changing estimator parameters did not reuse the immutable source");
    cached.channel=ChannelExtractionOptions{62000,120000,60000,400000};worker.request(cached);result=finish(worker);
    require(!result->failure && result->channel_analysis && result->channel_analysis->source==source && result->channel_analysis->sample_count()<source->sample_count(),"Channel reanalysis did not retain the raw source and processed clock");
    for(auto f:result->channel_analysis->trace("frequency").values)if(std::isfinite(f))require(std::abs(f)<1,"The worker channel did not remove its requested RF offset");
    require(std::abs(result->channel_analysis->time_offset_seconds()-123./2000000)<1e-15,"A processed file range lost its time offset relative to the whole recording");
    const auto processedResult=result;RecordedWaveformRequest selection;selection.completed=processedResult;selection.research_processed=true;selection.research_options=WindowResearchOptions{};
    auto fileChannel=cached;fileChannel.channel_file_range=SignalSampleRange{0,16384};worker.request(fileChannel);auto fileResult=finish(worker);
    require(!fileResult->failure && fileResult->channel_analysis && !fileResult->channel_analysis->source && fileResult->channel_analysis->processed->recording &&
        fileResult->channel_source_identity.normalized_source_sha256.size()==64 && fileResult->channel_source_identity.metadata_sha256==identity.metadata_sha256,"Recorded channel worker required the whole raw span in RAM or omitted its complete input identity");
    fileChannel.expected_channel_source=fileResult->channel_source_identity;fileChannel.channel_source_budget.central_samples=127;worker.request(fileChannel);result=finish(worker);
    require(!result->failure && result->channel_source_identity.normalized_source_sha256==fileResult->channel_source_identity.normalized_source_sha256,"Source identity changed with halo read size");
    {
        std::fstream file(capture/"capture.sigmf-data",std::ios::in|std::ios::out|std::ios::binary);file.seekg(15000*stride);char byte=0;file.read(&byte,1);require(bool(file),"Could not mutate source outside the loaded preview");byte^=1;file.seekp(15000*stride);file.write(&byte,1);
    }
    worker.request(fileChannel);result=finish(worker);require(result->failure && result->error_key=="channel.error.source_changed","A recorded input change outside the loaded preview escaped full selected-range identity verification");
    auto missingParameters=fileChannel;missingParameters.channel.reset();worker.request(missingParameters);result=finish(worker);
    require(result->failure && result->error_key=="channel.error.configuration","A file range without channel parameters was accepted as an applied capability");
    selection.research_range=SignalSampleRange{processedResult->channel_analysis->first_sample()+20,1024};worker.request(selection);result=finish(worker);
    require(!result->failure && result->research_only && result->analysis==processedResult->analysis && result->channel_analysis==processedResult->channel_analysis && result->research->range==*selection.research_range,"Selection research recalculated or replaced immutable waveform traces");
    require(std::abs(result->research->statistics.mean_frequency_hz)<1 && result->research->statistics.observed_samples==1024,"Selection research used the wrong channel or range");
    const auto researched=result;RecordedWaveformRequest modeling;modeling.completed=researched;modeling.model=WaveformModelRequest{};modeling.model->provider_id="constant-frequency";modeling.model->processed=true;modeling.model->parameters={{"carrier_hz",0}};modeling.model->range=SignalSampleRange{researched->channel_analysis->first_sample()+20,256};
    worker.request(modeling);auto modeled=finish(worker);
    require(!modeled->failure && modeled->research_only && modeled->analysis==researched->analysis && modeled->channel_analysis==researched->channel_analysis && modeled->research==researched->research && modeled->model && modeled->model->analysis==researched->channel_analysis && modeled->model->observations==256 && modeled->model->residual_rms<1e-5,"Model work did not share immutable waveform/window results or used the wrong processed clock");
    require(modeled->model_request->expected_clock==researched->channel_analysis->processed->clock && modeled->model->trace("reference_i").values.size()==256,"The model omitted its actual input clock or allocated the entire preview");
    RecordedWaveformRequest windowAfterModel;windowAfterModel.completed=modeled;windowAfterModel.research_processed=true;windowAfterModel.research_options=WindowResearchOptions{};windowAfterModel.research_range=SignalSampleRange{researched->channel_analysis->first_sample()+40,512};worker.request(windowAfterModel);auto combined=finish(worker);
    require(!combined->failure && combined->model==modeled->model && combined->research!=modeled->research && combined->research->statistics.observed_samples==512,"A window-only calculation silently regenerated or dropped the applied model");
    RecordedWaveformRequest recovery;recovery.completed=modeled;recovery.recovery=WaveformRecoveryRequest{};recovery.recovery->provider_id="manual-2fsk-fixed-crc16";recovery.recovery->processed=true;recovery.recovery->range=SignalSampleRange{modeled->channel_analysis->first_sample()+20,256};recovery.recovery->event={{"unknown_event",{{"opaque",27}}}};
    worker.request(recovery);const auto recovered=finish(worker);
    require(!recovered->failure && recovered->research_only && recovered->model==modeled->model && recovered->research==modeled->research && recovered->recovery && recovered->recovery->analysis==modeled->channel_analysis && recovered->recovery->event==recovery.recovery->event && recovered->recovery_request->expected_clock==modeled->channel_analysis->processed->clock,"Recovery did not share applied input/model/window or lost its event and actual clock");
    auto windowAfterRecovery=windowAfterModel;windowAfterRecovery.completed=recovered;worker.request(windowAfterRecovery);const auto refreshed=finish(worker);require(!refreshed->failure && refreshed->recovery==recovered->recovery && refreshed->recovery_request->range==recovered->recovery_request->range,"Window refresh regenerated or dropped applied recovery evidence");
    auto modelAfterRecovery=modeling;modelAfterRecovery.completed=recovered;worker.request(modelAfterRecovery);const auto remade=finish(worker);require(!remade->failure && remade->recovery==recovered->recovery && remade->recovery->event==recovery.recovery->event,"Model refresh dropped immutable recovery evidence");
    auto invalidRecovery=recovery;invalidRecovery.recovery->method_version=99;worker.request(invalidRecovery);auto recoveryFailure=finish(worker);require(recoveryFailure->failure && recoveryFailure->error_key=="recovery.error.version","Recovery silently substituted a saved method version");
    invalidRecovery=recovery;invalidRecovery.recovery->expected_clock=recovered->recovery_request->expected_clock;++invalidRecovery.recovery->expected_clock->rate;worker.request(invalidRecovery);recoveryFailure=finish(worker);require(recoveryFailure->failure && recoveryFailure->error_key=="recovery.error.clock","Recovery silently changed its saved input clock");
    invalidRecovery=recovery;invalidRecovery.result_payload_bytes=128;worker.request(invalidRecovery);recoveryFailure=finish(worker);require(recoveryFailure->failure && recoveryFailure->error_key=="error.budget","Recovery escaped the joint structured payload/metadata allowance");
    invalidRecovery=recovery;invalidRecovery.recovery->budget.metadata_bytes=1;worker.request(invalidRecovery);recoveryFailure=finish(worker);require(bool(recoveryFailure->failure),"Recovery omitted its metadata budget");
    std::uint64_t recoveryGeneration=0;for(unsigned n=0;n<10;++n){recovery.recovery->range->count=128+n;recoveryGeneration=worker.request(recovery);}const auto latestRecovery=finish(worker);require(!latestRecovery->failure && latestRecovery->generation==recoveryGeneration && latestRecovery->recovery->range.count==137,"An obsolete recovery request replaced the latest selection");
    auto invalidModel=modeling;invalidModel.model->method_version=99;worker.request(invalidModel);auto modelFailure=finish(worker);require(modelFailure->failure && modelFailure->error_key=="model.error.version","An unsupported model method was silently substituted");
    invalidModel=modeling;invalidModel.model->expected_clock=modeled->model_request->expected_clock;++invalidModel.model->expected_clock->rate;worker.request(invalidModel);modelFailure=finish(worker);require(modelFailure->failure && modelFailure->error_key=="model.error.clock","A saved model used a different processing clock");
    invalidModel=modeling;invalidModel.result_payload_bytes=32;worker.request(invalidModel);modelFailure=finish(worker);require(modelFailure->failure && modelFailure->error_key=="error.budget","Retained window/model payload escaped the joint budget");
    ReferenceModelProvider legacy;legacy.id="independent.legacy-worker-model";legacy.resource_namespace="independent.resources";legacy.label_key="label";legacy.parameters=[](const auto&){return Json::array();};legacy.prepare=[](const auto&,const Json&){PreparedReferenceModel value;value.evaluate=[](const auto&){return std::complex<double>{1,0};};return value;};register_reference_model(legacy);
    invalidModel=modeling;invalidModel.model->provider_id=legacy.id;worker.request(invalidModel);modelFailure=finish(worker);require(modelFailure->failure && modelFailure->error_key=="model.error.capability","A legacy provider was given an invented workspace guarantee");
    std::uint64_t modelGeneration=0;for(unsigned n=0;n<10;++n){modeling.model->range->count=64+n;modelGeneration=worker.request(modeling);}modeled=finish(worker);require(!modeled->failure && modeled->generation==modelGeneration && modeled->model->observations==73 && modeled->research==researched->research,"An obsolete model job replaced the latest request or discarded retained research");
    selection.result_payload_bytes=16;worker.request(selection);result=finish(worker);require(result->failure && result->error_key=="error.budget","Window workspace and matrix escaped the combined analysis budget");
    selection.result_payload_bytes=128*1024*1024;
    std::uint64_t selectionGeneration=0;for(unsigned n=0;n<10;++n){selection.research_range->count=512+n;selectionGeneration=worker.request(selection);}result=finish(worker);
    require(!result->failure && result->generation==selectionGeneration && result->research->statistics.observed_samples==521,"An obsolete research selection replaced the latest result");
    result=processedResult;
    cached.result_payload_bytes=source->payload_bytes()+result->analysis->payload_bytes()+16;worker.request(cached);result=finish(worker);
    require(result->failure && result->error_key=="error.budget","Raw and processed results escaped the joint payload budget");
    cached.result_payload_bytes=128*1024*1024;cached.channel.reset();
    {
        std::fstream file(capture/"capture.sigmf-data",std::ios::in|std::ios::out|std::ios::binary);
        file.seekg(200*stride);char byte=0;file.read(&byte,1);require(bool(file),"Could not mutate independent source fixture");
        byte^=1;file.seekp(200*stride);file.write(&byte,1);
    }
    request.expected=identity;worker.request(request);result=finish(worker);
    require(result->failure && result->error_key=="error.source_changed" && !result->analysis,"Reopening a changed source silently produced a reproducible-looking result");
    std::uint64_t latest=0;for(unsigned n=0;n<30;++n){cached.options.frequency_smoothing_samples=n+1;latest=worker.request(cached);}
    result=finish(worker);require(result->generation==latest && !result->failure && result->analysis->method.at("smoothing_samples")==30,"An obsolete range/parameter job replaced the latest request");
    worker.request(cached);worker.cancel();std::this_thread::sleep_for(std::chrono::milliseconds(30));require(!worker.takeLatest(),"A canceled request published a completed analysis");
    auto frozen=std::make_shared<SignalWindow>(select_signal_window(*source,source->first_sample()+97,512));frozen->recording_path.clear();frozen->source_id="independent.frozen";
    frozen->processing.push_back({{"method","independent-capture"},{"version",1}});
    RecordedWaveformRequest memory;memory.cached=frozen;worker.request(memory);result=finish(worker);
    require(!result->failure && !result->recording && result->file_range.first==0 && result->analysis->source==frozen && result->identity.normalized_window_sha256.size()==64,"A frozen in-memory window required a fictitious recording or lost its identity");
    for(std::size_t n=1;n<512;++n)require(std::abs(result->analysis->trace("frequency").values[n]-62000)<.1,"Frozen memory and recorded carrier analysis disagree");
    auto excess=std::make_shared<SignalWindow>(*frozen);excess->segments.front().samples.reserve(2000000);memory.cached=excess;worker.request(memory);result=finish(worker);
    require(result->failure && result->error_key=="error.budget","An immutable source with small size and excessive allocation escaped the actual payload budget");
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/path_from_utf8("amc-recorded-波形-📡-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {std::filesystem::create_directories(root);exercise(root);std::filesystem::remove_all(root);std::cout<<"Recorded worker source identity, cached reanalysis, supersession, cancellation and Unicode path passed.\n";return 0;}
    catch(const std::exception& e){std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
