#pragma once
#include "signalscope/backend/dsp/analysis_priority.hpp"
#include "signalscope/backend/dsp/analysis_workers.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace signalscope {
// A bounded persistent pool. Each batch owns disjoint channel state; callers
// collect its results only after the barrier, in source channel order.
class ParallelChannels {
  public:
    explicit ParallelChannels(std::size_t count) {
        count = std::min<std::size_t>(count, 32);
        if (count <= 1)
            return;
        try {
            for (std::size_t i = 0; i < count; ++i)
                workers_.emplace_back([this] { work(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~ParallelChannels() { shutdown(); }
    std::size_t workers() const { return std::max<std::size_t>(1, workers_.size()); }
    void run(std::size_t count, const std::function<void(std::size_t)> &task) {
        if (workers_.empty()) {
            for (std::size_t i = 0; i < count; ++i)
                task(i);
            return;
        }
        std::unique_lock lock(mutex_);
        task_ = task;
        count_ = count;
        next_ = 0;
        arrived_ = 0;
        error_ = nullptr;
        ++generation_;
        ready_.notify_all();
        done_.wait(lock, [&] { return arrived_ == workers_.size(); });
        task_ = {};
        if (error_)
            std::rethrow_exception(error_);
    }

  private:
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        workers_.clear();
    }
    void work() {
        lowerAnalysisPriority();
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_)
                    return;
                seen = generation_;
            }
            try {
                for (auto i = next_.fetch_add(1); i < count_; i = next_.fetch_add(1))
                    task_(i);
            } catch (...) {
                std::lock_guard lock(mutex_);
                if (!error_)
                    error_ = std::current_exception();
            }
            {
                std::lock_guard lock(mutex_);
                ++arrived_;
            }
            done_.notify_one();
        }
    }
    std::vector<std::jthread> workers_;
    std::mutex mutex_;
    std::condition_variable ready_, done_;
    std::function<void(std::size_t)> task_;
    std::atomic<std::size_t> next_ = 0;
    std::size_t count_ = 0, arrived_ = 0;
    std::uint64_t generation_ = 0;
    std::exception_ptr error_;
    bool stopping_ = false;
};
} // namespace signalscope
