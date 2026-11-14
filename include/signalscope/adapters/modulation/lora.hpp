#pragma once
#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/dsp/analysis_workers.hpp"
#include <memory>
namespace signalscope {
std::vector<double> defaultLiveLoRaBandwidths();
struct LoRaConfig {
    std::uint32_t rate = 2000000;
    double receiver_frequency = 470500000;
    double bandwidth = 125000;
    int sf_min = 7, sf_max = 12;
    unsigned repeat_symbols = 6;
    double peak_to_mean_db = 18;
    double search_width = 1000000;
    unsigned time_phases = 2;
    unsigned workers = recommendedAnalysisWorkers();
    bool normal_iq = true, inverted_iq = true;
    std::vector<double> bandwidth_candidates;
    unsigned probe_stride_symbols = 4;
    bool dense_short_symbols = false;
};
bool loraTimingSupported(const LoRaConfig& config);
class LoRaDetector;
class ParallelChannels;
struct LoRaFloatBlock {
    LoRaDetector* detector=nullptr;
    std::span<const std::complex<float>> samples;
    std::uint64_t first_sample=0;
};
// Share a worker budget across independent channels and their search models.
void feedLoRaChannels(std::span<const LoRaFloatBlock>,ParallelChannels&);
// 流式 Chirp 与 LoRa 前导码结构检测；不解调载荷或解析协议。
class LoRaDetector {
public:
    explicit LoRaDetector(const LoRaConfig&,SampleFormat format=SampleFormat::cu8,bool inline_processing=false);
    ~LoRaDetector();
    void feed(std::span<const std::uint8_t>,std::uint64_t first_sample);
    void feedFloat(std::span<const std::complex<float>>,std::uint64_t first_sample);
    Json results() const;
    Json take_events();
    void reset();
private:
    friend void feedLoRaChannels(std::span<const LoRaFloatBlock>,ParallelChannels&);
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
