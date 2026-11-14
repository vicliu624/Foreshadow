#pragma once

#include <QObject>
#include <exception>
#include <filesystem>
#include <functional>
#include <thread>

namespace signalscope::desktop {
class RecordingExportJob final : public QObject {
public:
    using Completion=std::function<void(std::exception_ptr)>;
    bool busy() const {return worker_.joinable();}
    bool start(std::filesystem::path metadata,std::filesystem::path destination,Completion completed);
private:
    std::jthread worker_;
};
}
