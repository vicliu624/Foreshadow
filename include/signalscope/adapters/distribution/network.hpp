#pragma once
#include "signalscope/backend/distribution/distribution.hpp"
namespace signalscope::distribution {
class NetworkTransport final : public Transport {
  public:
    explicit NetworkTransport(bool allowLoopbackForTests = false, int idleTimeoutMs = 30000)
        : loopback_(allowLoopbackForTests), timeout_(idleTimeoutMs) {}
    void download(const std::string &, const std::filesystem::path &, std::uint64_t, std::stop_token = {},
                  const Progress &progress = {}) override;

  private:
    bool loopback_;
    int timeout_;
};
} // namespace signalscope::distribution
