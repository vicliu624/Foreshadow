#pragma once
#include "signalscope/backend/iq/error.hpp"
#include "signalscope/backend/iq/sample_format.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace signalscope {
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
// 保留采集失败时的结构化统计，界面仍显示简短错误原因。
class SourceError : public OperationError {
public:
    SourceError(const std::string& message,Json report)
        : OperationError(ErrorCode::receiver_stream,message),details(std::move(report)) {}
    Json details;
};
struct Config {
    std::uint32_t rate = 2000000;
    std::uint64_t frequency = 470500000;
    int gain_tenths = 197;
    bool auto_gain = false;
    std::uint32_t device = 0;
    std::string antenna;
    std::string device_serial;
    std::string input_mode="auto"; // auto, tuner, direct_i, direct_q
    double if_bandwidth_hz=0; // 0 selects an automatic device filter
    Json receiver_settings=Json::object(); // Plugin-specific typed settings.
    std::size_t block_bytes = 65536;
    std::size_t queue_blocks = 64;
    std::size_t analysis_queue_bytes = 16*1024*1024;
    double analysis_queue_seconds = 5;
    double seconds = 10;
    double settle_seconds = 0.1;
    // Optional minimum RF passband needed by the caller.
    double required_passband_hz = 0;
    void validate() const;
};
std::uint64_t sample_count(double seconds, std::uint32_t rate);
std::string utc_now();
void write_json(const std::filesystem::path&, const Json&);

// 满队列明确拒绝，调用者必须停止或记录缺口；绝不默默覆盖。
template<class T> class BoundedQueue {
public:
    using Weight=std::function<std::size_t(const T&)>;
    explicit BoundedQueue(std::size_t capacity,Weight weight=[](const T&){return std::size_t{1};})
        : capacity_(capacity),weight_(std::move(weight)) {
        if (!capacity) throw std::invalid_argument("Queue capacity must be positive");
    }
    bool try_push(T value) {
        const auto cost=weight_(value);
        std::lock_guard lock(mutex_);
        if (closed_ || !cost || cost>capacity_ || used_>capacity_-cost) return false;
        queue_.push_back({std::move(value),cost});used_+=cost;
        high_water_ = std::max(high_water_, queue_.size());
        high_water_weight_=std::max(high_water_weight_,used_);
        cv_.notify_one(); return true;
    }
    bool pop(T& value) {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&]{return closed_ || !queue_.empty();});
        if (queue_.empty()) return false;
        used_-=queue_.front().cost;value = std::move(queue_.front().value); queue_.pop_front(); return true;
    }
    bool try_pop(T& value) {
        std::lock_guard lock(mutex_);
        if(queue_.empty())return false;
        used_-=queue_.front().cost;value=std::move(queue_.front().value);queue_.pop_front();return true;
    }
    void close() { std::lock_guard lock(mutex_); closed_ = true; cv_.notify_all(); }
    std::size_t high_water() const { std::lock_guard lock(mutex_); return high_water_; }
    std::size_t high_water_weight() const {std::lock_guard lock(mutex_);return high_water_weight_;}
    std::size_t used_weight() const {std::lock_guard lock(mutex_);return used_;}
private:
    const std::size_t capacity_;
    const Weight weight_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    struct Item {T value;std::size_t cost;};
    std::deque<Item> queue_;
    std::size_t high_water_ = 0;
    std::size_t used_=0,high_water_weight_=0;
    bool closed_ = false;
};

struct PowerStats {
    explicit PowerStats(SampleFormat format=SampleFormat::cu8):format_(format){}
    std::uint64_t pairs = 0, clipped_pairs = 0;
    double energy = 0, sum_i = 0, sum_q = 0;
    void add(std::span<const std::uint8_t>);
    Json json() const;
private:
    void add_ci16_le(std::span<const std::uint8_t>);
    SampleFormat format_;
};
class Recorder {
public:
    Recorder(const std::filesystem::path& directory, const Config&, const std::string& source,
             SampleFormat format=SampleFormat::cu8);
    void append(std::span<const std::uint8_t>);
    void finish(const std::string& status, const Json& extra = Json::object());
    void annotate(const Json& annotation);
    std::uint64_t pairs() const { return pairs_; }
    SampleFormat format() const { return format_; }
    ~Recorder();
private:
    void publish();
    std::filesystem::path directory_;
    std::ofstream data_;
    Json meta_;
    SampleFormat format_;
    std::uint64_t pairs_ = 0;
    bool finished_ = false;
};

struct ReplayInfo {
    std::filesystem::path data_path;
    Json metadata;
    std::uint32_t rate = 0;
    double frequency = 0;
    std::uint64_t pairs = 0;
    SampleFormat format = SampleFormat::cu8;
};
ReplayInfo inspect_recording(const std::filesystem::path& metadata);
using BlockSink = std::function<void(std::span<const std::uint8_t>, std::uint64_t)>;
void replay(const ReplayInfo&, std::uint64_t first, std::uint64_t count,
            double speed, std::size_t block_bytes, const BlockSink&,
            const std::function<bool()>& cancelled);
}
