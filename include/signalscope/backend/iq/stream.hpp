#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include <utility>
#include <vector>

namespace signalscope {
struct IqStreamParameters {
    std::uint32_t sample_rate;
    double center_frequency_hz;
    SampleFormat format;
};

struct IqBlockView {
    std::span<const std::uint8_t> bytes;
    std::uint64_t first_sample;
    IqStreamParameters stream;
    bool discontinuous;
};

using IqBlockSubscriber=std::function<void(const IqBlockView&)>;

// Register before starting a session. The caller owns the dispatch thread.
// Samples are borrowed for the duration of each callback.
class IqSubscribers {
public:
    void add(IqBlockSubscriber subscriber) {
        if(subscriber)subscribers_.push_back(std::move(subscriber));
    }

    bool empty() const noexcept {return subscribers_.empty();}

private:
    friend class IqPublisher;

    void publish(const IqBlockView& block) const noexcept {
        for(const auto& subscriber:subscribers_) {
            try {subscriber(block);}
            catch(...) {} // Optional consumers must not interrupt capture or other subscribers.
        }
    }

    std::vector<IqBlockSubscriber> subscribers_;
};

// Keep one publisher per delivery thread and session. A queue may skip
// samples on one thread while the source delivers a continuous stream.
class IqPublisher {
public:
    IqPublisher(const IqSubscribers& subscribers,IqStreamParameters stream)
        :subscribers_(subscribers),stream_(stream),stride_(bytes_per_pair(stream.format)) {}

    void publish(std::span<const std::uint8_t> bytes,std::uint64_t first_sample) noexcept {
        if(subscribers_.empty())return;
        const bool discontinuous=first_sample!=next_sample_;
        next_sample_=first_sample+bytes.size()/stride_;
        subscribers_.publish({bytes,first_sample,stream_,discontinuous});
    }

private:
    const IqSubscribers& subscribers_;
    const IqStreamParameters stream_;
    const std::size_t stride_;
    std::uint64_t next_sample_=0;
};
}
