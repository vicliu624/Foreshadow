#include "native_test_main.hpp"
#include "plugins/wspr/recording.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
static int run(int argc, char **argv) {
    using namespace signalscope;
    if (argc != 2)
        throw std::runtime_error("Expected reference path");
    std::ifstream file(path_from_utf8(argv[1]));
    Json reference;
    file >> reference;
    const auto tones = reference.at("cases")[0].at("symbols").get<std::string>();
    SignalWindow source;
    source.stream = {375, 14097100, SampleFormat::cf32_le};
    source.source_id = "independent-wspr-recording";
    source.recording_origin = 1ULL << 40;
    std::vector<std::complex<float>> samples(43000);
    double phase = .37;
    for (unsigned n = 0; n < wspr::frame_samples; ++n) {
        samples[n + 750] = std::polar(.3f, float(phase));
        phase += 2 * std::numbers::pi *
                 (17.3 + (tones[n / 256] - '0') * 375. / 256 + 2. * n / wspr::frame_samples) / 375;
    }
    source.segments = {{source.recording_origin, samples, false},
                       {source.recording_origin + samples.size() + 123, samples, true}};
    const auto directory =
        std::filesystem::temp_directory_path() /
        path_from_utf8("foreshadow-wspr-recording-录音-📡-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            if (path.parent_path() == std::filesystem::temp_directory_path() &&
                path_to_utf8(path.filename()).starts_with("foreshadow-wspr-recording-")) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{directory};
    const auto metadata = write_signal_archive(directory, source);
    wspr::SearchOptions options;
    options.minimum_hz = 10;
    options.maximum_hz = 25;
    const auto result = wspr::decode_recording(metadata, source.stream.center_frequency_hz, options);
    if (result.metadata != metadata || !result.result.finished || result.result.spots.size() != 2 ||
        result.result.discontinuities != 1 || result.result.input_rejected_pairs ||
        result.result.rejected_windows)
        throw std::runtime_error("WSPR recording lost a decoded segment or observation");
    for (unsigned n = 0; n < 2; ++n) {
        const auto &spot = result.result.spots[n];
        if (spot.epoch != n + 1 || spot.observation.decoded.message.callsign != "K1ABC" ||
            spot.source_sample < source.segments[n].first_sample + 600 ||
            spot.source_sample > source.segments[n].first_sample + 900)
            throw std::runtime_error("WSPR recording source mapping mismatch");
    }
    std::stop_source cancel;
    cancel.request_stop();
    bool canceled = false;
    try {
        wspr::decode_recording(metadata, source.stream.center_frequency_hz, options, {}, cancel.get_token());
    } catch (const std::runtime_error &) {
        canceled = true;
    }
    if (!canceled)
        throw std::runtime_error("Canceled WSPR recording decoded");
    std::cout << "WSPR recording, Unicode path, gap preservation, source clocks and cancellation passed\n";

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
