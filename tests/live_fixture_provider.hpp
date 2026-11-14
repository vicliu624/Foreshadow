#pragma once
#include "plugins/desktop/research/live_provider.hpp"
#include <atomic>
#include <filesystem>
#include <mutex>
struct LiveFixtureState {
    std::atomic<std::uint64_t> pairs=0;
    std::atomic<bool> reject=false;
    unsigned stopped=0,failures=0;
    bool busy=false;
    signalscope::desktop::LiveResearchContext context;
    signalscope::Bytes retained;
    std::uint64_t first=0;
    std::mutex mutex;
    signalscope::ReceptionAcquire acquire;
    std::shared_ptr<signalscope::ReceptionLease> lease;
};
signalscope::desktop::LiveResearchProvider liveFixtureProvider(const std::filesystem::path&,const QString&,std::shared_ptr<LiveFixtureState>);
signalscope::desktop::LiveResearchProvider independentReceptionProvider(const std::filesystem::path&,std::shared_ptr<LiveFixtureState>);
