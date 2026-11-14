#pragma once
#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/backend/iq/reception.hpp"
#include <memory>
#include <optional>

namespace signalscope {
// One physical input. Callbacks are borrowed/nonblocking and may run on its
// capture thread. Destruction joins that thread before returning. Releasing a
// lease is nonblocking; it never stops another consumer's use of the input.
class ReceiverSession {
public:
    ReceiverSession(const ReceiverBackend&,Config identity,std::optional<ReceiverCapabilities> capabilities = {});
    ~ReceiverSession();
    ReceiverSession(const ReceiverSession&)=delete;
    std::shared_ptr<ReceptionLease> acquire(ReceptionDemand,ReceptionConsumer);
    // Monitor remains an independent pipeline, with a bounded byte queue and
    // its own zero-based sample clock. A full queue fails that lease explicitly.
    Json stream(const Config&,const BlockSink&,const std::function<bool()>&,const ReceiverReady&);
    Json snapshot() const;
    Json describe();
    void disconnect();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
