#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/acquisition_conditions.hpp"
#include "signalscope/adapters/receivers/configuration.hpp"
#include <string_view>

namespace signalscope {
// The sink receives borrowed, interleaved I/Q in the backend's declared format.
// Calls are synchronous on the receiver stream's calling thread.
// A backend emits contiguous blocks of at most Config::block_bytes and stops
// on cancellation or a discontinuity. Sample indices start at zero.
struct ReceiverReadback {
    double sample_rate_hz;
    double center_frequency_hz;
    double filter_bandwidth_hz=0;
    Json configuration=Json::object();
    AcquisitionConditions conditions{};
};
inline constexpr unsigned receiverReadbackProtocolVersion=2;
// Called once before the first I/Q block when configuration can be read back.
using ReceiverReady=std::function<void(const ReceiverReadback&)>;
using ReceiverStream = std::function<Json(const Config&, const BlockSink&, const std::function<bool()>&, const ReceiverReady&)>;

enum class ReceiverGainKind { tuner, if_reduction };
inline constexpr std::uint32_t default_monitor_rates[]{1'750'000,1'875'000,2'000'000};

inline constexpr auto receiver_backend_not_built="backend_not_built";
inline constexpr auto receiver_driver_missing="driver_missing";

struct ReceiverBackend {
    const char* source;
    const char* recording_source;
    std::function<Json()> devices;
    std::function<Json(std::uint32_t index,std::string_view serial)> probe;
    ReceiverStream stream;
    SampleFormat format=SampleFormat::cu8;
    const char* default_gain="19.7";
    const char* id=nullptr;
    const char* display_name=nullptr;
    std::span<const int> gain_presets_tenths{};
    ReceiverGainKind gain_kind=ReceiverGainKind::tuner;
    std::uint32_t tuning_min_hz=100'000;
    std::uint32_t tuning_max_hz=1'740'000'000;
    std::span<const std::uint32_t> monitor_rates=default_monitor_rates;
    // Applies configuration and reads it back. A backend may briefly activate
    // reception to verify deferred hardware setup; no I/Q is delivered or saved.
    std::function<Json(const Config&)> configure;
    std::function<ReceiverPlan(const ReceiverCapabilities&,const Config&)> plan;
    std::function<std::vector<Config>(const ReceiverCapabilities&,const Config&)> recommend;
    std::function<Json(const ReceiverCapabilities&,const Config&)> parameters;
    // Request protocol 2: provider-owned connection fields are available before
    // enumeration. Legacy providers keep the original callbacks and defaults.
    Json connection_parameters=Json::array();
    std::function<Json(const Json&)> connected_devices;
    std::function<Json(std::uint32_t,std::string_view,const Json&)> connected_probe;
};

inline Json receiverDevices(const ReceiverBackend& b,const Json& settings=Json::object()) {
    return b.connected_devices?b.connected_devices(settings):b.devices();
}
inline Json receiverProbe(const ReceiverBackend& b,std::uint32_t index,std::string_view serial,
                          const Json& settings=Json::object()) {
    return b.connected_probe?b.connected_probe(index,serial,settings):b.probe(index,serial);
}

std::span<const ReceiverBackend* const> receiver_backends();
Json receiver_plugin_diagnostics();
Json receiverParameters(const ReceiverCapabilities&,const Config&);
std::vector<Config> receiverRecommendations(const ReceiverCapabilities&,const Config&);
const ReceiverBackend& default_receiver();
const ReceiverBackend* find_receiver(std::string_view name);
const ReceiverBackend& receiver_by_name(std::string_view name);
Json capture_receiver(const ReceiverBackend&, const Config&, Recorder&, const std::function<bool()>&);
}
