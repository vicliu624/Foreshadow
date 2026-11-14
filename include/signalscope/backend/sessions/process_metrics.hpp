#pragma once
#include <cstdint>
#include <optional>
namespace signalscope {
struct ProcessMetrics {
    std::optional<double> cpu_seconds;
    std::optional<std::uint64_t> resident_bytes;
    std::optional<std::uint64_t> peak_resident_bytes;
};
ProcessMetrics readProcessMetrics();
}
