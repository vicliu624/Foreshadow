#include "live_input.hpp"
#include "core/demod.hpp"
#include "core/input_policy.hpp"
#include <thread>
#include <cmath>
namespace signalscope::adsb {
struct LiveInput::State {
    struct Block {Bytes bytes;std::uint64_t first;IqStreamParameters stream;bool gap;};
    mutable std::mutex mutex;std::condition_variable wake;std::deque<Block> queue;
    LiveSnapshot published;bool closed=false;
    std::atomic<std::shared_ptr<const std::vector<Aircraft>>> aircraft;
    std::jthread worker;
    State():worker([this]{run();}){}
    ~State(){{std::lock_guard lock(mutex);closed=true;}wake.notify_all();worker.join();}
    void run() {
        Tracker tracker;std::unique_ptr<Demodulator> demod;std::optional<IqStreamParameters> stream;
        std::uint64_t next=0;std::vector<std::complex<float>> samples;
        for(;;){Block block;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return closed || !queue.empty();});if(closed)return;
                block=std::move(queue.front());queue.pop_front();published.queued_bytes-=block.bytes.size();}
            try {
                const bool changed=!stream || stream->sample_rate!=block.stream.sample_rate || stream->format!=block.stream.format || stream->center_frequency_hz!=block.stream.center_frequency_hz;
                const bool gap=changed || block.gap || block.first!=next;
                const bool observedGap=block.gap || (stream && (changed || block.first!=next));
                if(changed){demod=std::make_unique<Demodulator>(block.stream.sample_rate);stream=block.stream;}
                if(gap){demod->reset();tracker.clear();}
                const auto stride=bytes_per_pair(block.stream.format);samples.clear();samples.reserve(block.bytes.size()/stride);
                for(std::size_t at=0;at<block.bytes.size();at+=stride)samples.push_back(read_iq_pair(block.bytes.data()+at,block.stream.format));
                demod->feed(samples,block.first,gap,[&](const Frame& frame){tracker.observe(frame.fields,double(frame.first_sample)/block.stream.sample_rate);});
                next=block.first+samples.size();const double time=double(next)/block.stream.sample_rate;
                aircraft.store(std::make_shared<const std::vector<Aircraft>>(tracker.snapshot(time)));
                {std::lock_guard lock(mutex);published.source_seconds=time;published.processed_pairs+=samples.size();if(observedGap)++published.discontinuities;published.error.clear();}
            }catch(const std::exception& error){aircraft.store({});std::lock_guard lock(mutex);published.error=error.what();published.rejected_pairs+=block.bytes.size()/bytes_per_pair(block.stream.format);demod.reset();stream.reset();tracker.clear();}
        }
    }
};
LiveInput::LiveInput():state_(std::make_unique<State>()){}
LiveInput::~LiveInput()=default;
void LiveInput::offer(const IqBlockView& block) {
    const auto stride=bytes_per_pair(block.stream.format),pairs=block.bytes.size()/stride;
    std::lock_guard lock(state_->mutex);auto& status=state_->published;status.offered_pairs+=pairs;
    if(state_->closed || block.bytes.size()%stride || block.bytes.size()>capacity_bytes-status.queued_bytes || !covers_band(block.stream.center_frequency_hz,block.stream.sample_rate,block.stream.sample_rate)){status.rejected_pairs+=pairs;return;}
    state_->queue.push_back({Bytes(block.bytes.begin(),block.bytes.end()),block.first_sample,block.stream,block.discontinuous});
    status.queued_bytes+=block.bytes.size();status.high_water_bytes=std::max(status.high_water_bytes,status.queued_bytes);state_->wake.notify_one();
}
LiveSnapshot LiveInput::snapshot() const {
    LiveSnapshot result;{std::lock_guard lock(state_->mutex);result=state_->published;}
    if(const auto aircraft=state_->aircraft.load())result.aircraft=*aircraft;
    return result;
}
}
