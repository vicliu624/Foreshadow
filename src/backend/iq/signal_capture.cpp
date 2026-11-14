#include "signalscope/backend/iq/signal_capture.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace signalscope {
struct SignalCapture::State {
    struct Epoch {
        std::uint64_t generation=0,base_serial=0;
        IqStreamParameters stream{};std::string source;Json context;
        std::atomic<std::uint64_t> accepted=0,processed=0,rejected=0,canceled=0,processing_rejected=0;
        std::atomic<std::uint64_t> malformed=0,oversized=0,contention=0,full=0,configuration=0;
        std::atomic<std::size_t> peak=0,in_flight=0;
        // All cross-thread access uses shared_ptr atomic functions, including libc++.
        std::shared_ptr<const SignalWindowStatus> window;
        std::shared_ptr<const std::string> error;
    };
    struct Packet {std::shared_ptr<Epoch> epoch;Bytes bytes;std::uint64_t first=0,serial=0;std::size_t samples=0;bool discontinuous=false;};
    struct Request {std::shared_ptr<Epoch> epoch;std::uint64_t id=0,target=0;};
    explicit State(SignalCaptureBudget value):budget(value) {
        (void)SignalWindowBuffer(budget.retention);
        if(!budget.queue_payload_bytes || !budget.maximum_block_bytes || budget.maximum_block_bytes>budget.queue_payload_bytes ||
            !budget.maximum_queued_blocks || budget.maximum_queued_blocks>65536 || budget.queue_payload_bytes>256*1024*1024)
            throw std::invalid_argument("Invalid live signal capture budget");
        worker=std::jthread([this](auto stop){run(stop);});
    }
    ~State(){close();}
    void close() noexcept {
        {std::lock_guard lock(mutex);if(closed)return;closed=true;std::atomic_store(&active, {});discardQueue();worker.request_stop();}
        condition.notify_all();if(worker.joinable())worker.join();
    }
    SignalCaptureStatus describe(const std::shared_ptr<Epoch>& epoch,bool enabled,std::size_t queued,std::size_t blocks) const {
        SignalCaptureStatus result;result.inactive_blocks=inactive.load();result.stale_generation_samples_total=stale_samples.load();result.stale_generation_blocks_total=stale_blocks.load();if(!epoch)return result;
        result.generation=epoch->generation;result.active=enabled;result.queued_payload_bytes=queued;result.queued_blocks=blocks;result.peak_queued_payload_bytes=epoch->peak.load();result.in_flight_payload_bytes=epoch->in_flight.load();
        result.accepted_samples=epoch->accepted.load();result.processed_samples=epoch->processed.load();result.rejected_samples=epoch->rejected.load();result.canceled_samples=epoch->canceled.load();result.processing_rejected_samples=epoch->processing_rejected.load();
        result.malformed_blocks=epoch->malformed.load();result.oversized_blocks=epoch->oversized.load();result.contention_blocks=epoch->contention.load();result.queue_full_blocks=epoch->full.load();result.configuration_rejected_blocks=epoch->configuration.load();
        if(const auto value=std::atomic_load(&epoch->window))result.window=*value;
        if(const auto value=std::atomic_load(&epoch->error))result.error=*value;
        return result;
    }
    void discardQueue() {
        for(const auto& packet:queue)packet.epoch->canceled+=packet.samples;
        queue.clear();queued_bytes=0;pending.reset();std::atomic_store(&latest, {});
    }
    void run(std::stop_token stop) {
        std::shared_ptr<Epoch> current;SignalWindowBuffer retained(budget.retention);std::uint64_t processed_serial=0;
        while(!stop.stop_requested()) {
            std::optional<Packet> packet;std::optional<Request> snapshot;
            {std::unique_lock lock(mutex);condition.wait(lock,[&]{return stop.stop_requested() || !queue.empty() || pending || std::atomic_load(&active)!=current;});
                if(stop.stop_requested())return;
                const auto selected=std::atomic_load(&active);if(selected!=current){current=selected;retained.clear();processed_serial=current?current->base_serial:serial;}
                if(pending && pending->epoch==current && processed_serial>=pending->target){snapshot=std::move(pending);pending.reset();}
                else if(!queue.empty()){packet=std::move(queue.front());queue.pop_front();queued_bytes-=packet->bytes.capacity();packet->epoch->in_flight=packet->bytes.capacity();}
                else continue;
            }
            if(packet) {
                try {retained.append({packet->bytes,packet->first,packet->epoch->stream,packet->discontinuous});packet->epoch->processed+=packet->samples;
                    std::atomic_store(&packet->epoch->window,std::make_shared<const SignalWindowStatus>(retained.status()));
                }catch(const std::exception& error){packet->epoch->processing_rejected+=packet->samples;std::atomic_store(&packet->epoch->error,std::make_shared<const std::string>(error.what()));}
                packet->epoch->in_flight=0;processed_serial=packet->serial;
            }else if(snapshot) {
                auto result=std::make_shared<SignalCaptureSnapshot>();result->request=snapshot->id;result->generation=current->generation;
                try {
                    auto window=std::make_shared<SignalWindow>(retained.snapshot(current->source));
                    if(window->segments.empty())throw std::runtime_error("empty_capture");
                    window->processing.push_back({{"method","retained-live-window"},{"version",1},{"capture_generation",std::to_string(current->generation)},
                        {"receiver_context",current->context},{"expired_samples",retained.status().expired_samples},
                        {"capture_counters",{{"scope","sampled-during-window-copy"},{"input_accepted_samples",current->accepted.load()},{"input_rejected_samples",current->rejected.load()},
                            {"processing_rejected_samples",current->processing_rejected.load()},{"canceled_samples",current->canceled.load()},
                            {"known_missing_samples",retained.status().known_missing_samples},{"unquantified_discontinuities",retained.status().unquantified_discontinuities}}}});
                    window->validate();result->window=std::move(window);
                }catch(const std::exception& error){result->error=error.what();}
                std::lock_guard lock(mutex);
                result->status=describe(current,std::atomic_load(&active)==current,queued_bytes,queue.size());
                if(std::atomic_load(&active)==current && snapshot->id==requested)std::atomic_store(&latest,std::shared_ptr<const SignalCaptureSnapshot>(std::move(result)));
            }
        }
    }
    SignalCaptureBudget budget;
    mutable std::mutex mutex;std::condition_variable condition;std::deque<Packet> queue;std::size_t queued_bytes=0;
    std::uint64_t generation=0,serial=0,requested=0;
    bool closed=false;
    std::optional<Request> pending;
    std::shared_ptr<Epoch> active,last;
    std::shared_ptr<const SignalCaptureSnapshot> latest;
    std::atomic<std::uint64_t> inactive=0;
    std::atomic<std::uint64_t> stale_samples=0,stale_blocks=0;
    std::jthread worker;
};
SignalCapture::SignalCapture(SignalCaptureBudget budget):state_(std::make_unique<State>(budget)){}
SignalCapture::~SignalCapture(){close();}
std::uint64_t SignalCapture::start(IqStreamParameters stream,std::string source,Json context) {
    SignalWindow check;check.stream=stream;check.validate();if(source.empty() || !context.is_object())throw std::invalid_argument("Live capture needs an identity and receiver context");
    auto epoch=std::make_shared<State::Epoch>();epoch->stream=stream;epoch->source=std::move(source);epoch->context=std::move(context);
    std::lock_guard lock(state_->mutex);if(state_->closed)throw std::logic_error("Signal capture is permanently closed");state_->discardQueue();epoch->generation=++state_->generation;epoch->base_serial=state_->serial;
    std::atomic_store(&state_->last,epoch);std::atomic_store(&state_->active,epoch);state_->condition.notify_one();return epoch->generation;
}
void SignalCapture::stop() noexcept {
    std::lock_guard lock(state_->mutex);std::atomic_store(&state_->active, {});state_->discardQueue();state_->condition.notify_one();
}
void SignalCapture::close() noexcept {state_->close();}
bool SignalCapture::offer(const IqBlockView& block,std::uint64_t expected_generation) noexcept {
    const auto epoch=std::atomic_load(&state_->active);if(!epoch){++state_->inactive;return false;}
    if(expected_generation && expected_generation!=epoch->generation) {
        ++state_->stale_blocks;try{state_->stale_samples+=block.bytes.size()/bytes_per_pair(block.stream.format);}catch(...){}return false;
    }
    std::size_t samples=0;
    try {
        const auto stride=bytes_per_pair(block.stream.format);samples=block.bytes.size()/stride;
        if(block.bytes.empty() || block.bytes.size()%stride || samples>UINT64_MAX-block.first_sample){++epoch->malformed;epoch->rejected+=samples;return false;}
        if(block.stream.sample_rate!=epoch->stream.sample_rate || block.stream.format!=epoch->stream.format || block.stream.center_frequency_hz!=epoch->stream.center_frequency_hz){++epoch->configuration;epoch->rejected+=samples;return false;}
        if(block.bytes.size()>state_->budget.maximum_block_bytes){++epoch->oversized;epoch->rejected+=samples;return false;}
        std::unique_lock lock(state_->mutex,std::try_to_lock);if(!lock.owns_lock()){++epoch->contention;epoch->rejected+=samples;return false;}
        if(std::atomic_load(&state_->active)!=epoch){++epoch->configuration;epoch->rejected+=samples;return false;}
        if(state_->queue.size()>=state_->budget.maximum_queued_blocks || block.bytes.size()>state_->budget.queue_payload_bytes-state_->queued_bytes){++epoch->full;epoch->rejected+=samples;return false;}
        Bytes owned(block.bytes.begin(),block.bytes.end());if(owned.capacity()>state_->budget.queue_payload_bytes-state_->queued_bytes){++epoch->full;epoch->rejected+=samples;return false;}
        const auto cost=owned.capacity();state_->queue.push_back({epoch,std::move(owned),block.first_sample,++state_->serial,samples,block.discontinuous});state_->queued_bytes+=cost;
        epoch->peak=std::max(epoch->peak.load(),state_->queued_bytes);epoch->accepted+=samples;state_->condition.notify_one();return true;
    }catch(...){++epoch->malformed;epoch->rejected+=samples;return false;}
}
std::uint64_t SignalCapture::requestSnapshot() {
    std::lock_guard lock(state_->mutex);const auto epoch=std::atomic_load(&state_->active);if(!epoch)throw std::logic_error("Cannot freeze an inactive signal capture");
    const auto id=++state_->requested;state_->pending=State::Request{epoch,id,state_->serial};std::atomic_store(&state_->latest, {});state_->condition.notify_one();return id;
}
std::shared_ptr<const SignalCaptureSnapshot> SignalCapture::takeSnapshot(){return std::atomic_exchange(&state_->latest,{});}
SignalCaptureStatus SignalCapture::status() const {
    std::lock_guard lock(state_->mutex);const auto epoch=std::atomic_load(&state_->active);return state_->describe(epoch?epoch:std::atomic_load(&state_->last),bool(epoch),state_->queued_bytes,state_->queue.size());
}
}
