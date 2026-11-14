#pragma once
#include <mutex>
namespace signalscope {
// FFTW execution on separate plans is concurrent; its planner and plan
// destruction share global state. All DSP and detector plans use this lock.
inline std::mutex& fftwPlannerMutex(){static std::mutex mutex;return mutex;}
}
