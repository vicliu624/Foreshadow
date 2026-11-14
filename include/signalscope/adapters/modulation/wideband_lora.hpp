#pragma once
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/backend/dsp/channelizer.hpp"
namespace signalscope {
struct LoRaAnalysisPlan {
    bool direct=false;
    std::uint32_t analysis_rate=0;
    std::vector<AnalysisChannel> channels;
};
LoRaAnalysisPlan planWidebandLoRa(const LoRaConfig&);
// Source-rate facade. Wide sources use overlapping analysis channels and one
// shared worker pool; returned event indices always refer to source I/Q.
class WidebandLoRa {
public:
    WidebandLoRa(const LoRaConfig&,SampleFormat format=SampleFormat::cu8);
    ~WidebandLoRa();
    void feed(std::span<const std::uint8_t>,std::uint64_t first_sample);
    void finish();
    void reset();
    Json take_events();
    Json results() const;
    Json status() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
