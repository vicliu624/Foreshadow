#include "recorded_worker.hpp"
#include <QCryptographicHash>
#include <QByteArray>
#include <bit>

namespace signalscope::desktop {
namespace {
std::string digest(const std::string& text) {
    return QCryptographicHash::hash(QByteArray::fromRawData(text.data(),static_cast<int>(text.size())),QCryptographicHash::Sha256).toHex().toStdString();
}
std::size_t waveformPayload(const RecordedWaveformCompletion& result) {
    if(!result.analysis || !result.analysis->source)throw std::invalid_argument("A completed waveform requires immutable original observations");
    return result.analysis->source->allocated_payload_bytes()+result.analysis->payload_bytes()+
        (result.channel_analysis?result.channel_analysis->payload_bytes()+result.channel_analysis->processed->payload_bytes():0);
}
std::string windowDigest(const SignalWindow& window,std::stop_token stop) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Json descriptor={{"version",signalWindowProtocolVersion},{"sample_rate",window.stream.sample_rate},
        {"center_frequency_hz",window.stream.center_frequency_hz},{"original_encoding",sample_datatype(window.stream.format)},
        {"processing",window.processing},{"segments",Json::array()}};
    for(const auto& segment:window.segments)descriptor["segments"].push_back({{"first",segment.first_sample},
        {"count",segment.samples.size()},{"discontinuous",segment.discontinuous_before}});
    const auto header=descriptor.dump();hash.addData(QByteArray::fromRawData(header.data(),static_cast<int>(header.size())));
    const auto stride=bytes_per_pair(SampleFormat::cf32_le);Bytes bytes(4096*stride);
    for(const auto& segment:window.segments)for(std::size_t first=0;first<segment.samples.size();first+=4096) {
        if(stop.stop_requested())return {};
        const auto count=std::min<std::size_t>(4096,segment.samples.size()-first);
        for(std::size_t n=0;n<count;++n)write_iq_pair(bytes.data()+n*stride,SampleFormat::cf32_le,segment.samples[first+n]);
        hash.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()),static_cast<int>(count*stride)));
    }
    return hash.result().toHex().toStdString();
}
class RecordedRangeDigest {
public:
    RecordedRangeDigest(const SignalSourceInfo& source,SignalSampleRange range,std::shared_ptr<const SignalWindow> preview,std::stop_token stop,std::size_t metadata_budget)
        :source_(source),range_(range),preview_(std::move(preview)),stop_(stop),next_(range.first),hash_(QCryptographicHash::Sha256),bytes_(4096*bytes_per_pair(SampleFormat::cf32_le)) {
        Json descriptor={{"format","org.amc.normalized-recorded-range"},{"version",1},
            {"first_file_sample",std::to_string(range.first)},{"sample_count",std::to_string(range.count)},
            {"sample_rate_hz",source.stream.sample_rate},{"center_frequency_hz",source.stream.center_frequency_hz},
            {"original_encoding",sample_datatype(source.stream.format)},{"time_origin",std::to_string(source.time_origin)},
            {"processing",source.processing},{"segments",Json::array()}};
        for(const auto& map:source.segments) {
            const auto first=std::max(range.first,map.first_stored_sample),last=std::min(range.first+range.count,map.first_stored_sample+map.count);if(first>=last)continue;
            descriptor["segments"].push_back({{"first",std::to_string(map.first_source_sample+first-map.first_stored_sample)},{"count",std::to_string(last-first)},
                {"discontinuous_before",first==map.first_stored_sample && map.discontinuous_before}});
        }
        const auto header=descriptor.dump();if(header.size()>metadata_budget)throw std::length_error("Recorded source identity descriptor exceeds its budget");
        hash_.addData(QByteArray::fromRawData(header.data(),static_cast<int>(header.size())));
    }
    void append(const SignalWindow& window,SignalSampleRange central) {
        if(central.first!=next_ || central.count>range_.first+range_.count-next_)throw std::invalid_argument("Recorded source identity blocks are not a complete ordered range");
        std::uint64_t seen=0;const auto stride=bytes_per_pair(SampleFormat::cf32_le);
        for(const auto& segment:window.segments) {
            const auto stored=source_.stored_index(segment.first_sample);if(!stored)throw std::invalid_argument("Recorded source identity received an unmapped segment");
            const auto first=std::max(*stored,central.first),last=std::min(*stored+segment.samples.size(),central.first+central.count);if(first>=last)continue;
            for(auto offset=first;offset<last;) {
                if(stop_.stop_requested())return;const auto count=std::min<std::uint64_t>(4096,last-offset);
                for(std::size_t i=0;i<count;++i) {
                    const auto local=static_cast<std::size_t>(offset-*stored)+i;const auto value=segment.samples[local];
                    if(preview_ && segment.first_sample+local>=preview_->first_sample() && segment.first_sample+local<preview_->end_sample())if(const auto address=locate_signal_sample(*preview_,segment.first_sample+local)) {
                        const auto cached=preview_->segments[address->segment].samples[address->index];
                        if(std::bit_cast<std::uint32_t>(cached.real())!=std::bit_cast<std::uint32_t>(value.real()) || std::bit_cast<std::uint32_t>(cached.imag())!=std::bit_cast<std::uint32_t>(value.imag()))
                            throw std::invalid_argument("Recorded source no longer matches the immutable original preview");
                    }
                    write_iq_pair(bytes_.data()+i*stride,SampleFormat::cf32_le,value);
                }
                hash_.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(bytes_.data()),static_cast<int>(count*stride)));offset+=count;seen+=count;
            }
        }
        if(seen!=central.count)throw std::invalid_argument("Recorded source identity omitted central observations");next_+=seen;
    }
    std::string finish(){if(next_!=range_.first+range_.count)throw std::invalid_argument("Recorded source identity is incomplete");return hash_.result().toHex().toStdString();}
