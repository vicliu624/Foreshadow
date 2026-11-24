#include "live_input.hpp"
#include "core/window.hpp"
#include "downconverter.hpp"
#include <cmath>
#include <thread>
namespace signalscope::wspr {
struct LiveInput::State {
    struct Block {
        Bytes bytes;
        std::uint64_t first;
        IqStreamParameters stream;
        bool gap;
    };
    struct Job {
        std::vector<std::complex<float>> samples;
        std::uint64_t first, epoch, origin;
        unsigned rate;
        double delay, center;
    };
    double target;
    SearchOptions options;
    mutable std::mutex mutex;
    std::mutex shutdown_mutex;
    std::condition_variable wake;
    std::deque<Block> input;
    std::deque<Job> jobs;
    LiveSnapshot status;
    bool closed = false, accepting = true, finish_requested = false, input_finished = false;
    bool pending_gap = false;
    std::uint64_t input_epoch = 0;
    WindowBuffer history;
    std::atomic<std::shared_ptr<const std::vector<Spot>>> published;
    std::jthread input_worker, decoder_worker;
    State(double hz, SearchOptions settings) : target(hz), options(settings) {
        try {
            input_worker = std::jthread([this] { convert(); });
            decoder_worker = std::jthread([this](std::stop_token stop) { decode(stop); });
        } catch (...) {
            close();
            throw;
        }
    }
    ~State() {
        close();
    }
    void close() {
        std::lock_guard shutdown(shutdown_mutex);
        {
            std::lock_guard lock(mutex);
            closed = true;
            accepting = false;
        }
        decoder_worker.request_stop();
        wake.notify_all();
        if (input_worker.joinable())
            input_worker.join();
        if (decoder_worker.joinable())
            decoder_worker.join();
    }
    void convert() {
        std::unique_ptr<Downconverter> converter;
        auto &windows = history;
        std::optional<IqStreamParameters> stream;
        std::uint64_t next = 0, origin = 0;
        const auto submit = [&](WindowView view) {
            Job job{{view.samples.begin(), view.samples.end()},
                    view.first,
                    input_epoch,
                    origin,
                    stream->sample_rate,
                    converter->delay_source_samples(),
                    converter->center_hz()};
            const auto bytes = job.samples.size() * sizeof(std::complex<float>);
            std::lock_guard lock(mutex);
            if (bytes > window_budget_bytes - status.window_queue_bytes) {
                ++status.rejected_windows;
                return;
            }
            status.window_queue_bytes += bytes;
            jobs.push_back(std::move(job));
            wake.notify_all();
        };
        const auto collect = [&](std::span<const std::complex<float>> samples, std::uint64_t first) {
            windows.append(samples, first, false, submit);
        };
        for (;;) {
            Block block;
            bool ending = false;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return closed || !input.empty() || finish_requested; });
                if (closed)
                    return;
                if (input.empty()) {
                    ending = true;
                    finish_requested = false;
                } else {
                    block = std::move(input.front());
                    input.pop_front();
                    status.queued_bytes -= block.bytes.size();
                    status.queued_memory_bytes -= block.bytes.capacity() + sizeof(Block);
                    const auto seconds = double(block.bytes.size() / bytes_per_pair(block.stream.format)) /
                                         block.stream.sample_rate;
                    status.queued_input_seconds =
                        input.empty() ? 0 : std::max(0., status.queued_input_seconds - seconds);
                    status.input_in_flight_bytes = block.bytes.size();
                    status.input_in_flight_seconds = seconds;
                }
            }
            if (ending) {
                try {
                    if (converter) {
                        converter->finish(collect);
                        windows.finish(submit);
                    }
                } catch (const std::exception &error) {
                    std::lock_guard lock(mutex);
                    status.error = error.what();
                }
                std::lock_guard lock(mutex);
                input_finished = true;
                status.retained_samples = windows.retained_samples();
                continue;
            }
            try {
                const bool changed = !stream || stream->sample_rate != block.stream.sample_rate ||
                                     stream->format != block.stream.format ||
                                     stream->center_frequency_hz != block.stream.center_frequency_hz;
                const bool gap = block.gap || (stream && (changed || block.first != next));
                if (changed || gap) {
                    converter.reset();
                    windows.reset();
                    origin = block.first;
                    {
                        std::lock_guard lock(mutex);
                        ++input_epoch;
                        if (gap)
                            ++status.discontinuities;
                    }
                    stream = block.stream;
                    converter = std::make_unique<Downconverter>(*stream, target);
                }
                converter->feed(block.bytes, collect);
                next = block.first + block.bytes.size() / bytes_per_pair(block.stream.format);
                {
                    std::lock_guard lock(mutex);
                    status.processed_pairs += block.bytes.size() / bytes_per_pair(block.stream.format);
                    status.input_in_flight_bytes = 0;
                    status.input_in_flight_seconds = 0;
                    status.retained_samples = windows.retained_samples();
                }
            } catch (const std::exception &error) {
                {
                    std::lock_guard lock(mutex);
                    status.error = error.what();
                    status.input_rejected_pairs += block.bytes.size() / bytes_per_pair(block.stream.format);
                    status.input_in_flight_bytes = 0;
                    status.input_in_flight_seconds = 0;
                    status.retained_samples = 0;
                    ++input_epoch;
                }
                stream.reset();
                converter.reset();
                windows.reset();
            }
        }
    }
    void decode(std::stop_token stop) {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return closed || !jobs.empty(); });
                if (closed)
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
                status.window_queue_bytes -= job.samples.size() * sizeof(std::complex<float>);
                status.window_in_flight_bytes = job.samples.size() * sizeof(std::complex<float>);
            }
            try {
                const auto history = published.load();
                std::vector<Spot> spots = history ? *history : std::vector<Spot>{};
                unsigned added = 0, expired = 0;
                for (auto observation : search(job.samples, options, stop)) {
                    observation.first += job.first;
                    const auto relative =
                        std::max(0., double(observation.first) * job.rate / analysis_rate - job.delay);
                    const auto source = job.origin + std::uint64_t(std::llround(relative));
                    const auto hz = job.center + observation.base_hz;
                    const auto duplicate = std::any_of(spots.begin(), spots.end(), [&](const auto &old) {
                        return old.epoch == job.epoch &&
                               old.observation.decoded.message.payload ==
                                   observation.decoded.message.payload &&
                               std::abs(old.frequency_hz - hz) < 3 &&
                               std::abs(double(old.source_sample) - double(source)) < job.rate;
                    });
                    if (!duplicate) {
                        ++added;
                        if (spots.size() == 256) {
                            spots.erase(spots.begin());
                            ++expired;
                        }
                        spots.push_back({std::move(observation), hz, source, job.rate, job.epoch});
                    }
                }
                auto result = std::make_shared<const std::vector<Spot>>(spots);
                std::lock_guard lock(mutex);
                status.window_in_flight_bytes = 0;
                if (!closed) {
                    published.store(std::move(result));
                    ++status.completed_windows;
                    status.decoded_spots_total += added;
                    status.expired_spots += expired;
                    if (job.epoch == input_epoch)
                        status.error.clear();
                } else
                    ++status.rejected_windows;
            } catch (const std::exception &error) {
                std::lock_guard lock(mutex);
                status.window_in_flight_bytes = 0;
                ++status.rejected_windows;
                if (!closed)
                    status.error = error.what();
            }
        }
    }
};
LiveInput::LiveInput(double target, SearchOptions options) {
    if (!std::isfinite(target) || target <= 0)
        throw std::invalid_argument("Invalid WSPR RF center");
    state_ = std::make_unique<State>(target, options);
}
LiveInput::~LiveInput() = default;
void LiveInput::stop() {
    state_->close();
}
void LiveInput::offer(const IqBlockView &block) {
    if (block.bytes.empty()) {
        std::lock_guard lock(state_->mutex);
        if (!state_->closed && state_->accepting)
            state_->pending_gap |= block.discontinuous;
        return;
    }
    const auto stride = bytes_per_pair(block.stream.format), pairs = block.bytes.size() / stride;
    std::lock_guard lock(state_->mutex);
    auto &status = state_->status;
    status.offered_pairs += pairs;
    if (state_->closed || !state_->accepting || pairs > UINT64_MAX - block.first_sample ||
        block.bytes.size() % stride || block.bytes.size() > maximum_block_bytes ||
        block.stream.sample_rate < analysis_rate || block.stream.sample_rate > 20000000 ||
        !std::isfinite(block.stream.center_frequency_hz) ||
        std::abs(state_->target - block.stream.center_frequency_hz) + analysis_rate / 2. >
            block.stream.sample_rate / 2.) {
        status.input_rejected_pairs += pairs;
        state_->pending_gap = true;
        return;
    }
    const auto seconds = double(pairs) / block.stream.sample_rate;
    if (block.bytes.size() + sizeof(State::Block) > input_budget_bytes - status.queued_memory_bytes ||
        seconds > input_budget_seconds - status.queued_input_seconds) {
        status.input_rejected_pairs += pairs;
        state_->pending_gap = true;
        return;
    }
    State::Block owned{Bytes(block.bytes.begin(), block.bytes.end()), block.first_sample, block.stream,
                       block.discontinuous || state_->pending_gap};
    const auto charge = owned.bytes.capacity() + sizeof(State::Block);
    if (charge > input_budget_bytes - status.queued_memory_bytes) {
        status.input_rejected_pairs += pairs;
        state_->pending_gap = true;
        return;
    }
    state_->input.push_back(std::move(owned));
    state_->pending_gap = false;
    status.queued_bytes += block.bytes.size();
    status.queued_memory_bytes += charge;
    status.queued_input_seconds += seconds;
    state_->wake.notify_all();
}
void LiveInput::finish() {
    std::lock_guard lock(state_->mutex);
    if (!state_->accepting)
        return;
    state_->accepting = false;
    state_->finish_requested = true;
    state_->wake.notify_all();
}
LiveSnapshot LiveInput::snapshot() const {
    LiveSnapshot value;
    {
        std::lock_guard lock(state_->mutex);
        value = state_->status;
        value.finished = state_->input_finished && !value.queued_bytes && !value.input_in_flight_bytes &&
                         !value.window_queue_bytes && !value.window_in_flight_bytes;
    }
    if (const auto spots = state_->published.load())
        value.spots = *spots;
    return value;
}
} // namespace signalscope::wspr
