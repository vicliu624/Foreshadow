#pragma once
#include "plugins/desktop/research/live_provider.hpp"
struct ResearchWorkspaceFixture {
    signalscope::desktop::ResearchRecordingContext recording;
    unsigned opened=0,failures=0;
    bool busy=false,rejectActions=false;
};
signalscope::desktop::LiveResearchProvider researchWorkspaceFixture(
    const std::filesystem::path&,std::shared_ptr<ResearchWorkspaceFixture>);
