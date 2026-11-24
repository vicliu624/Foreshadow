#include "native_test_main.hpp"
#include "plugins/wspr/core/search.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>
static int run(int argc, char **argv) {
    using namespace signalscope::wspr;
    if (argc != 2)
        throw std::runtime_error("Expected upstream reference path");
    std::ifstream file(signalscope::path_from_utf8(argv[1]));
    nlohmann::json fixture;
    file >> fixture;
    const auto tones = fixture.at("cases")[0].at("symbols").get<std::string>();
    std::vector<std::complex<float>> input(analysis_rate * 120);
    constexpr unsigned offset = 750;
    constexpr double base = 17.3, drift = 2.;
    double phase = .37;
    for (unsigned n = 0; n < frame_samples; ++n) {
        const auto tone = tones[n / samples_per_symbol] - '0';
        const double frequency =
            base + tone * double(analysis_rate) / samples_per_symbol + drift * double(n) / frame_samples;
        input[offset + n] = std::polar(.3f, float(phase));
        phase += 2 * std::numbers::pi * frequency / analysis_rate;
    }
    SearchOptions options;
    options.minimum_hz = 10;
    options.maximum_hz = 25;
    const auto results = search(input, options);
    bool found = false;
    for (const auto &result : results) {
        std::cout << result.decoded.message.callsign << ' ' << result.first << ' ' << result.base_hz << ' '
                  << result.drift_hz << '\n';
        if (result.decoded.message.callsign == "K1ABC" && result.decoded.message.locator == "FN42" &&
            std::abs(double(result.first) - offset) <= 128 && std::abs(result.base_hz - base) < 1)
            found = true;
    }
    if (!found)
        throw std::runtime_error("WSPR I/Q search failed independent waveform");
    std::fill(input.begin(), input.end(), std::complex<float>{});
    if (!search(input, options).empty())
        throw std::runtime_error("Silence decoded as WSPR");
    std::cout << "WSPR I/Q timing, frequency, drift and silence search passed\n";

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
