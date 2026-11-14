#pragma once

#include <stdexcept>

namespace signalscope {
enum class ErrorCode {
    receiver_unavailable,
    receiver_open,
    receiver_configuration,
    receiver_antenna_frequency,
    receiver_passband,
    receiver_stream,
    recording_open,
    recording_invalid,
    recording_format,
    recording_write,
    recording_space,
    event_store_unavailable,
    export_destination_exists,
    recording_in_progress,
};

class OperationError : public std::runtime_error {
public:
    OperationError(ErrorCode code, const std::string& detail)
        : std::runtime_error(detail), code_(code) {}
    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};
}
