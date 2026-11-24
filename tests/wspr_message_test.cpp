#include "native_test_main.hpp"
#include "plugins/wspr/core/channel.hpp"
#include "plugins/wspr/core/fec.hpp"
#include "plugins/wspr/core/interleave.hpp"
#include "plugins/wspr/core/message.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
static int run(int argc, char **argv) {
    using namespace signalscope::wspr;
    if (argc != 2)
        throw std::runtime_error("Expected independent reference path");
    std::ifstream stream(signalscope::path_from_utf8(argv[1]));
    nlohmann::json references;
    stream >> references;
    unsigned checked = 0;
    for (const auto &fixture : references.at("cases")) {
        const auto hex = fixture.at("bytes_hex").get<std::string>(),
                   tones = fixture.at("symbols").get<std::string>();
        std::array<std::uint8_t, 11> expected{};
        for (unsigned n = 0; n < 11; ++n)
            expected[n] = std::uint8_t(std::stoul(hex.substr(n * 2, 2), nullptr, 16));
        if (tones.size() != 162)
            throw std::runtime_error("Invalid reference symbol count");
        std::array<std::uint8_t, 162> soft{};
        for (unsigned n = 0; n < 162; ++n)
            soft[n] = tones[n] >= '2' ? 255 : 0;
        const auto fec = decode_fec(deinterleave(soft), 10000);
        if (fec.state != FecState::decoded || fec.bytes != expected)
            throw std::runtime_error("Reference interleave/FEC mismatch");
        const auto fields = unpack_message(fec.bytes);
        if (!fields)
            throw std::runtime_error("Reference message rejected");
        TonePowers powers{};
        for (unsigned n = 0; n < 162; ++n)
            powers[n][tones[n] - '0'] = 1;
        const auto channel = decode_channel(powers);
        if (!channel || channel->message.payload != fields->payload || channel->hard_symbol_mismatches ||
            channel->synchronization != 1)
            throw std::runtime_error("Reference four-tone chain mismatch");
        for (auto &symbol : powers)
            symbol = {1, 1, 1, 1};
        if (decode_channel(powers))
            throw std::runtime_error("Unsynchronized input decoded");
        std::string reconstructed;
        if (fields->kind == MessageKind::hashed) {
            if (!fields->callsign.empty() || !fields->callsign_hash)
                throw std::runtime_error("Unknown hash became identity");
            reconstructed = "<K1ABC> " + fields->locator;
        } else
            reconstructed = fields->callsign + (fields->locator.empty() ? "" : " " + fields->locator);
        reconstructed += ' ' + std::to_string(fields->power_dbm);
        if (reconstructed != fixture.at("message").get<std::string>())
            throw std::runtime_error("Reference field mismatch: " + reconstructed);
        auto damaged = fec.bytes;
        damaged[10] = 1;
        if (unpack_message(damaged))
            throw std::runtime_error("Nonzero tail accepted");
        ++checked;
    }
    std::cout << "WSPR independent upstream interleave, FEC and fields passed: " << checked << '\n';

    return 0;
}

FORESHADOW_NATIVE_TEST_MAIN(run)
