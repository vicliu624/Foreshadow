#include "signalscope/backend/iq/iq.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

namespace signalscope {
void Config::validate() const {
    if (!rate || rate > 100000000) throw std::invalid_argument("Sample rate must be between 1 and 100000000 S/s; receiver and analysis limits are checked separately");
    if (!frequency) throw std::invalid_argument("Center frequency must be positive");
    if(input_mode.empty() || input_mode.size()>128)
        throw std::invalid_argument("Receiver input mode must be a nonempty plugin identifier");
    if(!receiver_settings.is_object())throw std::invalid_argument("Receiver settings must be an object");
    if(!std::isfinite(if_bandwidth_hz) || if_bandwidth_hz<0 || if_bandwidth_hz>100000000)
        throw std::invalid_argument("IF bandwidth must be finite, nonnegative and at most 100 MHz");
    if (!std::isfinite(required_passband_hz) || required_passband_hz<0)
        throw std::invalid_argument("Required receiver passband must be finite and nonnegative");
    if (block_bytes < 512 || block_bytes > 4*1024*1024 || block_bytes % 512)
        throw std::invalid_argument("Block size must be a multiple of 512 between 512 and 4194304 bytes");
    if (!queue_blocks || queue_blocks > 4096 || queue_blocks > (256u*1024u*1024u)/block_bytes)
        throw std::invalid_argument("Queue capacity must be bounded and at most 256 MiB");
    if (analysis_queue_bytes<512 || analysis_queue_bytes>128u*1024u*1024u ||
        !std::isfinite(analysis_queue_seconds) || analysis_queue_seconds<=0 || analysis_queue_seconds>30)
        throw std::invalid_argument("Analysis buffer must be 512 bytes to 128 MiB with a duration greater than 0 and at most 30 seconds");
    if (!std::isfinite(seconds) || seconds<0) throw std::invalid_argument("Monitoring duration must be finite and nonnegative. Zero means continuous monitoring");
    if (seconds>0) sample_count(seconds, rate);
    if (!std::isfinite(settle_seconds) || settle_seconds < 0 || settle_seconds > 10)
        throw std::invalid_argument("Settling duration must be between 0 and 10 seconds");
}
std::uint64_t sample_count(double seconds, std::uint32_t rate) {
    if (!rate || !std::isfinite(seconds) || seconds <= 0 || seconds > 366*86400.0)
        throw std::invalid_argument("Duration must be positive and at most 366 days, with a nonzero sample rate");
    const auto count = std::floor(seconds * rate);
    if (count < 1) throw std::invalid_argument("Duration is shorter than one I/Q pair");
    return static_cast<std::uint64_t>(count);
}
std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out; out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ"); return out.str();
}
void write_json(const std::filesystem::path& path, const Json& j) {
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw OperationError(ErrorCode::recording_write,"Could not write metadata: " + path_to_utf8(temp));
    out << j.dump(2) << '\n'; out.close();
    if (!out) throw OperationError(ErrorCode::recording_write,"Metadata write failed");
    // 同目录重命名：POSIX 原子替换；Windows 使用备份，失败时恢复旧文件。
#ifdef _WIN32
    auto backup = path;
    backup += ".bak";
    if (std::filesystem::exists(path)) {
        std::filesystem::rename(path, backup);
        try { std::filesystem::rename(temp, path); }
        catch (...) { std::filesystem::rename(backup, path); throw; }
        std::filesystem::remove(backup);
    } else std::filesystem::rename(temp, path);
#else
    std::filesystem::rename(temp, path);
#endif
}
namespace {
void accumulatePower(PowerStats& stats,std::span<const std::uint8_t> bytes,SampleFormat format) {
    const auto& codec=sample_codec(format);
    if(bytes.size()%codec.pair_bytes)throw std::invalid_argument("I/Q data contains an incomplete pair");
    for(std::size_t n=0;n<bytes.size();n+=codec.pair_bytes) {
        const auto value=codec.decode(bytes.data()+n);const double i=value.real(),q=value.imag();
        stats.energy+=i*i+q*q;stats.sum_i+=i;stats.sum_q+=q;++stats.pairs;
        if(codec.clipped(bytes.data()+n))++stats.clipped_pairs;
    }
}
}
void PowerStats::add(std::span<const std::uint8_t> bytes){accumulatePower(*this,bytes,format_);}
void PowerStats::add_ci16_le(std::span<const std::uint8_t> bytes){accumulatePower(*this,bytes,SampleFormat::ci16_le);}
Json PowerStats::json() const {
    const auto* reference=sample_codec(format_).power_reference;
    Json j={{"pairs",pairs},{"clipped_pairs",clipped_pairs},{"power_reference",reference}};
    const auto power = pairs ? energy/double(pairs) : 0;
    j["mean_power_dbfs"] = power>0 ? Json(10*std::log10(power)) : Json(nullptr);
    j["dc_i"] = pairs ? sum_i/double(pairs) : 0;
    j["dc_q"] = pairs ? sum_q/double(pairs) : 0;
    return j;
}
Recorder::Recorder(const std::filesystem::path& dir, const Config& c, const std::string& source,SampleFormat format)
    :directory_(dir),format_(format) {
    c.validate();
    // 原子创建独占目录，防止覆盖与并发写入同一录制。
    if (!std::filesystem::create_directory(dir)) throw OperationError(ErrorCode::recording_open,"Recording directory already exists: "+path_to_utf8(dir));
    const auto required=sample_count(c.seconds,c.rate)*bytes_per_pair(format_)+4*1024*1024;
    if (std::filesystem::space(dir).available<required)
        throw OperationError(ErrorCode::recording_space,"Insufficient disk space for the requested recording and safety margin");
    meta_={{"global",{{"core:datatype",sample_datatype(format_)},{"core:sample_rate",c.rate},{"core:version","1.2.6"},
        {"core:recorder","SignalScope 0.1.0"},{"core:description","Wideband I/Q for offline analysis"},
        {"core:extensions",Json::array({{{"name","signalscope"},{"version","0.1.0"},{"optional",true}}})},
        {"signalscope:source",source},{"signalscope:status","recording"},
        {"signalscope:gain_mode",c.auto_gain?"auto":"manual"},{"signalscope:gain_tenths_db",c.gain_tenths},
        {"signalscope:time_source","host observation; no hardware timestamp"},
        {"signalscope:continuity","unverified"}}},
        {"captures",Json::array()}, {"annotations",Json::array()}};
    data_.open(dir/"capture.sigmf-data",std::ios::binary);
    if (!data_) throw OperationError(ErrorCode::recording_write,"Could not create the recording data file");
    meta_["captures"].push_back({{"core:sample_start",0},{"core:frequency",c.frequency}});
    publish();
}
void Recorder::append(std::span<const std::uint8_t> bytes) {
    const auto stride=bytes_per_pair(format_);
    if (finished_ || bytes.size()%stride) throw std::runtime_error("Invalid recording state or I/Q block length");
    if (bytes.empty()) return;
    if (!pairs_) meta_["captures"][0]["core:datetime"]=utc_now();
    data_.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!data_) throw OperationError(ErrorCode::recording_write,"I/Q write failed. Check available disk space");
    pairs_+=bytes.size()/stride;
}
void Recorder::publish() { write_json(directory_/"capture.sigmf-meta",meta_); }
void Recorder::annotate(const Json& annotation) { meta_["annotations"].push_back(annotation); }
void Recorder::finish(const std::string& status, const Json& extra) {
    if (finished_) throw std::runtime_error("The recording is already closed");
    data_.flush(); if (!data_) throw OperationError(ErrorCode::recording_write,"Could not flush I/Q data to disk");
    data_.close(); if (!data_) throw OperationError(ErrorCode::recording_write,"Could not close the I/Q data file");
    meta_["global"]["signalscope:status"]=status;
    meta_["global"]["signalscope:sample_count"]=pairs_;
    meta_["global"]["signalscope:result"]=extra;
    if (meta_["global"]["signalscope:source"]=="derived-replay") {
        meta_["captures"][0].erase("core:datetime");
        meta_["global"]["signalscope:time_source"]="parent recording sample index; no new RF observation";
    }
    if (meta_["global"]["signalscope:source"].get<std::string>().starts_with("automatic-lora-event")) {
        meta_["captures"][0].erase("core:datetime");
        meta_["global"]["signalscope:time_source"]="stream sample index; save time is not RF arrival time";
        meta_["global"]["signalscope:saved_utc"]=utc_now();
    }
    publish(); finished_=true;
}
Recorder::~Recorder() {
    if (!finished_) { try { finish("interrupted"); } catch (...) {} }
}
ReplayInfo inspect_recording(const std::filesystem::path& path) try {
    if (path.extension()!=".sigmf-meta") throw OperationError(ErrorCode::recording_invalid,"Expected a .sigmf-meta file");
    if (std::filesystem::file_size(path)>4*1024*1024) throw OperationError(ErrorCode::recording_invalid,"Metadata exceeds the 4 MiB limit");
    std::ifstream in(path);
    if (!in) throw OperationError(ErrorCode::recording_open,"Could not open SigMF metadata");
    Json j; in >> j;
    const auto& g=j.at("global");
    const auto& datatype=g.at("core:datatype");
    SampleFormat format;
    try {format=sample_format_from_name(datatype.get<std::string>());}
    catch(const std::exception& e){throw OperationError(ErrorCode::recording_format,e.what());}
    const auto stride=bytes_per_pair(format);
    if (g.value("core:num_channels",1)!=1 || g.value("core:offset",0)!=0 || g.value("core:trailing_bytes",0)!=0 || g.contains("core:dataset"))
        throw OperationError(ErrorCode::recording_format,"Multiple channels, byte offsets, trailing bytes and external datasets are unsupported");
    const double rate=g.at("core:sample_rate").get<double>();
    if (!std::isfinite(rate) || rate<1 || rate>100000000 || std::floor(rate)!=rate) throw OperationError(ErrorCode::recording_invalid,"Sample rate must be a valid integer");
    const auto& captures=j.at("captures");
    if (!captures.is_array() || captures.size()!=1 || captures[0].at("core:sample_start")!=0 || captures[0].value("core:header_bytes",0)!=0)
        throw OperationError(ErrorCode::recording_format,"Expected a single capture beginning at sample zero");
    const double frequency=captures[0].at("core:frequency").get<double>();
    if (!std::isfinite(frequency) || frequency<=0) throw OperationError(ErrorCode::recording_invalid,"Invalid center frequency");
    auto data=path; data.replace_extension(".sigmf-data");
    const auto size=std::filesystem::file_size(data);
    if (size%stride) throw OperationError(ErrorCode::recording_invalid,"Data file contains an incomplete I/Q pair");
    if (g.contains("signalscope:sample_count") && g.at("signalscope:sample_count")!=size/stride)
        throw OperationError(ErrorCode::recording_invalid,"Metadata sample count does not match the data file length");
    return {data,std::move(j),static_cast<std::uint32_t>(rate),frequency,size/stride,format};
} catch (const Json::exception& error) {
    throw OperationError(ErrorCode::recording_invalid,error.what());
} catch (const std::filesystem::filesystem_error& error) {
    throw OperationError(ErrorCode::recording_open,error.what());
}
void replay(const ReplayInfo& info, std::uint64_t first, std::uint64_t count, double speed,
            std::size_t block_bytes, const BlockSink& sink, const std::function<bool()>& cancelled) {
    const auto stride=bytes_per_pair(info.format);
    if (!std::isfinite(speed) || speed<0 || speed>1000 || first>info.pairs || count>info.pairs-first || block_bytes<stride || block_bytes>4*1024*1024 || block_bytes%stride)
        throw std::invalid_argument("Invalid replay range, speed or block size");
    std::ifstream in(info.data_path,std::ios::binary);
    if (!in) throw OperationError(ErrorCode::recording_open,"Could not open the I/Q file");
    in.seekg(static_cast<std::streamoff>(first*stride));
    Bytes bytes(block_bytes); std::uint64_t read=0;
    const auto start=std::chrono::steady_clock::now();
    while (read<count && !cancelled()) {
        if (speed>0) {
            const auto deadline=start+std::chrono::duration<double>(double(read)/info.rate/speed);
            while (std::chrono::steady_clock::now()<deadline && !cancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (cancelled()) break;
        }
        const auto size=static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size(),(count-read)*stride));
        in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
        if (in.gcount()!=static_cast<std::streamsize>(size)) throw OperationError(ErrorCode::recording_invalid,"The I/Q file was truncated during replay");
        sink(std::span(bytes.data(),size),first+read); read+=size/stride;
    }
}
}
