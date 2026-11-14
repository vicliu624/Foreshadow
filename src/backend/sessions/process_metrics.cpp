#include "process_metrics.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace signalscope {
ProcessMetrics readProcessMetrics() {
    ProcessMetrics metrics;
#if defined(_WIN32)
    const auto process=GetCurrentProcess();
    FILETIME created{},exited{},kernel{},user{};
    if(GetProcessTimes(process,&created,&exited,&kernel,&user)) {
        const auto ticks=[](FILETIME value) {
            return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;
        };
        metrics.cpu_seconds=double(ticks(kernel)+ticks(user))/10'000'000;
    }
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb=sizeof(memory);
    if(K32GetProcessMemoryInfo(process,&memory,memory.cb)) {
        metrics.resident_bytes=memory.WorkingSetSize;
        metrics.peak_resident_bytes=memory.PeakWorkingSetSize;
    }
#elif defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) {
        const auto seconds=[](timeval time) {return time.tv_sec+time.tv_usec/1e6;};
        metrics.cpu_seconds=seconds(usage.ru_utime)+seconds(usage.ru_stime);
#ifdef __linux__
        if(usage.ru_maxrss>=0)metrics.peak_resident_bytes=std::uint64_t(usage.ru_maxrss)*1024;
#endif
    }
#ifdef __APPLE__
    mach_task_basic_info_data_t memory{};
    mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&memory),&count)==KERN_SUCCESS) {
        metrics.resident_bytes=memory.resident_size;
        metrics.peak_resident_bytes=memory.resident_size_max;
    }
#else
    std::ifstream memory("/proc/self/statm");
    std::uint64_t virtual_pages=0,resident_pages=0;
    const auto page_size=sysconf(_SC_PAGESIZE);
    if(page_size>0 && memory>>virtual_pages>>resident_pages)
        metrics.resident_bytes=resident_pages*static_cast<std::uint64_t>(page_size);
#endif
#endif
    return metrics;
}
}