private:
    const SignalSourceInfo& source_;SignalSampleRange range_;std::shared_ptr<const SignalWindow> preview_;std::stop_token stop_;std::uint64_t next_;
    QCryptographicHash hash_;Bytes bytes_;
};
}
WaveformSourceIdentity waveform_source_identity(const Json& metadata,const SignalWindow& window,std::stop_token stop) {
    return {digest(metadata.dump()),windowDigest(window,stop)};
}
RecordedWaveformWorker::RecordedWaveformWorker():thread_([this](auto stop){run(stop);}){}
RecordedWaveformWorker::~RecordedWaveformWorker() {
    thread_.request_stop();cancel();condition_.notify_all();if(thread_.joinable())thread_.join();
}
std::uint64_t RecordedWaveformWorker::request(RecordedWaveformRequest input) {
    std::lock_guard lock(mutex_);active_stop_.request_stop();active_stop_=std::stop_source{};
    const auto generation=++generation_;pending_=Job{generation,std::move(input),active_stop_.get_token()};
    std::atomic_store(&latest_, {});condition_.notify_one();return generation;
}
void RecordedWaveformWorker::cancel() {
    std::lock_guard lock(mutex_);++generation_;active_stop_.request_stop();pending_.reset();std::atomic_store(&latest_, {});condition_.notify_all();
}
std::shared_ptr<const RecordedWaveformCompletion> RecordedWaveformWorker::takeLatest(){return std::atomic_exchange(&latest_, {});}
void RecordedWaveformWorker::run(std::stop_token stop) {
    for(;;) {
        std::optional<Job> job;
        {std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stop.stop_requested() || pending_.has_value();});
            if(stop.stop_requested())return;job=std::move(pending_);pending_.reset();}
        auto result=std::make_shared<RecordedWaveformCompletion>();result->generation=job->generation;
        result->preserve_view=job->input.preserve_view;
        result->options=job->input.options;
        result->channel=job->input.channel;
        result->channel_file_range=job->input.channel_file_range;
        try {
            const auto& input=job->input;
            if(input.completed && input.completed->recovery && !input.completed->recovery_request)throw std::invalid_argument("Retained recovery lacks its actual request");
            if(input.channel_file_range && !input.channel && !input.completed){result->error_key="channel.error.configuration";throw std::invalid_argument("A recorded channel range requires channel parameters");}
            std::size_t reserved=0;
            if(input.research_options) {
                const auto& options=*input.research_options;
                if(options.result_payload_bytes>input.result_payload_bytes || options.workspace_payload_bytes>input.result_payload_bytes-options.result_payload_bytes)
                    throw std::length_error("Window research exceeds the combined result budget");
                reserved=options.result_payload_bytes+options.workspace_payload_bytes;
            }
            const auto reserve=[&](std::size_t bytes){if(bytes>input.result_payload_bytes-reserved)throw std::length_error("Retained research exceeds the combined result budget");reserved+=bytes;};
            if(input.completed) {
                if(!input.research_options && input.completed->research)reserve(input.completed->research->payload_bytes());
                if(!input.model && input.completed->model)reserve(input.completed->model->payload_bytes());
                if(!input.recovery && input.completed->recovery){reserve(input.completed->recovery->payload_bytes());reserve(input.completed->recovery_request->budget.metadata_bytes);}
            }
            if(input.channel_file_range && !input.completed) {
                const auto& budget=input.channel_source_budget;
                if(reserved>input.result_payload_bytes || budget.workspace_payload_bytes>input.result_payload_bytes-reserved ||
                   budget.metadata_payload_bytes>input.result_payload_bytes-reserved-budget.workspace_payload_bytes)
                    throw std::length_error("Recorded channel workspace exceeds the combined result budget");
                reserved+=budget.workspace_payload_bytes+budget.metadata_payload_bytes;
            }
            const auto computation_budget=input.result_payload_bytes-reserved;
            if(input.completed) {
                const auto& previous=*input.completed;
                if(previous.failure || !previous.analysis || (!input.research_options && !input.model && !input.recovery))throw std::invalid_argument("Selection research requires a successful immutable waveform result");
                result->analysis=previous.analysis;result->channel_analysis=previous.channel_analysis;result->channel=previous.channel;
                result->channel_file_range=previous.channel_file_range;result->channel_source_identity=previous.channel_source_identity;
                result->recording=previous.recording;result->file_range=previous.file_range;result->identity=previous.identity;
                result->options=previous.options;result->preserve_view=true;result->research_only=true;
                result->research=previous.research;result->research_processed=previous.research_processed;result->model=previous.model;result->model_request=previous.model_request;
                result->recovery=previous.recovery;result->recovery_request=previous.recovery_request;
                const auto used=waveformPayload(*result);
                if(used>computation_budget)throw std::length_error("Immutable traces and window research exceed the combined result budget");
            }else {
                std::shared_ptr<const SignalWindow> source;
                if(input.cached) {
                    source=input.cached;result->recording=input.recording;result->identity=input.cached_identity;
                    if(source->allocated_payload_bytes()>input.source_payload_bytes || source->sample_count()>waveform_sample_capacity(input.options))throw std::length_error("Frozen waveform exceeds its analysis budget");
                    const auto first=input.recording?input.recording->stored_index(source->first_sample()):std::optional<std::uint64_t>(0);
                    if(!first)throw std::invalid_argument("Cached window is not mapped to its stored source");result->file_range={*first,source->sample_count()};
                    if(result->identity.metadata_sha256.empty())result->identity.metadata_sha256=digest(source->processing.dump());
                    if(result->identity.normalized_window_sha256.empty())result->identity.normalized_window_sha256=windowDigest(*source,job->stop);
                }else {
                    result->recording=std::make_shared<SignalSourceInfo>(inspect_signal_source(input.metadata));
                    const auto maximum=std::min<std::uint64_t>(input.source_payload_bytes/sizeof(std::complex<float>),waveform_sample_capacity(input.options));
                    result->file_range=input.range.value_or(SignalSampleRange{0,std::min(result->recording->storage.pairs,maximum)});
                    source=std::make_shared<SignalWindow>(read_signal_source(*result->recording,result->file_range.first,result->file_range.count,job->stop,input.source_payload_bytes));
                    if(job->stop.stop_requested())continue;
                    result->identity.metadata_sha256=digest(result->recording->storage.metadata.dump());
                    result->identity.normalized_window_sha256=windowDigest(*source,job->stop);
                }
                if(job->stop.stop_requested())continue;
                if((!input.expected.metadata_sha256.empty() && input.expected.metadata_sha256!=result->identity.metadata_sha256) ||
                   (!input.expected.normalized_window_sha256.empty() && input.expected.normalized_window_sha256!=result->identity.normalized_window_sha256)) {
                    result->error_key="error.source_changed";throw std::runtime_error("Saved waveform source identity no longer matches");
                }
                const auto source_bytes=source->allocated_payload_bytes();
                if(source_bytes>computation_budget || source->sample_count()>(computation_budget-source_bytes)/(8*sizeof(double)))
                    throw std::length_error("Waveform source and raw traces exceed their combined result budget");
                auto raw_options=input.options;raw_options.output_payload_bytes=std::min(raw_options.output_payload_bytes,computation_budget-source_bytes);
                result->analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source,raw_options,job->stop));
                if(input.channel && !job->stop.stop_requested()) {
                    result->error_key="channel.error.configuration";auto channel=*input.channel;
                    const auto used=source_bytes+result->analysis->payload_bytes();
                    const auto coefficients=input.channel_file_range?0:channel.coefficient_payload_bytes;
                    if(coefficients>computation_budget-used)throw std::length_error("Channel coefficients exceed the combined result budget");
                    const auto remaining=computation_budget-used-coefficients;
                    const auto maximum=remaining/(sizeof(std::complex<float>)+8*sizeof(double));
                    channel.output_payload_bytes=std::min(channel.output_payload_bytes,maximum*sizeof(std::complex<float>));
                    std::shared_ptr<ProcessedSignal> processed;
                    if(input.channel_file_range) {
                        result->error_key="channel.error.source_changed";
                        if(!result->recording)throw std::invalid_argument("A recorded channel range requires a recorded original source");
                        const auto file=std::make_shared<SignalSourceInfo>(inspect_signal_source(result->recording->metadata_path));
                        const auto range=*input.channel_file_range;
                        if(!range.count || range.first>file->storage.pairs || range.count>file->storage.pairs-range.first){result->error_key="channel.error.configuration";throw std::invalid_argument("Recorded channel range must fit its stored source");}
                        result->channel_source_identity.metadata_sha256=digest(file->storage.metadata.dump());
                        if(result->channel_source_identity.metadata_sha256!=result->identity.metadata_sha256 ||
                            file->stream.sample_rate!=source->stream.sample_rate || file->stream.center_frequency_hz!=source->stream.center_frequency_hz || file->stream.format!=source->stream.format ||
                            file->time_origin!=result->recording->time_origin || file->segments!=result->recording->segments || file->processing!=result->recording->processing ||
                            file->source_id!=result->recording->source_id || file->storage.pairs!=result->recording->storage.pairs || file->storage.format!=result->recording->storage.format ||
                            (!input.expected_channel_source.metadata_sha256.empty() && input.expected_channel_source.metadata_sha256!=result->channel_source_identity.metadata_sha256))
                            throw std::invalid_argument("Recorded channel metadata no longer matches its original preview");
                        RecordedRangeDigest identity(*file,*input.channel_file_range,source,job->stop,input.channel_source_budget.metadata_payload_bytes);
                        processed=std::make_shared<ProcessedSignal>(extract_signal_source_channel(file,*input.channel_file_range,channel,input.channel_source_budget,job->stop,[&](const auto& window,auto central){identity.append(window,central);}));
                        if(job->stop.stop_requested())continue;
                        result->channel_source_identity.normalized_source_sha256=identity.finish();
                        if(!input.expected_channel_source.normalized_source_sha256.empty() && input.expected_channel_source.normalized_source_sha256!=result->channel_source_identity.normalized_source_sha256)
                            throw std::invalid_argument("Recorded channel samples no longer match their saved input identity");
                    }else processed=std::make_shared<ProcessedSignal>(extract_signal_channel(source,channel,job->stop));
                    if(!processed->sample_count() && !job->stop.stop_requested()) {
                        result->error_key="channel.error.short";throw std::invalid_argument("No complete channel filter support is available");
                    }
                    auto processed_options=input.options;processed_options.output_payload_bytes=std::min(processed_options.output_payload_bytes,remaining-processed->payload_bytes());
                    result->channel_analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(std::move(processed),processed_options,job->stop));
                    result->error_key.clear();
                }
            }
            if(input.research_options && !job->stop.stop_requested()) {
                result->error_key="window.error.configuration";result->research_processed=input.research_processed;
                const auto analysis=input.research_processed?result->channel_analysis:result->analysis;
                if(!analysis)throw std::invalid_argument("Processed window research requires an extracted channel");
                const auto range=input.research_range.value_or(SignalSampleRange{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()});
                result->research=std::make_shared<WindowResearch>(research_signal_window(analysis,range,*input.research_options,job->stop));result->error_key.clear();
            }
            if(input.model && !job->stop.stop_requested()) {
                auto request=*input.model;result->error_key="model.error.configuration";
                const auto analysis=request.processed?result->channel_analysis:result->analysis;
                if(!analysis){result->error_key="model.error.input";throw std::invalid_argument("Processed reference comparison requires an extracted channel");}
                const auto provider=reference_model_provider(request.provider_id);
                if(provider.protocol_version<2){result->error_key="model.error.capability";throw std::invalid_argument("A bounded desktop model requires declared generator workspace");}
                if(request.method_version!=provider.method_version){result->error_key="model.error.version";throw std::invalid_argument("Unsupported saved reference model method");}
                const auto clock=analysis->processed?analysis->processed->clock:SignalSampleClock{0,analysis->sample_rate(),analysis->sample_rate()};
                if(request.expected_clock && *request.expected_clock!=clock){result->error_key="model.error.clock";throw std::invalid_argument("Saved model input clock does not match observations");}
                request.expected_clock=clock;request.range=request.range.value_or(SignalSampleRange{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()});
                const auto used=waveformPayload(*result)+(result->research?result->research->payload_bytes():0)+(!input.recovery && result->recovery?result->recovery->payload_bytes()+result->recovery_request->budget.metadata_bytes:0);
                if(used>input.result_payload_bytes || request.fit.workspace_payload_bytes>input.result_payload_bytes-used)throw std::length_error("Model workspace exceeds the combined result budget");
                request.fit.result_payload_bytes=std::min(request.fit.result_payload_bytes,input.result_payload_bytes-used-request.fit.workspace_payload_bytes);
                result->model=std::make_shared<ReferenceComparison>(compare_reference_model(analysis,*request.range,provider,request.parameters,request.fit,job->stop));result->model_request=std::move(request);result->error_key.clear();
            }
            if(input.recovery && !job->stop.stop_requested()) {
                auto request=*input.recovery;result->error_key="recovery.error.configuration";const auto analysis=request.processed?result->channel_analysis:result->analysis;
                if(!analysis){result->error_key="recovery.error.input";throw std::invalid_argument("Processed recovery requires an extracted channel");}
                const auto provider=recovery_provider(request.provider_id);
                if(request.method_version!=provider.method_version){result->error_key="recovery.error.version";throw std::invalid_argument("Unsupported recovery method version");}
                const auto clock=analysis->processed?analysis->processed->clock:SignalSampleClock{0,analysis->sample_rate(),analysis->sample_rate()};
                if(request.expected_clock && *request.expected_clock!=clock){result->error_key="recovery.error.clock";throw std::invalid_argument("Saved recovery clock differs from observations");}
                request.expected_clock=clock;request.range=request.range.value_or(SignalSampleRange{analysis->first_sample(),analysis->end_sample()-analysis->first_sample()});
                auto used=waveformPayload(*result)+(result->research?result->research->payload_bytes():0)+(result->model?result->model->payload_bytes():0);
                if(used>input.result_payload_bytes || request.budget.workspace_payload_bytes>input.result_payload_bytes-used)throw std::length_error("Recovery workspace exceeds the combined payload budget");used+=request.budget.workspace_payload_bytes;
                // JSON wire allowance is separate from capacity-based tables;
                // this does not measure JSON allocator or process heap usage.
                if(request.budget.metadata_bytes>input.result_payload_bytes-used)throw std::length_error("Recovery metadata allowance exceeds the combined payload budget");used+=request.budget.metadata_bytes;
                request.budget.result_payload_bytes=std::min(request.budget.result_payload_bytes,input.result_payload_bytes-used);
                result->recovery=std::make_shared<RecoveryResult>(recover_signal(analysis,*request.range,provider,request.parameters,request.budget,request.event,job->stop));result->recovery_request=std::move(request);result->error_key.clear();
            }
            const auto used=waveformPayload(*result)+(result->research?result->research->payload_bytes():0)+(result->model?result->model->payload_bytes():0)+(result->recovery?result->recovery->payload_bytes()+result->recovery_request->budget.metadata_bytes:0);
            if(used>input.result_payload_bytes)throw std::length_error("Completed research exceeds the combined result budget");
        }catch(const std::length_error&){result->failure=std::current_exception();result->error_key="error.budget";}
        catch(...){result->failure=std::current_exception();if(result->error_key.empty())result->error_key=job->input.cached && !job->input.recording?"error.capture":"error.recording";}
        if(job->stop.stop_requested())continue;
        {std::lock_guard lock(mutex_);if(job->generation==generation_ && !stop.stop_requested())std::atomic_store(&latest_, std::shared_ptr<const RecordedWaveformCompletion>(std::move(result)));}
    }
}
}
