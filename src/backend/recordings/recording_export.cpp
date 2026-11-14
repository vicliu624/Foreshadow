#include "signalscope/backend/recordings/recording_export.hpp"
#include "signalscope/backend/iq/iq.hpp"

namespace signalscope {
void export_recording(const std::filesystem::path& metadata,const std::filesystem::path& destination) {
    namespace fs=std::filesystem;
    const auto original=inspect_recording(metadata);
    const auto state=original.metadata.at("global").find("signalscope:status");
    if(state!=original.metadata.at("global").end() && *state=="recording")
        throw OperationError(ErrorCode::recording_in_progress,"Wait for the recording to finish before exporting");

    const auto pendingMeta=destination/".pending.sigmf-meta";
    const auto pendingData=destination/".pending.sigmf-data";
    const auto finalMeta=destination/"capture.sigmf-meta";
    const auto finalData=destination/"capture.sigmf-data";

    std::error_code error;
    if(!fs::create_directory(destination,error)) {
        if(!error || error==std::errc::file_exists)
            throw OperationError(ErrorCode::export_destination_exists,"Export destination already exists");
        throw OperationError(ErrorCode::recording_write,error.message());
    }
    bool dataPublished=false;
    const auto cleanup=[&]{
        fs::remove(pendingMeta,error);
        fs::remove(pendingData,error);
        if(dataPublished)fs::remove(finalData,error);
        fs::remove(destination,error);
    };
    try {
        fs::copy_file(metadata,pendingMeta);
        fs::copy_file(original.data_path,pendingData);
        const auto copied=inspect_recording(pendingMeta);
        if(copied.pairs!=original.pairs || copied.metadata!=original.metadata)
            throw OperationError(ErrorCode::recording_write,"Recording changed during export");
        fs::rename(pendingData,finalData);
        dataPublished=true;
        fs::rename(pendingMeta,finalMeta);
    } catch(const fs::filesystem_error& failure) {
        cleanup();
        throw OperationError(ErrorCode::recording_write,failure.what());
    } catch(...) {
        cleanup();
        throw;
    }
}
}
