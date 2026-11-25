#pragma once
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/resource.h>
#endif

namespace amc_test {
inline std::size_t openResourceCount() {
#ifdef _WIN32
    DWORD count=0;
    if(!GetProcessHandleCount(GetCurrentProcess(),&count))throw std::runtime_error("Cannot count process handles");
    return count;
#elif defined(__linux__)
    // The directory iterator owns one descriptor until it is destroyed.
    std::size_t count=0;
    for(const auto& entry:std::filesystem::directory_iterator("/proc/self/fd")){(void)entry;++count;}
    return count-1;
#elif defined(__APPLE__)
    rlimit limit{};
    if(getrlimit(RLIMIT_NOFILE,&limit))throw std::runtime_error("Cannot inspect descriptor limit");
    std::size_t count=0;
    for(rlim_t fd=0;fd<limit.rlim_cur;++fd)if(fcntl(static_cast<int>(fd),F_GETFD)!=-1)++count;
    return count;
#else
#error Resource lifetime acceptance needs a native process resource counter
#endif
}
}
