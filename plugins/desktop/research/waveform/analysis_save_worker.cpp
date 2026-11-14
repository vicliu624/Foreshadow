#include "analysis_save_worker.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QSaveFile>
#include <QString>
#include <QUuid>

namespace signalscope::desktop {
WaveformAnalysisSaveWorker::WaveformAnalysisSaveWorker():thread_([this](auto stop){run(stop);}){}
WaveformAnalysisSaveWorker::~WaveformAnalysisSaveWorker(){thread_.request_stop();cancel();condition_.notify_all();if(thread_.joinable())thread_.join();}
std::uint64_t WaveformAnalysisSaveWorker::request(WaveformSaveRequest input) {
    std::lock_guard lock(mutex_);if(busy_)throw std::logic_error("A waveform save is already active");
    active_stop_=std::stop_source{};const auto generation=++generation_;pending_=Job{generation,std::move(input),active_stop_.get_token()};std::atomic_store(&latest_, {});busy_=true;condition_.notify_one();return generation;
}
void WaveformAnalysisSaveWorker::cancel(){std::lock_guard lock(mutex_);active_stop_.request_stop();condition_.notify_all();}
std::shared_ptr<const WaveformSaveCompletion> WaveformAnalysisSaveWorker::takeLatest(){return std::atomic_exchange(&latest_, {});}
void WaveformAnalysisSaveWorker::run(std::stop_token stop) {
    while(!stop.stop_requested()) {
        std::optional<Job> job;
        {std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stop.stop_requested() || pending_.has_value();});if(stop.stop_requested())return;job=std::move(pending_);pending_.reset();}
        auto result=std::make_shared<WaveformSaveCompletion>();result->generation=job->generation;result->purpose=job->input.purpose;
        try {
            auto& input=job->input;result->configuration_path=std::filesystem::absolute(input.configuration_path);
            for(const auto& source:input.protected_paths)if(!source.empty()) {
                std::error_code error;if(std::filesystem::exists(result->configuration_path) && std::filesystem::equivalent(result->configuration_path,source,error))throw std::invalid_argument("Waveform output cannot overwrite its source");
                if(error)throw std::filesystem::filesystem_error("Cannot verify protected waveform source",result->configuration_path,source,error);
            }
            if(input.purpose!=WaveformFilePurpose::analysis) {
                if(!input.recovery)throw std::invalid_argument("Recovery evidence is unavailable");
                const auto binary=input.purpose==WaveformFilePurpose::recovery_bytes;
                if(result->configuration_path.extension()!=std::filesystem::path(binary?".bin":".json"))throw std::invalid_argument("Invalid recovery export suffix");
                if(binary && (!input.selection || !recovery_bytes_exportable(*input.recovery,*input.selection))) {
                    result->error_key="recovery.export.unknown";throw std::invalid_argument("Unknown recovery bytes cannot be exported");
                }
                QSaveFile output(QString::fromStdString(path_to_utf8(result->configuration_path)));output.setDirectWriteFallback(false);
                if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot create atomic recovery export");
                const auto write=[&](std::string_view chunk){if(output.write(chunk.data(),static_cast<qint64>(chunk.size()))!=static_cast<qint64>(chunk.size()))throw std::runtime_error("Recovery export write failed");};
                if(binary)export_recovery_bytes(*input.recovery,*input.selection,write,input.export_maximum_bytes,job->stop);
                else export_recovery_json(*input.recovery,input.configuration,write,input.export_maximum_bytes,job->stop);
                if(job->stop.stop_requested()){output.cancelWriting();throw std::runtime_error("Recovery export canceled before commit");}
                if(!output.commit())throw std::runtime_error("Atomic recovery export commit failed");
                result->export_committed=true;
            }else {
            if(!input.configuration.is_object() || input.configuration.at("format")!="org.amc.waveform-analysis" ||
                (input.configuration.at("version")!=1 && input.configuration.at("version")!=2 && input.configuration.at("version")!=3 && input.configuration.at("version")!=4 && input.configuration.at("version")!=5 && input.configuration.at("version")!=6 && input.configuration.at("version")!=7 && input.configuration.at("version")!=8))throw std::invalid_argument("Unsupported analysis configuration");
            if(result->configuration_path.extension()!=std::filesystem::path(".json"))throw std::invalid_argument("Analysis configuration needs a JSON suffix");
            if(job->stop.stop_requested())throw std::runtime_error("Waveform save canceled");
            if(input.capture) {
                const auto name="amc-capture-"+QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
                result->capture_metadata=write_signal_archive(result->configuration_path.parent_path()/name,*input.capture,job->stop,input.source_payload_bytes);
                if(job->stop.stop_requested())throw std::runtime_error("Waveform save canceled after committing a capture");
                const auto info=inspect_signal_source(result->capture_metadata);
                const auto persisted=read_signal_source(info,0,info.storage.pairs,job->stop,input.source_payload_bytes);
                if(job->stop.stop_requested())throw std::runtime_error("Waveform save canceled during verification");
                result->identity=waveform_source_identity(info.storage.metadata,persisted,job->stop);
                const auto expected=input.capture_identity.normalized_window_sha256.empty()?waveform_source_identity(Json::object(),*input.capture,job->stop).normalized_window_sha256:input.capture_identity.normalized_window_sha256;
                if(job->stop.stop_requested())throw std::runtime_error("Waveform save canceled during verification");
                if(result->identity.normalized_window_sha256!=expected){result->error_key="save.error.verification";throw std::runtime_error("Persisted signal window does not match its immutable source");}
                result->verified_capture=true;
                if(input.configuration.at("version")==1)input.configuration["version"]=2;
                input.configuration["source"]={{"metadata_path",path_to_utf8(result->capture_metadata.lexically_relative(result->configuration_path.parent_path()),PathTextFormat::generic)},{"path_base","analysis-directory"},
                    {"first_file_sample","0"},{"sample_count",std::to_string(persisted.sample_count())},{"metadata_sha256",result->identity.metadata_sha256},{"normalized_window_sha256",result->identity.normalized_window_sha256}};
            }else {
                auto& source=input.configuration.at("source");const auto path=path_from_utf8(source.at("metadata_path").get<std::string>());
                if(path.is_absolute()) {
                    const auto relative=path.lexically_relative(result->configuration_path.parent_path());
                    if(!relative.empty() && *relative.begin()!=std::filesystem::path("..")) {
                        source["metadata_path"]=path_to_utf8(relative,PathTextFormat::generic);source["path_base"]="analysis-directory";if(input.configuration.at("version")==1)input.configuration["version"]=2;
                    }
                }
            }
            const auto serialized=input.configuration.dump(2)+"\n";
            if(serialized.size()>8*1024*1024){result->error_key="save.error.configuration_budget";throw std::length_error("Analysis configuration exceeds its persistence budget");}
            QSaveFile output(QString::fromStdString(path_to_utf8(result->configuration_path)));output.setDirectWriteFallback(false);
            if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot create atomic analysis configuration");
            for(std::size_t at=0;at<serialized.size();at+=65536) {
                if(job->stop.stop_requested()){output.cancelWriting();throw std::runtime_error("Waveform save canceled before commit");}
                const auto count=std::min<std::size_t>(65536,serialized.size()-at);
                if(output.write(serialized.data()+at,static_cast<qint64>(count))!=static_cast<qint64>(count))throw std::runtime_error("Analysis configuration write failed");
            }
            if(job->stop.stop_requested()){output.cancelWriting();throw std::runtime_error("Waveform save canceled before commit");}
            if(!output.commit())throw std::runtime_error("Atomic analysis configuration commit failed");
            result->configuration_committed=true;
            }
        }catch(const std::length_error&){result->failure=std::current_exception();if(result->error_key.empty())result->error_key=result->purpose==WaveformFilePurpose::analysis?"save.error.budget":"recovery.export.budget";}
        catch(...){result->failure=std::current_exception();if(result->error_key.empty())result->error_key=result->purpose==WaveformFilePurpose::analysis?"error.save":"recovery.export.error";}
        if(job->stop.stop_requested() && !result->configuration_committed && !result->export_committed){result->canceled=true;result->failure={};result->error_key.clear();}
        {std::lock_guard lock(mutex_);busy_=false;std::atomic_store(&latest_, std::shared_ptr<const WaveformSaveCompletion>(std::move(result)));}
    }
}
}
