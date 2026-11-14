#pragma once
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include "signalscope/backend/recordings/event_store.hpp"
#include "signalscope/adapters/modulation/lora.hpp"
#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include "signalscope/backend/sessions/iq_subscribers.hpp"
namespace signalscope {
struct MonitorObserver {
    // 回调可能来自采集/分析/录制不同线程，必须短小且线程安全；GUI 只替换最新快照。
    std::function<void(const Json&)> status;
    std::function<void(const SpectrumFrame&)> spectrum;
    // 录制路径已消费的全局原始样本位置，不等于磁盘实际保存的样本数。
    std::function<void(std::uint64_t)> recording_progress;
    // 分析线程已处理的全局样本位置；可用于进度观察及有界故障注入。
    std::function<void(std::uint64_t)> analysis_progress;
    // 可选的有限诊断录制，在写盘线程收到连续原始采样块。
    std::function<void(std::span<const std::uint8_t>,std::uint64_t)> audit_iq;
    bool enable_lora=true;
    NarrowbandSelection narrowband{false,false,false,false};
    // 仅在低优先级窄带线程调用；前端负责限制分析请求数。
    IqSubscribers analysis_iq;
    // Best-effort subscribers on the capture thread. Callbacks must not block.
    IqSubscribers capture_iq;
};
void validateMonitorReplaySpeed(bool from_recording,double replay_speed);
Json monitor(const Config&,const LoRaConfig&,const RetentionPolicy&,const std::filesystem::path& output,
             const std::filesystem::path& input,const std::function<bool()>& cancelled,
             const MonitorObserver& observer={},const ReceiverBackend& receiver=default_receiver(),
             double replay_speed=1);
}
