#include "native_test_main.hpp"
#include "plugins/wspr/live_input.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
#include <thread>
static int run(int argc, char **argv) {
    using namespace signalscope;
    if (argc != 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(path_from_utf8(argv[1]));
    Json references;
    file >> references;
    const auto tones = references.at("cases")[0].at("symbols").get<std::string>();
    constexpr double target = 14097100;
    const auto stride = bytes_per_pair(SampleFormat::cf32_le);
    wspr::SearchOptions options;
    options.minimum_hz = 10;
    options.maximum_hz = 25;
    for (const unsigned rate : {375u, 12000u}) {
        const double center = rate == 375 ? target : target - 70.3;
        wspr::LiveInput input(target, options);
        const auto count = rate == 375 ? 43000u : rate * 120;
        const auto start = rate * 2;
        const auto duration = wspr::frame_samples * rate / 375;
        double phase = .37;
        for (unsigned first = 0; first < count; first += 4096) {
            const auto size = std::min(4096u, count - first);
            Bytes bytes(size * stride);
            for (unsigned n = 0; n < size; ++n) {
                const auto at = first + n;
                std::complex<float> sample{};
                if (at >= start && at < start + duration) {
                    const auto relative = at - start;
                    const auto symbol = std::uint64_t(relative) * 375 / rate / 256;
                    sample = std::polar(.3f, float(phase));
                    phase += 2 * std::numbers::pi *
                             (target - center + 17.3 + (tones[symbol] - '0') * 375. / 256 +
                              2. * relative / duration) /
                             rate;
                }
                write_iq_pair(bytes.data() + n * stride, SampleFormat::cf32_le, sample);
            }
            input.offer({bytes, first, {rate, center, SampleFormat::cf32_le}, false});
        }
        input.finish();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        wspr::LiveSnapshot result;
        do {
            result = input.snapshot();
            if (result.finished)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        if (!result.finished || !result.error.empty() || result.input_rejected_pairs ||
            result.spots.empty() || result.spots[0].observation.decoded.message.callsign != "K1ABC" ||
            std::abs(result.spots[0].frequency_hz - (target + 17.3)) > 1 ||
            std::abs(double(result.spots[0].source_sample) - start) > double(rate) / 2)
            throw std::runtime_error("WSPR finite input/converted path mismatch: " + result.error);
        std::cout << "WSPR finite input decoded at " << rate
                  << " S/s, source=" << result.spots[0].source_sample << '\n';
    }

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
