#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include <memory>
namespace signalscope {
Json readEventStoreSnapshot(const std::filesystem::path& root);

struct RetentionPolicy {
    double ring_seconds=8, pre_seconds=.5, post_seconds=.5, max_event_seconds=8;
    std::uint64_t ring_bytes=32*1024*1024, max_event_bytes=32*1024*1024;
    std::uint64_t quota_bytes=1024ULL*1024*1024, reserve_bytes=256ULL*1024*1024;
    std::size_t max_files=256;
    double max_age_hours=0; // 零：不按年龄删除有效事件，仅受容量/数量限制。
    bool diagnostic_candidates=false; // 临时诊断：至多一段 8 MiB，120 秒内丢弃。
};
class EventStore {
public:
    EventStore(const std::filesystem::path&,const Config&,const RetentionPolicy&,SampleFormat format=SampleFormat::cu8);
    ~EventStore();
    void feed(std::span<const std::uint8_t>,std::uint64_t first,const Json& events);
    void finish(const std::string& reason="monitor_stopped");
    void discontinuity(std::uint64_t next_sample);
    Json status() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
