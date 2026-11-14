#include "signalscope/backend/iq/signal_source.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <charconv>
#include <mutex>

namespace signalscope {
namespace {
constexpr std::size_t metadata_limit=4*1024*1024,segment_limit=65536;
std::uint64_t index(const Json& node) {
    const auto text=node.get<std::string>();std::uint64_t result=0;const auto read=std::from_chars(text.data(),text.data()+text.size(),result);
    if(text.empty() || read.ec!=std::errc{} || read.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid source sample position");
    return result;
}
std::uint64_t sigmfOrigin(const ReplayInfo& info) {
    const auto& global=info.metadata.at("global");const auto result=global.find("signalscope:result");if(result==global.end() || !result->is_object())return 0;
    const auto first=result->find("stream_first_sample");if(first==result->end())return 0;
    if(first->is_number_unsigned() || (first->is_number_integer() && first->get<std::int64_t>()>=0))return first->get<std::uint64_t>();
    throw std::invalid_argument("Invalid recording source-clock origin");
}
SignalWindow readMapped(const SignalSourceInfo& info,SignalSampleRange range,std::stop_token stop,std::size_t maximum) {
    SignalWindow window;window.stream=info.stream;window.source_id=info.source_id;window.recording_path=info.metadata_path;window.recording_origin=info.time_origin;window.processing=info.processing;
    const auto end=range.first+range.count;const auto stride=bytes_per_pair(info.storage.format);std::size_t allocated=0;std::vector<std::uint64_t> boundaries;
    for(const auto& map:info.segments) {
        const auto first=std::max(range.first,map.first_stored_sample),last=std::min(end,map.first_stored_sample+map.count);if(first>=last)continue;
        SignalSegment segment;segment.first_sample=map.first_source_sample+(first-map.first_stored_sample);segment.discontinuous_before=first==map.first_stored_sample && map.discontinuous_before;
        segment.samples.reserve(static_cast<std::size_t>(last-first));const auto cost=segment.samples.capacity()*sizeof(std::complex<float>);
        if(cost>maximum-allocated)throw std::length_error("Signal source allocation exceeds its payload budget");
        allocated+=cost;
        boundaries.push_back(last);window.segments.push_back(std::move(segment));
    }
    std::size_t current=0;
    ReplayInfo raw;raw.data_path=info.storage.data_path;raw.rate=info.stream.sample_rate;raw.frequency=info.stream.center_frequency_hz;raw.pairs=info.storage.pairs;raw.format=info.storage.format;
    replay(raw,range.first,range.count,0,8192*stride,[&](auto bytes,auto first) {
        for(std::size_t at=0;at<bytes.size();at+=stride) {
            const auto stored=first+at/stride;while(current<boundaries.size() && stored>=boundaries[current])++current;
            if(current>=window.segments.size())throw std::invalid_argument("Signal source reader exceeded its selected map");
            window.segments[current].samples.push_back(read_iq_pair(bytes.data()+at,info.storage.format));
        }
    },[&]{return stop.stop_requested();});
    return window;
}
SignalSourceInfo openSigmf(const std::filesystem::path& path) {
    auto recording=inspect_recording(path);SignalSourceInfo info;info.metadata_path=std::filesystem::absolute(path);info.time_origin=sigmfOrigin(recording);info.stream={recording.rate,recording.frequency,recording.format};
    info.storage={std::move(recording.data_path),std::move(recording.metadata),recording.pairs,recording.format};
    info.source_id=path_to_utf8(info.metadata_path);info.segments.push_back({0,info.time_origin,info.storage.pairs,false});info.read=readMapped;return info;
}
SignalSourceInfo openArchive(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>metadata_limit)throw std::length_error("Signal source metadata exceeds its budget");
    std::ifstream file(path);Json data;file>>data;if(data.at("format")!="org.amc.signal-window" || data.at("version")!=1)throw std::invalid_argument("Unsupported signal archive version");
    if(data.at("storage").at("file")!="samples.cf32" || data.at("storage").at("encoding")!=sample_datatype(SampleFormat::cf32_le))throw std::invalid_argument("Unsupported archive storage layout");
    SignalSourceInfo info;info.metadata_path=std::filesystem::absolute(path);info.storage.data_path=info.metadata_path.parent_path()/"samples.cf32";
    info.storage.metadata=data;info.storage.format=sample_format_from_name(data.at("storage").at("encoding").get<std::string>());info.storage.pairs=index(data.at("storage").at("sample_count"));
    const auto rate=data.at("source").at("sample_rate_hz");
    if(!rate.is_number_unsigned() || rate.get<std::uint64_t>()>UINT32_MAX)throw std::invalid_argument("Invalid archive sample rate");
    info.stream={rate.get<std::uint32_t>(),data.at("source").at("center_frequency_hz").get<double>(),sample_format_from_name(data.at("source").at("encoding").get<std::string>())};
    info.source_id=data.at("source").at("id").get<std::string>();info.time_origin=index(data.at("time_origin_source_sample"));info.processing=data.at("processing");
    const auto& maps=data.at("segments");if(!maps.is_array() || maps.size()>segment_limit)throw std::length_error("Signal archive mapping exceeds its budget");
    for(const auto& map:maps)info.segments.push_back({index(map.at("first_stored_sample")),index(map.at("first_source_sample")),index(map.at("count")),map.at("discontinuous_before").get<bool>()});
    const auto stride=bytes_per_pair(info.storage.format);if(info.storage.pairs>UINT64_MAX/stride || std::filesystem::file_size(info.storage.data_path)!=info.storage.pairs*stride)throw std::invalid_argument("Signal archive data length does not match its mapping");
    info.read=readMapped;return info;
}
std::mutex provider_mutex;
std::vector<SignalSourceProvider>& providers() {
    static std::vector<SignalSourceProvider> value{
        {"sigmf","signal-sources","source.sigmf",{".sigmf-meta"},openSigmf},
        {"segmented-capture","signal-sources","source.capture",{".amc-window.json"},openArchive}};return value;
}
void validateInfo(const SignalSourceInfo& info) {
    SignalWindow check;check.stream=info.stream;check.processing=info.processing;check.validate();(void)sample_codec(info.storage.format);
    if(!info.read || info.metadata_path.empty() || info.source_id.empty() || !info.storage.metadata.is_object() || info.segments.empty() || info.segments.size()>segment_limit)throw std::invalid_argument("Incomplete signal source descriptor");
    std::uint64_t stored=0;std::optional<std::uint64_t> next_source;
    for(const auto& map:info.segments) {
        if(!map.count || map.first_stored_sample!=stored || map.count>UINT64_MAX-stored || map.count>UINT64_MAX-map.first_source_sample || (next_source && map.first_source_sample<*next_source))throw std::invalid_argument("Invalid signal source sample mapping");
        stored+=map.count;next_source=map.first_source_sample+map.count;
    }
    if(stored!=info.storage.pairs)throw std::invalid_argument("Signal source mapping has the wrong stored sample count");
}
}
std::optional<std::uint64_t> SignalSourceInfo::stored_index(std::uint64_t source) const {
    const auto it=std::upper_bound(segments.begin(),segments.end(),source,[](auto position,const auto& map){return position<map.first_source_sample;});
    if(it==segments.begin())return {};
    const auto& map=*std::prev(it);if(source-map.first_source_sample<map.count)return map.first_stored_sample+(source-map.first_source_sample);
    return {};
}
std::optional<std::uint64_t> SignalSourceInfo::source_index(std::uint64_t stored) const {
    const auto it=std::upper_bound(segments.begin(),segments.end(),stored,[](auto position,const auto& map){return position<map.first_stored_sample;});
    if(it==segments.begin())return {};
    const auto& map=*std::prev(it);if(stored-map.first_stored_sample<map.count)return map.first_source_sample+(stored-map.first_stored_sample);
    return {};
}
void SignalSourceInfo::validate() const {validateInfo(*this);}
std::vector<SignalSourceProvider> signal_source_providers(){std::lock_guard lock(provider_mutex);return providers();}
void register_signal_source_provider(SignalSourceProvider provider) {
    if(provider.protocol_version!=signalSourceProtocolVersion || provider.id.empty() || provider.label_domain.empty() || provider.label_key.empty() || !provider.open || provider.filename_suffixes.empty() ||
        provider.label_domain.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos)throw std::invalid_argument("Unsupported or incomplete signal source provider");
    for(const auto& suffix:provider.filename_suffixes)if(suffix.size()<2 || suffix.size()>80 || suffix.front()!='.' || suffix.find_first_of("/\\*?\r\n")!=std::string::npos)throw std::invalid_argument("Invalid signal source filename suffix");
    std::lock_guard lock(provider_mutex);for(const auto& known:providers()) {
        if(known.id==provider.id)throw std::invalid_argument("Duplicate signal source provider");
        for(const auto& a:known.filename_suffixes)for(const auto& b:provider.filename_suffixes)if(a.ends_with(b) || b.ends_with(a))throw std::invalid_argument("Overlapping signal source filename suffixes");
    }
    providers().push_back(std::move(provider));
}
SignalSourceInfo inspect_signal_source(const std::filesystem::path& path) {
    const auto filename=path_to_utf8(path.filename());
    for(const auto& provider:signal_source_providers())for(const auto& suffix:provider.filename_suffixes)if(filename.ends_with(suffix)) {
        auto info=provider.open(path);info.validate();return info;
    }
    throw std::invalid_argument("No registered signal source reader supports this filename");
}
SignalWindow read_signal_source(const SignalSourceInfo& info,std::uint64_t first,std::uint64_t count,std::stop_token stop,std::size_t maximum) {
    info.validate();if(!count || first>info.storage.pairs || count>info.storage.pairs-first)throw std::invalid_argument("Signal window must fit the stored source samples");
    if(count>maximum/sizeof(std::complex<float>))throw std::length_error("Signal window exceeds its normalized payload budget");
    SignalWindow canceled;canceled.stream=info.stream;canceled.source_id=info.source_id;canceled.recording_path=info.metadata_path;canceled.recording_origin=info.time_origin;
    if(stop.stop_requested())return canceled;
    auto window=info.read(info,{first,count},stop,maximum);
    if(stop.stop_requested())return canceled;
    window.validate();
    if(window.sample_count()!=count || window.payload_bytes()>maximum || window.stream.sample_rate!=info.stream.sample_rate || window.stream.center_frequency_hz!=info.stream.center_frequency_hz || window.stream.format!=info.stream.format)throw std::invalid_argument("Signal source reader violated its sample contract");
    std::size_t at=0,allocated=0;const auto end=first+count;
    for(const auto& map:info.segments) {
        const auto begin=std::max(first,map.first_stored_sample),last=std::min(end,map.first_stored_sample+map.count);if(begin>=last)continue;
        if(at>=window.segments.size())throw std::invalid_argument("Signal source reader omitted a source map");
        const auto& segment=window.segments[at++];
        if(segment.first_sample!=map.first_source_sample+(begin-map.first_stored_sample) || segment.samples.size()!=last-begin || segment.discontinuous_before!=(begin==map.first_stored_sample && map.discontinuous_before))throw std::invalid_argument("Signal source reader violated its source-clock mapping");
        const auto cost=segment.samples.capacity()*sizeof(std::complex<float>);if(cost>maximum-allocated)throw std::length_error("Signal source reader exceeded its allocation budget");allocated+=cost;
    }
    if(at!=window.segments.size())throw std::invalid_argument("Signal source reader added undeclared source maps");
    window.recording_path=info.metadata_path;window.source_id=info.source_id;window.recording_origin=info.time_origin;return window;
}
std::filesystem::path write_signal_archive(const std::filesystem::path& directory,const SignalWindow& window,std::stop_token stop,std::size_t maximum) {
    window.validate();if(window.segments.empty() || window.source_id.empty())throw std::invalid_argument("Cannot save an empty or unidentified signal window");
    if(window.payload_bytes()>maximum || window.segments.size()>segment_limit)throw std::length_error("Signal archive exceeds its payload or mapping budget");
    Json data={{"format","org.amc.signal-window"},{"version",1},{"source",{{"id",window.source_id},{"sample_rate_hz",window.stream.sample_rate},{"center_frequency_hz",window.stream.center_frequency_hz},
            {"encoding",sample_datatype(window.stream.format)},{"original_clock_origin",std::to_string(window.recording_origin)}}},
        {"storage",{{"file","samples.cf32"},{"encoding",sample_datatype(SampleFormat::cf32_le)},{"sample_count",std::to_string(window.sample_count())}}},
        {"time_origin_source_sample",std::to_string(window.recording_path.empty()?window.first_sample():window.recording_origin)},
        {"time_reference","source_sample_clock"},{"absolute_arrival_utc",nullptr},{"saved_utc",utc_now()},{"processing",window.processing},{"segments",Json::array()}};
    std::uint64_t stored=0;for(const auto& segment:window.segments){data["segments"].push_back({{"first_stored_sample",std::to_string(stored)},{"first_source_sample",std::to_string(segment.first_sample)},
        {"count",std::to_string(segment.samples.size())},{"discontinuous_before",segment.discontinuous_before}});stored+=segment.samples.size();}
    const auto metadata=data.dump(2);if(metadata.size()>=metadata_limit)throw std::length_error("Signal archive metadata exceeds its budget");
    if(stop.stop_requested())throw std::runtime_error("Signal archive canceled");
    const auto target=std::filesystem::absolute(directory);if(!std::filesystem::create_directory(target))throw std::runtime_error("Signal archive directory already exists");
    const auto samples=target/"samples.cf32",partial=target/"capture.amc-window.json.part";auto manifest=target/"capture.amc-window.json";
    try {
        std::ofstream output(samples,std::ios::binary);if(!output)throw std::runtime_error("Cannot create signal archive samples");
        const auto stride=bytes_per_pair(SampleFormat::cf32_le);Bytes bytes(8192*stride);
        for(const auto& segment:window.segments)for(std::size_t first=0;first<segment.samples.size();first+=8192) {
            if(stop.stop_requested())throw std::runtime_error("Signal archive canceled");
            const auto count=std::min<std::size_t>(8192,segment.samples.size()-first);
            for(std::size_t n=0;n<count;++n)write_iq_pair(bytes.data()+n*stride,SampleFormat::cf32_le,segment.samples[first+n]);
            output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(count*stride));if(!output)throw std::runtime_error("Signal archive sample write failed");
        }
        output.close();if(!output || stop.stop_requested())throw std::runtime_error("Signal archive write failed or canceled");
        std::ofstream header(partial,std::ios::binary);header<<metadata<<'\n';header.close();if(!header || stop.stop_requested())throw std::runtime_error("Signal archive metadata write failed or canceled");
        std::filesystem::rename(partial,manifest);return manifest;
    }catch(...) {
        // Only the three paths created by this operation are removed. Unknown
        // files in the new directory prevent its removal and are preserved.
        std::error_code error;if(!std::filesystem::is_symlink(target,error)){std::filesystem::remove(manifest,error);std::filesystem::remove(partial,error);std::filesystem::remove(samples,error);std::filesystem::remove(target,error);}throw;
    }
}
}
