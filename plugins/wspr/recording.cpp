#include "recording.hpp"
#include <thread>
namespace signalscope::wspr {
RecordingResult decode_recording(const std::filesystem::path &path, double target,
                                 const SearchOptions &options, std::optional<SignalSampleRange> selection,
                                 std::stop_token stop) {
    const auto source = inspect_signal_source(path);
    const auto range = selection.value_or(SignalSampleRange{0, source.storage.pairs});
    if (!range.count || range.first > source.storage.pairs ||
        range.count > source.storage.pairs - range.first)
        throw std::invalid_argument("Invalid WSPR recording range");
    if (source.stream.sample_rate < analysis_rate || source.stream.sample_rate > 20000000 ||
        std::abs(target - source.stream.center_frequency_hz) + analysis_rate / 2. >
            source.stream.sample_rate / 2.)
        throw std::invalid_argument("WSPR recording does not cover the requested channel");
    RecordingResult result;
    result.metadata = path;
    result.stored_range = range;
    std::unique_ptr<LiveInput> engine;
    std::optional<std::uint64_t> next;
    const auto collect = [&](const LiveSnapshot &snapshot) {
        if (!snapshot.error.empty())
            throw std::runtime_error("WSPR recording processing failed: " + snapshot.error);
        for (const auto &spot : snapshot.spots) {
            const auto epoch = result.result.discontinuities + 1;
            const auto duplicate =
                std::any_of(result.result.spots.begin(), result.result.spots.end(), [&](const auto &old) {
                    return old.epoch == epoch &&
                           old.observation.decoded.message.payload ==
                               spot.observation.decoded.message.payload &&
                           old.source_sample == spot.source_sample && old.frequency_hz == spot.frequency_hz;
                });
            if (!duplicate) {
                if (result.result.spots.size() >= 4096)
                    throw std::length_error("WSPR recording result budget exceeded");
                auto value = spot;
                value.epoch = epoch;
                result.result.spots.push_back(std::move(value));
            }
        }
    };
    const auto wait = [&](bool final) {
        LiveSnapshot snapshot;
        for (;;) {
            if (stop.stop_requested())
                throw std::runtime_error("WSPR recording canceled");
            snapshot = engine->snapshot();
            collect(snapshot);
            if (final ? snapshot.finished
                      : !snapshot.queued_bytes && !snapshot.input_in_flight_bytes &&
                            !snapshot.window_queue_bytes && !snapshot.window_in_flight_bytes)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return snapshot;
    };
    const auto finish = [&] {
        if (!engine)
            return;
        engine->finish();
        const auto snapshot = wait(true);
        result.result.offered_pairs += snapshot.offered_pairs;
        result.result.processed_pairs += snapshot.processed_pairs;
        result.result.input_rejected_pairs += snapshot.input_rejected_pairs;
        result.result.completed_windows += snapshot.completed_windows;
        result.result.rejected_windows += snapshot.rejected_windows;
        result.result.decoded_spots_total += snapshot.decoded_spots_total;
        engine.reset();
    };
    const auto chunk = std::min<std::uint64_t>(16384, std::uint64_t(source.stream.sample_rate) * 4);
    for (auto first = range.first; first < range.first + range.count;) {
        if (stop.stop_requested())
            throw std::runtime_error("WSPR recording canceled");
        const auto count = std::min(chunk, range.first + range.count - first);
        const auto window = read_signal_source(source, first, count, stop, 1024 * 1024);
        for (const auto &segment : window.segments) {
            if (engine && (segment.discontinuous_before || (next && segment.first_sample != *next))) {
                finish();
                ++result.result.discontinuities;
            }
            if (!engine)
                engine = std::make_unique<LiveInput>(target, options);
            Bytes bytes(segment.samples.size() * bytes_per_pair(SampleFormat::cf32_le));
            for (std::size_t n = 0; n < segment.samples.size(); ++n)
                write_iq_pair(bytes.data() + n * bytes_per_pair(SampleFormat::cf32_le), SampleFormat::cf32_le,
                              segment.samples[n]);
            engine->offer(
                {bytes,
                 segment.first_sample,
                 {source.stream.sample_rate, source.stream.center_frequency_hz, SampleFormat::cf32_le},
                 false});
            wait(false);
            next = segment.first_sample + segment.samples.size();
        }
        first += count;
    }
    finish();
    result.result.finished = true;
    return result;
}
} // namespace signalscope::wspr
