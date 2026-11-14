#pragma once
#include "signalscope/backend/iq/stream.hpp"
#include "signalscope/backend/iq/acquisition_conditions.hpp"
#include <memory>
#include <optional>
#include <stdexcept>

namespace signalscope {
inline constexpr unsigned receptionProtocolVersion=2;
// Versioned input/lifetime protocol. It has no session, driver, GUI or DSP
// implementation dependency. Requirements describe the whole occupied band.
struct ReceptionDemand {
    double frequency_hz=0,passband_hz=0;
    std::uint32_t minimum_rate=1,maximum_rate=UINT32_MAX;
    std::optional<Config> exact;
    bool configured_receiver=false;
    std::optional<Config> preferences=std::nullopt;
};
struct ReceptionStatus {
    std::string state="starting",detail;
    std::optional<IqStreamParameters> stream;
    Json requested=Json::object(),applied=Json::object();
    Json diagnostic=Json::object();
    std::uint64_t generation=0;
    AcquisitionConditions conditions;
    ReceptionStatus(std::string value="starting",std::string reason={}):state(std::move(value)),detail(std::move(reason)){}
};
struct ReceptionConsumer {
    std::function<void(const ReceptionStatus&)> changed;
    IqBlockSubscriber offer;
};
class ReceptionError:public std::runtime_error {
public:
    explicit ReceptionError(ReceptionStatus value):std::runtime_error("reception."+value.state+(value.detail.empty()?"":": "+value.detail)),status(std::move(value)){}
    const ReceptionStatus status;
};
class ReceptionLease {
public:
    virtual ~ReceptionLease()=default;
    // Stops future dispatch without joining an in-flight callback. Callbacks
    // retain their worker state independently of a panel/widget's lifetime.
    virtual void release() noexcept=0;
};
using ReceptionAcquire=std::function<std::shared_ptr<ReceptionLease>(const ReceptionDemand&,ReceptionConsumer)>;
}
