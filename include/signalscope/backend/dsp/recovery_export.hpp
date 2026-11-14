#pragma once
#include "recovery.hpp"

namespace signalscope {
// The sink must consume the view before returning. Serialization is streamed;
// no second JSON tree proportional to the value/link graph is constructed.
using RecoveryExportSink=std::function<void(std::string_view)>;
inline constexpr std::size_t recoveryExportMaximumBytes=64*1024*1024;
bool recovery_bytes_exportable(const RecoveryResult&,RecoveryLink);
void export_recovery_json(const RecoveryResult&,const Json& source_analysis,const RecoveryExportSink&,
    std::size_t maximum_bytes=recoveryExportMaximumBytes,std::stop_token stop={});
void export_recovery_bytes(const RecoveryResult&,RecoveryLink,const RecoveryExportSink&,
    std::size_t maximum_bytes=recoveryExportMaximumBytes,std::stop_token stop={});
}
