#include "native_test_main.hpp"
#include "plugins/wspr/core/search.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <random>
static int run(int argc, char **argv) {
    using namespace signalscope::wspr;
    if (argc != 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(signalscope::path_from_utf8(argv[1]));
    nlohmann::json references;
    file >> references;
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        const double sigma = scenario == 0 ? .0267 : .0667;
        std::mt19937 generator(1090);
        std::normal_distribution<float> noise(0, float(sigma));
        std::vector<std::complex<float>> input(375 * 120);
        for (auto &sample : input)
            sample = {noise(generator), noise(generator)};
        for (unsigned station = 0; station < (scenario == 0 ? 2u : 1u); ++station) {
            const auto tones = references.at("cases")[station].at("symbols").get<std::string>();
            const double base = station ? -42.7 : 17.3, drift = station ? -1 : 2;
            const unsigned start = station ? 1100 : 750;
            double phase = .37;
            for (unsigned n = 0; n < frame_samples; ++n) {
                input[start + n] += std::polar(.01f, float(phase));
                phase += 2 * std::numbers::pi *
                         (base + (tones[n / 256] - '0') * 375. / 256 + drift * n / frame_samples) / 375;
            }
        }
        SearchOptions options;
        options.minimum_hz = -60;
        options.maximum_hz = 30;
        const auto results = search(input, options);
        bool standard = false, compound = false;
        for (const auto &result : results) {
            standard |= result.decoded.message.callsign == "K1ABC";
            compound |= result.decoded.message.callsign == "PJ4/K1ABC";
        }
        std::cout << "WSPR expected input SNR/2500Hz="
                  << 10 * std::log10(.0001 / (2 * sigma * sigma) * 375 / 2500)
                  << " dB, decoded=" << results.size() << " standard=" << standard << " compound=" << compound
                  << '\n';
        if (!standard || (scenario == 0 && !compound))
            throw std::runtime_error("WSPR noisy or simultaneous reference failed");
    }
    std::mt19937 generator(37);
    std::normal_distribution<float> noise(0, .03f);
    std::vector<std::complex<float>> background(375 * 120);
    for (auto &sample : background)
        sample = {noise(generator), noise(generator)};
    SearchOptions options;
    options.minimum_hz = -60;
    options.maximum_hz = 30;
    if (!search(background, options).empty())
        throw std::runtime_error("Noise-only WSPR observation generated a message");

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
