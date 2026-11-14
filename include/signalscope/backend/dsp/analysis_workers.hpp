#pragma once
#include <algorithm>
#include <thread>

namespace signalscope {
inline unsigned recommendedAnalysisWorkers() {
    const auto available = std::thread::hardware_concurrency();
    return std::clamp(available > 2 ? available - 2 : 1, 1u, 16u);
}
}
