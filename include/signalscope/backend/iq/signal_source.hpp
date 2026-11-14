#pragma once
#include "signalscope/backend/iq/signal_window.hpp"
#include <functional>

namespace signalscope {
inline constexpr unsigned signalSourceProtocolVersion=1;
struct SignalSourceSegment {
    std::uint64_t first_stored_sample=0,first_source_sample=0,count=0;
    bool discontinuous_before=false;
    bool operator==(const SignalSourceSegment&) const=default;
};
struct SignalSourceStorage {
    std::filesystem::path data_path;
    Json metadata=Json::object();
    std::uint64_t pairs=0;
    SampleFormat format=SampleFormat::cf32_le;
};
struct SignalSourceInfo {
    std::filesystem::path metadata_path;
    // Storage encoding can differ from the source's original encoding. All
    // readers return normalized samples through the common codec semantics.
    // Container layout belongs to the reader. These facts do not imply that
    // the entire data_path can be replayed as a headerless raw stream.
    SignalSourceStorage storage;
    IqStreamParameters stream{};
    std::string source_id;
    std::uint64_t time_origin=0;
    std::vector<SignalSourceSegment> segments;
    Json processing=Json::array();
    // One completed output segment for each intersecting declared map. I/O
    // block boundaries are internal; consumers verify this source-clock axis.
    std::function<SignalWindow(const SignalSourceInfo&,SignalSampleRange,std::stop_token,std::size_t)> read;
    void validate() const;
    std::optional<std::uint64_t> stored_index(std::uint64_t source_sample) const;
    std::optional<std::uint64_t> source_index(std::uint64_t stored_sample) const;
};
struct SignalSourceProvider {
    std::string id,label_domain,label_key;
    std::vector<std::string> filename_suffixes;
    std::function<SignalSourceInfo(const std::filesystem::path&)> open;
    unsigned protocol_version=signalSourceProtocolVersion;
};
// Static C++ providers, not a distributable binary ABI. Container parsing is
// owned here; math and UI never route by a container/provider identifier.
std::vector<SignalSourceProvider> signal_source_providers();
void register_signal_source_provider(SignalSourceProvider);
SignalSourceInfo inspect_signal_source(const std::filesystem::path&);
SignalWindow read_signal_source(const SignalSourceInfo&,std::uint64_t first_stored_sample,
    std::uint64_t count,std::stop_token stop={},std::size_t maximum_payload_bytes=32*1024*1024);
// Creates an exclusive new directory. A segmented capture is never presented
// as a continuous SigMF file. Returns capture.amc-window.json on success.
std::filesystem::path write_signal_archive(const std::filesystem::path& new_directory,
    const SignalWindow&,std::stop_token stop={},std::size_t maximum_payload_bytes=32*1024*1024);
}
