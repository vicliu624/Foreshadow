#include "desktop_recording_export.hpp"
#include "signalscope/backend/recordings/recording_export.hpp"
#include <utility>

namespace signalscope::desktop {
bool RecordingExportJob::start(std::filesystem::path metadata,std::filesystem::path destination,Completion completed) {
    if(busy())return false;
    worker_=std::jthread([this,metadata=std::move(metadata),destination=std::move(destination),
                          completed=std::move(completed)]{
        std::exception_ptr failure;
        try {export_recording(metadata,destination);}
        catch(...) {failure=std::current_exception();}
        QMetaObject::invokeMethod(this,[this,completed,failure]{
            worker_.join();
            completed(failure);
        },Qt::QueuedConnection);
    });
    return true;
}
}
