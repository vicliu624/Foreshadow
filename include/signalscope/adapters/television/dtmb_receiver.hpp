#pragma once
#include "signalscope/backend/iq/stream.hpp"
#include <memory>

namespace signalscope::television {
struct DtmbReceiveOptions {
    IqStreamParameters input;
    // Zero selects automatic discovery. Manual values are validated together.
    unsigned pn_symbols=0,system_info=0,workers=4;
    unsigned maximum_iterations=100;
    double frequency_hz=0;
    std::string frame_body="auto";
    std::size_t probe_bytes=48*1024*1024;
    std::filesystem::path helper_directory;
};
struct DtmbMode {
    unsigned pn_symbols=0,system_info=0,fec_rate=0,codewords=0;
    std::string qam,interleaver,frame_body;
};
DtmbMode dtmbMode(unsigned pn,unsigned systemInfo,const std::string& body="c3780");
std::filesystem::path dtmbHelperDirectory();
void validateDtmbReceiveOptions(const DtmbReceiveOptions&);
Json dtmbReceiveParameters(const Json& pending=Json::object());
Json dtmbRecommendedParameters();
DtmbReceiveOptions dtmbReceiveOptions(IqStreamParameters,double frequency,const Json&);
Json dtmbReceiveDiagnostic(const std::string& key,const Json& arguments=Json::object());

// A single worker owns this state and every QProcess. Input is borrowed only
// during feed; the caller isolates live capture with its own byte-bounded queue.
// Transport/status callbacks run on that worker and must also be bounded.
// cancel is observed during all pipe/probe/drain waits, including finish().
class DtmbStreamDecoder {
public:
    using TransportSink=std::function<void(std::span<const std::uint8_t>)>;
    using StatusSink=std::function<void(const Json&)>;
    DtmbStreamDecoder(DtmbReceiveOptions,TransportSink,StatusSink={},std::function<bool()> cancel={});
    ~DtmbStreamDecoder();
    DtmbStreamDecoder(const DtmbStreamDecoder&)=delete;
    DtmbStreamDecoder& operator=(const DtmbStreamDecoder&)=delete;
    void feed(const IqBlockView&);
    void finish();
    Json snapshot() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
