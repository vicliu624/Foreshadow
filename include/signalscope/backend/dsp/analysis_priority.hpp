#pragma once
#ifdef __linux__
#include <sys/resource.h>
#endif
namespace signalscope {
// Linux niceness is per thread. Keep receiver callbacks and the recording
// writer at their normal priority when DSP consumes every available core.
inline void lowerAnalysisPriority() {
#ifdef __linux__
    setpriority(PRIO_PROCESS,0,8);
#endif
}
}
