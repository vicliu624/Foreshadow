#include "native_test_main.hpp"
#include "plugins/wspr/live_input.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>
static int run(int argc, char **argv) {
    using namespace signalscope;
    if (argc != 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(path_from_utf8(argv[1]));
    Json reference;
    file >> reference;
    const auto tones = reference.at("cases")[0].at("symbols").get<std::string>();
    constexpr unsigned rate = 12000, seconds = 140, block_size = rate / 4, start_sample = rate * 9;
    constexpr auto duration = std::uint64_t(wspr::frame_samples) * rate / wspr::analysis_rate;
    constexpr double center = 14097100;
    const auto format = SampleFormat::cf32_le;
    const auto stride = bytes_per_pair(format);
    wspr::LiveInput input(center);
    double phase = .37;
    std::size_t peak_charge = 0;
    double maximum_snapshot_us = 0;
    const auto began = std::chrono::steady_clock::now();
    for (unsigned first = 0; first < rate * seconds; first += block_size) {
        const auto count = std::min(block_size, rate * seconds - first);
        Bytes bytes(count * stride);
        for (unsigned n = 0; n < count; ++n) {
            const auto at = first + n;
            std::complex<float> sample{};
            if (at >= start_sample && at < start_sample + duration) {
                const auto relative = at - start_sample;
                const auto symbol =
                    std::uint64_t(relative) * wspr::analysis_rate / rate / wspr::samples_per_symbol;
                sample = std::polar(.2f, float(phase));
                phase += 2 * std::numbers::pi *
                         (17.3 + (tones.at(symbol) - '0') * 375. / 256 + 2. * relative / duration) / rate;
            }
            write_iq_pair(bytes.data() + n * stride, format, sample);
        }
        input.offer({bytes, first, {rate, center, format}, false});
        const auto before = std::chrono::steady_clock::now();
        const auto snapshot = input.snapshot();
        maximum_snapshot_us = std::max(
            maximum_snapshot_us,
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before).count());
        peak_charge = std::max(peak_charge, snapshot.queued_memory_bytes);
        if ((first + count) % (rate * 20) == 0)
            std::cout << "input_seconds=" << (first + count) / rate
                      << " analyzed_windows=" << snapshot.completed_windows
                      << " rejected_pairs=" << snapshot.input_rejected_pairs << '\n'
                      << std::flush;
        std::this_thread::sleep_until(began + std::chrono::duration<double>(double(first + count) / rate));
    }
    input.finish();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    wspr::LiveSnapshot result;
    do {
        result = input.snapshot();
        if (result.finished)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    std::cout << "processed_pairs=" << result.processed_pairs
              << " rejected_pairs=" << result.input_rejected_pairs
              << " rejected_windows=" << result.rejected_windows
              << " analyzed_windows=" << result.completed_windows
              << " decoded_spots=" << result.decoded_spots_total << " retained_spots=" << result.spots.size()
              << " observed_peak_queue_charge=" << peak_charge
              << " maximum_snapshot_us=" << maximum_snapshot_us << " elapsed_seconds="
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count() << '\n';
    return result.finished && result.error.empty() &&
                   result.processed_pairs == std::uint64_t(rate) * seconds && !result.input_rejected_pairs &&
                   !result.rejected_windows && !result.discontinuities && result.completed_windows >= 3 &&
                   result.decoded_spots_total == 1 && result.spots.size() == 1
               ? 0
               : 1;
}
FORESHADOW_NATIVE_TEST_MAIN(run)
