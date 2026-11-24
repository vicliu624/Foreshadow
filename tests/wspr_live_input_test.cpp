#include "native_test_main.hpp"
#include "plugins/wspr/live_input.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>
using namespace signalscope;
namespace {
wspr::LiveSnapshot drain(wspr::LiveInput &input, std::uint64_t pairs, bool decoded) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    wspr::LiveSnapshot result;
    do {
        result = input.snapshot();
        if (result.processed_pairs + result.input_rejected_pairs >= pairs &&
            (!decoded || result.completed_windows || !result.error.empty()))
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("WSPR live input did not drain");
}
} // namespace
static int run(int argc, char **argv) {
    if (argc != 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(path_from_utf8(argv[1]));
    Json references;
    file >> references;
    const auto tones = references.at("cases")[0].at("symbols").get<std::string>();
    std::vector<std::complex<float>> samples(45000);
    double phase = .37;
    for (unsigned n = 0; n < wspr::frame_samples; ++n) {
        samples[750 + n] = std::polar(.3f, float(phase));
        phase += 2 * std::numbers::pi *
                 (17.3 + (tones[n / 256] - '0') * 375. / 256 + 2. * n / wspr::frame_samples) / 375;
    }
    constexpr double center = 14097100;
    constexpr std::uint64_t origin = 1ULL << 40;
    for (const auto &codec : sample_codecs) {
        wspr::SearchOptions options;
        options.minimum_hz = 10;
        options.maximum_hz = 25;
        wspr::LiveInput input(center, options);
        const IqStreamParameters stream{375, center, codec.format};
        for (unsigned n = 0; n < 1000; ++n)
            input.offer({{}, origin, stream, false});
        if (input.snapshot().queued_memory_bytes || input.snapshot().offered_pairs)
            throw std::runtime_error("Empty WSPR callbacks consumed queue capacity");
        Bytes bytes(samples.size() * codec.pair_bytes);
        for (std::size_t n = 0; n < samples.size(); ++n)
            write_iq_pair(bytes.data() + n * codec.pair_bytes, codec.format, samples[n]);
        for (std::size_t first = 0; first < samples.size(); first += 1373) {
            const auto count = std::min<std::size_t>(1373, samples.size() - first);
            input.offer({std::span(bytes).subspan(first * codec.pair_bytes, count * codec.pair_bytes),
                         origin + first, stream, false});
        }
        auto result = drain(input, samples.size(), true);
        if (result.completed_windows != 1 || result.input_rejected_pairs || result.discontinuities ||
            result.spots.empty() || result.spots[0].observation.decoded.message.callsign != "K1ABC" ||
            result.spots[0].source_sample < origin + 600 || result.spots[0].source_sample > origin + 900)
            throw std::runtime_error("WSPR live reference or source clock mismatch: " + result.error);
        input.offer(
            {std::span(bytes).first(1000 * codec.pair_bytes), origin + samples.size() + 1, stream, false});
        result = drain(input, samples.size() + 1000, false);
        if (result.discontinuities != 1 || result.spots.size() != 1 || result.decoded_spots_total != 1 ||
            result.retained_samples != 1000)
            throw std::runtime_error(
                "WSPR live input lost a validated pre-gap message or bridged incomplete history");
        input.offer({{}, origin + samples.size() + 1001, stream, true});
        input.offer(
            {std::span(bytes).first(1000 * codec.pair_bytes), origin + samples.size() + 1001, stream, false});
        result = drain(input, samples.size() + 2000, false);
        if (result.discontinuities != 2 || result.retained_samples != 1000 || result.spots.size() != 1)
            throw std::runtime_error("WSPR ignored an explicit empty-block continuity marker");
        input.stop();
        const auto rejected = input.snapshot().input_rejected_pairs;
        input.offer({std::span(bytes).first(codec.pair_bytes), 0, stream, false});
        if (input.snapshot().input_rejected_pairs != rejected + 1)
            throw std::runtime_error("Stopped WSPR input accepted a late callback");
    }
    {
        wspr::LiveInput input(center);
        const auto pairs = std::size_t((wspr::LiveInput::input_budget_seconds + 1) * 375);
        Bytes bytes(pairs * bytes_per_pair(SampleFormat::cf32_le));
        input.offer({bytes, 0, {375, center, SampleFormat::cf32_le}, false});
        const auto state = input.snapshot();
        if (state.input_rejected_pairs != pairs || state.queued_bytes || state.queued_memory_bytes)
            throw std::runtime_error("WSPR queue ignored its actual sample-time budget");
    }
    {
        wspr::SearchOptions options;
        options.minimum_hz = 10;
        options.maximum_hz = 25;
        wspr::LiveInput input(center, options);
        const auto stride = bytes_per_pair(SampleFormat::cf32_le);
        Bytes bytes(samples.size() * stride);
        for (std::size_t n = 0; n < samples.size(); ++n)
            write_iq_pair(bytes.data() + n * stride, SampleFormat::cf32_le, samples[n]);
        const IqStreamParameters stream{375, center, SampleFormat::cf32_le};
        input.offer({bytes, 0, stream, false});
        input.offer({bytes, 0, stream, true});
        input.finish();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        wspr::LiveSnapshot result;
        do {
            result = input.snapshot();
            if (result.finished)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        if (!result.finished || result.spots.size() != 2 || result.decoded_spots_total != 2 ||
            result.rejected_windows || result.spots[0].epoch == result.spots[1].epoch)
            throw std::runtime_error("WSPR input lost a complete queued window across a clock reset");
    }
    std::cout << "WSPR live worker, all codecs, large source origin and gap reset passed\n";

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
