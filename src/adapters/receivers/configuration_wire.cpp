#include "signalscope/adapters/receivers/plugin_support.hpp"
namespace signalscope::receiver_plugin {
Json encodeConfig(const Config& c) {
    auto j=requestedReceiverConfiguration(c);
    j["gain_tenths_db"]=c.gain_tenths;
    j.update({{"block_bytes",c.block_bytes},{"queue_blocks",c.queue_blocks},{"seconds",c.seconds},
        {"settle_seconds",c.settle_seconds},{"analysis_queue_bytes",c.analysis_queue_bytes},
        {"analysis_queue_seconds",c.analysis_queue_seconds}});
    return j;
}
Config decodeConfig(const Json& j) {
    Config c;
    c.rate=j.value("sample_rate_hz",c.rate);c.frequency=j.value("frequency_hz",c.frequency);
    c.device=j.value("device",c.device);c.device_serial=j.value("serial",std::string());
    c.antenna=j.value("antenna",std::string());c.auto_gain=j.value("auto_gain",c.auto_gain);
    if(j.contains("gain_tenths_db") && !j.at("gain_tenths_db").is_null())c.gain_tenths=j.at("gain_tenths_db");
    c.input_mode=j.value("input_mode",c.input_mode);c.if_bandwidth_hz=j.value("if_bandwidth_hz",0.);
    c.receiver_settings=j.value("settings",Json::object());
    c.required_passband_hz=j.value("required_passband_hz",0.);
    c.block_bytes=j.value("block_bytes",c.block_bytes);c.queue_blocks=j.value("queue_blocks",c.queue_blocks);
    c.seconds=j.value("seconds",c.seconds);c.settle_seconds=j.value("settle_seconds",c.settle_seconds);
    c.analysis_queue_bytes=j.value("analysis_queue_bytes",c.analysis_queue_bytes);
    c.analysis_queue_seconds=j.value("analysis_queue_seconds",c.analysis_queue_seconds);
    return c;
}
}
