#include "signalscope/backend/recordings/event_store.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace signalscope {
namespace {
constexpr std::uint64_t metadata_reserve=256*1024;
struct RingBlock { std::uint64_t first; Bytes bytes; };
class StoreLock {
public:
    explicit StoreLock(const std::filesystem::path& path) {
#ifdef _WIN32
        if(_wsopen_s(&fd_,path.c_str(),_O_CREAT|_O_RDWR,_SH_DENYRW,_S_IREAD|_S_IWRITE)!=0)
            throw OperationError(ErrorCode::event_store_unavailable,"The event store is in use or could not be locked");
#else
        fd_=::open(path.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW,0600);
        if(fd_<0) throw OperationError(ErrorCode::event_store_unavailable,"Could not open the event store lock");
        if(flock(fd_,LOCK_EX|LOCK_NB)!=0) { ::close(fd_); fd_=-1; throw OperationError(ErrorCode::event_store_unavailable,"The event store is in use"); }
#endif
    }
    ~StoreLock() {
#ifdef _WIN32
        if(fd_>=0) _close(fd_);
#else
        if(fd_>=0) ::close(fd_);
#endif
    }
private: int fd_=-1;
};
std::uint64_t bytes_in(const std::filesystem::path& path) {
    std::uint64_t total=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(path)) {
        if(entry.is_symlink()) throw std::runtime_error("Symbolic links are disallowed inside event directories");
        if(entry.is_regular_file()) total+=entry.file_size();
    }
    return total;
}

std::optional<Json> read_snapshot_object(const std::filesystem::path& path) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if(status.type()==std::filesystem::file_type::not_found)return {};
    if(error || !std::filesystem::is_regular_file(status))
        throw OperationError(ErrorCode::event_store_unavailable,"Invalid snapshot file: "+path_to_utf8(path));
    std::ifstream file(path,std::ios::binary);
    if(!file) {
        // Retention can remove an event between enumerating and opening it.
        if(!std::filesystem::exists(path))return {};
        throw OperationError(ErrorCode::event_store_unavailable,"Could not open snapshot file: "+path_to_utf8(path));
    }
    auto result=Json::parse(file);
    if(!result.is_object())throw OperationError(ErrorCode::recording_invalid,"Snapshot JSON is not an object");
    return result;
}
}

Json readEventStoreSnapshot(const std::filesystem::path& root) {
    auto status=read_snapshot_object(root/"monitor-status.json");
    if(!status)throw OperationError(ErrorCode::event_store_unavailable,"Monitor status file is missing");
    std::vector<std::filesystem::path> paths;
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
        std::error_code error;
        const auto type=entry.symlink_status(error);
        if(!error && std::filesystem::is_directory(type) && entry.path().filename().u8string().starts_with(u8"event-"))
            paths.push_back(entry.path()/"capture.sigmf-meta");
    }
    std::sort(paths.begin(),paths.end());
    auto records=Json::array();
    for(const auto& path:paths) {
        if(auto metadata=read_snapshot_object(path))
            records.push_back({{"path",path_to_utf8(path)},{"name",path_to_utf8(path.parent_path().filename())},
                               {"metadata",std::move(*metadata)}});
    }
    const auto now=std::chrono::system_clock::now();
    return {{"remote_unix_seconds",std::chrono::duration<double>(now.time_since_epoch()).count()},
            {"status",std::move(*status)},{"records",std::move(records)}};
}

class EventStore::Impl {
public:
    Impl(const std::filesystem::path& path,const Config& cfg,const RetentionPolicy& policy,SampleFormat input_format)
        : root(path),config(cfg),p(policy),format(input_format),stride(bytes_per_pair(format)),tail_meter(format) {
        config.validate();
        if(!std::isfinite(p.ring_seconds) || p.ring_seconds<=0 || p.ring_seconds>30 ||
           !std::isfinite(p.pre_seconds) || p.pre_seconds<0 || p.pre_seconds>p.ring_seconds ||
           !std::isfinite(p.post_seconds) || p.post_seconds<0 || p.post_seconds>30 ||
           !std::isfinite(p.max_event_seconds) || p.max_event_seconds<=0 || p.max_event_seconds>60 ||
           !std::isfinite(p.max_age_hours) || p.max_age_hours<0 || p.max_age_hours>8760 ||
           p.ring_bytes<stride || p.ring_bytes>128ULL*1024*1024 || p.max_event_bytes<stride ||
           p.max_event_bytes>512ULL*1024*1024 || p.quota_bytes<p.max_event_bytes+metadata_reserve ||
           p.quota_bytes>64ULL*1024*1024*1024 || p.max_files<1 || p.max_files>4096)
            throw std::invalid_argument("Invalid event retention policy");
        ring_limit=std::min<std::uint64_t>(static_cast<std::uint64_t>(p.ring_seconds*config.rate),p.ring_bytes/stride);
        file_limit=std::min(sample_count(p.max_event_seconds,config.rate),p.max_event_bytes/stride);
        if(!ring_limit || !file_limit) throw std::invalid_argument("The buffer or event limit is smaller than one I/Q pair");
        if(std::filesystem::is_symlink(root)) throw OperationError(ErrorCode::event_store_unavailable,"The event store root must not be a symbolic link");
        const auto marker=root/"signalscope-store.json";
        if(std::filesystem::exists(root)) {
            if(!std::filesystem::is_regular_file(marker) || std::filesystem::file_size(marker)>4096)
                throw OperationError(ErrorCode::event_store_unavailable,"The existing directory is not an Foreshadow event store");
            std::ifstream file(marker); Json owner; file>>owner;
            if(owner.value("owner",std::string())!="signalscope-event-store-v1") throw OperationError(ErrorCode::event_store_unavailable,"Event store ownership marker does not match");
        } else {
            std::filesystem::create_directories(root);
            write_json(marker,{{"owner","signalscope-event-store-v1"},{"created_utc",utc_now()}});
        }
        lock=std::make_unique<StoreLock>(root/".store.lock");
        prune(0);
    }
    void prune(std::uint64_t reserve) {
        struct Entry { std::filesystem::path path; std::filesystem::file_time_type time; std::uint64_t bytes; };
        std::vector<Entry> entries;
        std::uint64_t total=0;
        for(const auto& item:std::filesystem::directory_iterator(root)) {
            if(item.is_symlink()) throw OperationError(ErrorCode::event_store_unavailable,"Symbolic links are disallowed in the event store");
            if(!item.is_directory() || item.path()==active_path || !item.path().filename().u8string().starts_with(u8"event-")) continue;
            const auto marker=item.path()/"signalscope-event.json";
            if(!std::filesystem::is_regular_file(marker)) continue;
            std::ifstream file(marker); Json owner; file>>owner;
            if(owner.value("owner",std::string())!="signalscope-event-v1") continue;
            const auto bytes=bytes_in(item.path());
            entries.push_back({item.path(),item.last_write_time(),bytes}); total+=bytes;
        }
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
            // 快速连续保存可能落在同一文件系统时间刻度内。
            // 目录名带创建时钟和序号，避免按目录遍历顺序随机淘汰。
            return a.time==b.time?a.path<b.path:a.time<b.time;
        });
        auto remaining=entries.size();
        const auto now=std::filesystem::file_time_type::clock::now();
        for(const auto& entry:entries) {
            const bool expired=p.max_age_hours>0 && std::chrono::duration<double,std::ratio<3600>>(now-entry.time).count()>p.max_age_hours;
            if(!expired && total+reserve<=p.quota_bytes && remaining+(reserve?1:0)<=p.max_files) continue;
            // 只删除固定根下、有自身标识且没有符号链接的事件目录。
            if(entry.path.parent_path()!=root || std::filesystem::is_symlink(entry.path)) throw std::runtime_error("Invalid event cleanup path");
            std::filesystem::remove_all(entry.path); total-=entry.bytes; --remaining; ++deleted;
        }
        stored_bytes=total;
    }
    void close(const std::string& reason) {
        if(!active) return;
        const bool quiet=tail_mode==1 && last_activity && next>=last_activity+quiet_samples;
        active->finish(reason,{{"stream_first_sample",record_first},{"stream_last_sample_exclusive",written_until},
            {"requested_end_sample",record_end},{"pretrigger_truncated",pre_truncated},{"events",active_events},
            {"tail_strategy",tail_mode==1?"band_activity":tail_mode==2?"conservative_size_limit":"fixed_post_no_band_parameters"},
            {"quiet_tail_observed",quiet},{"possibly_truncated",!quiet||reason=="size_or_time_limit"||reason=="analysis_gap"},
            {"last_activity_sample_exclusive",last_activity},{"frame_crc_verified",false},
            {"time_note","host save time is not RF arrival time; use stream sample indices"}});
        active.reset(); active_path.clear(); ++completed;
    }
    void add_band(const Json& event) {
        if(!event.contains("center_frequency_hz")||!event.contains("bandwidth_hz"))return;
        const double center=event["center_frequency_hz"].get<double>(),bw=event["bandwidth_hz"].get<double>();
        if(!std::isfinite(center)||!std::isfinite(bw)||bw<=0||std::abs(center-config.frequency)+bw/2>config.rate/2.0)return;
        for(const auto& band:bands)if(std::abs(band.first-center)<bw/8&&band.second==bw)return;
        if(bands.size()<4)bands.emplace_back(center,bw);
    }
    double activity_score(std::span<const std::uint8_t> bytes,std::uint64_t first) {
        if(bands.empty()||bytes.size()<64)return -100;
        const auto frame=tail_meter.measure(bytes,first,config.rate,config.frequency);
        double best=-100;
        for(const auto& band:bands) {
            std::vector<double> background;double power=0;std::size_t count=0;
            for(std::size_t i=0;i<frame.bins_dbfs.size();++i) {
                const double frequency=config.frequency-config.rate/2.0+i*double(config.rate)/frame.bins_dbfs.size();
                const double value=std::pow(10.0,frame.bins_dbfs[i]/10.0);
                if(std::abs(frequency-band.first)<=band.second/2){power+=value;++count;}
                else {
                    bool guard=false;
                    for(const auto& excluded:bands)if(std::abs(frequency-excluded.first)<=excluded.second*.75){guard=true;break;}
                    if(!guard)background.push_back(value);
                }
            }
            if(!count||background.size()<16)continue;
            const auto middle=background.begin()+static_cast<std::ptrdiff_t>(background.size()/2);
            std::nth_element(background.begin(),middle,background.end());
            const double noise=std::max(*middle,1e-14)*count;
            best=std::max(best,10*std::log10(std::max(power,1e-14)/noise));
        }
        return best;
    }
    void update_activity(std::span<const std::uint8_t> bytes,std::uint64_t first) {
        if(!active||bands.empty())return;
        const double score=activity_score(bytes,first);
        if(score>=6) {
            if(tail_mode!=2)tail_mode=1;
            last_activity=first+bytes.size()/stride;
            record_end=std::min(record_first+file_limit,std::max(record_end,last_activity+quiet_samples));
        } else if(tail_mode==0) {
            // 弱信号的包尾不能靠能量可靠判断：按有界上限保留，而非过早截断。
            tail_mode=2;record_end=record_first+file_limit;
        }
    }
    void close_diagnostic() {
        if(diagnostic) {diagnostic->finish("provisional_not_confirmed",{{"stream_first_sample",diag_first},{"probe",diag_probe},{"expires_unix_seconds",diag_expiry}});diagnostic.reset();}
    }
    void cleanup_diagnostic(bool force=false) {
        const auto path=root/"pending-chirp";
        if(!std::filesystem::exists(path))return;
        if(diagnostic && !force && std::chrono::system_clock::now().time_since_epoch()<std::chrono::seconds(diag_expiry))return;
        const auto marker=path/"signalscope-provisional.json";
        if(std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(marker) || std::filesystem::file_size(marker)>4096)
            throw std::runtime_error("Invalid ownership marker for the provisional candidate directory");
        std::ifstream file(marker);Json owner;file>>owner;
        file.close();
        if(owner.value("owner",std::string())!="signalscope-provisional-v1")throw std::runtime_error("Provisional candidate directory ownership marker does not match");
        const auto expiry=owner.at("expires_unix_seconds").get<std::int64_t>();
        if(force || std::chrono::system_clock::now().time_since_epoch()>=std::chrono::seconds(expiry)) {
            close_diagnostic();bytes_in(path);std::filesystem::remove_all(path);
        }
    }
    void append_diagnostic(std::span<const std::uint8_t> bytes,std::uint64_t first) {
        if(!diagnostic)return;
        const auto start=std::max(first,diag_written),stop=std::min(first+bytes.size()/stride,diag_end);
        if(start<stop) {diagnostic->append(bytes.subspan(static_cast<std::size_t>((start-first)*stride),static_cast<std::size_t>((stop-start)*stride)));diag_written=stop;}
    }
    void trigger_diagnostic(const Json& event) {
        const auto path=root/"pending-chirp";
        if(!p.diagnostic_candidates || std::filesystem::exists(path) || ring.empty())return;
        const auto limit=std::min<std::uint64_t>(config.rate*2ULL,8*1024*1024/stride);
        if(std::filesystem::space(root).available<limit*stride+p.reserve_bytes+4*1024*1024)return;
        const auto first=event.at("first_observed_sample").get<std::uint64_t>();
        const auto pre=std::uint64_t(config.rate/2);diag_first=std::max(first>pre?first-pre:0,ring.front().first);
        diag_end=diag_first+limit;diag_written=diag_first;diag_probe=event;
        diag_expiry=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()+120;
        Config cfg=config;cfg.seconds=double(limit)/config.rate;
        diagnostic=std::make_unique<Recorder>(path,cfg,"diagnostic-chirp-candidate-not-confirmed",format);
        write_json(path/"signalscope-provisional.json",{{"owner","signalscope-provisional-v1"},{"expires_unix_seconds",diag_expiry}});
        for(const auto& block:ring)append_diagnostic(block.bytes,block.first);
        ++diagnostic_count;
    }
    void append(std::span<const std::uint8_t> bytes,std::uint64_t first) {
        if(!active) return;
        const auto end=first+bytes.size()/stride;
        if(end<=written_until || written_until>=record_end) return;
        const auto start=std::max(first,written_until), stop=std::min({end,record_end,record_first+file_limit});
        if(first>written_until) throw std::runtime_error("A sample gap was detected in the event recording");
        if(start<stop) {
            active->append(bytes.subspan(static_cast<std::size_t>((start-first)*stride),static_cast<std::size_t>((stop-start)*stride)));
            written_until=stop;
        }
    }
    void feed(std::span<const std::uint8_t> bytes,std::uint64_t first,const Json& events) {
        if(bytes.size()%stride || first!=next) throw std::runtime_error("Event store input is discontinuous or misaligned");
        const auto end=first+bytes.size()/stride;
        update_activity(bytes,first);append(bytes,first);append_diagnostic(bytes,first); next=end;
        cleanup_diagnostic();
        if(!bytes.empty())ring.push_back({first,Bytes(bytes.begin(),bytes.end())});
        const auto keep_from=next>ring_limit?next-ring_limit:0;
        while(!ring.empty() && ring.front().first+ring.front().bytes.size()/stride<=keep_from) ring.pop_front();
        if(!ring.empty() && ring.front().first<keep_from) {
            auto& front=ring.front();
            front.bytes.erase(front.bytes.begin(),front.bytes.begin()+static_cast<std::ptrdiff_t>((keep_from-front.first)*stride));
            front.first=keep_from;
        }
        for(const auto& event:events) {
            if(event.value("evidence_level",std::string())=="chirp_probe" || event.value("evidence_level",std::string())=="energy_transient") {++probe_count;trigger_diagnostic(event);continue;}
            if(event.value("evidence_level",std::string())!="lora_structure_match") { ++discarded_candidates; continue; }
            if(event.dump().size()>1536) throw std::runtime_error("Event metadata exceeds the size limit");
            const auto observed=event.at("first_observed_sample").get<std::uint64_t>();
            const auto confirmed=event.at("confirmed_at_sample").get<std::uint64_t>();
            if(observed>confirmed || confirmed>next) throw std::runtime_error("The event falls outside the received sample range");
            const auto requested_end=confirmed+static_cast<std::uint64_t>(p.post_seconds*config.rate);
            if(active && observed<=record_end) {
                record_end=std::min(record_first+file_limit,std::max(record_end,requested_end));
                add_band(event);update_activity(bytes,first);
                if(active_events.size()<128) active_events.push_back(event);
                append(bytes,first); continue;
            }
            close("event_complete");
            prune(file_limit*stride+metadata_reserve);
            if(std::filesystem::space(root).available<file_limit*stride+p.reserve_bytes+4*1024*1024) { ++disk_refused; continue; }
            const auto pre=static_cast<std::uint64_t>(p.pre_seconds*config.rate);
            const auto requested_first=observed>pre?observed-pre:0;
            record_first=ring.empty()?next:std::max(requested_first,ring.front().first);
            pre_truncated=record_first>requested_first;
            record_end=std::min(record_first+file_limit,requested_end); written_until=record_first;
            bands.clear();add_band(event);tail_mode=0;last_activity=0;
            quiet_samples=std::max<std::uint64_t>(static_cast<std::uint64_t>(p.post_seconds*config.rate),config.rate/20);
            // 每次均创建新目录；无固定文件名覆盖，重启后也不会碰撞。
            for(;;) {
                std::ostringstream name; name<<"event-"<<std::chrono::system_clock::now().time_since_epoch().count()<<'-'<<sequence++;
                active_path=root/name.str(); if(!std::filesystem::exists(active_path)) break;
            }
            Config recording=config; recording.seconds=double(file_limit)/config.rate;
            active=std::make_unique<Recorder>(active_path,recording,"automatic-lora-event:"+event.value("source",std::string("unspecified")),format);
            // 结果可能晚于原始样本到达；先从捕获历史中的有效前导码开始跟踪，
            // 不能用结果到达时的安静背景代替实际包尾。
            for(const auto& block:ring) {
                const auto start=std::max(block.first,observed);
                const auto block_end=block.first+block.bytes.size()/stride;
                if(start<block_end)update_activity(std::span(block.bytes).subspan(static_cast<std::size_t>((start-block.first)*stride)),start);
            }
            write_json(active_path/"signalscope-event.json",{{"owner","signalscope-event-v1"},{"stream_first_sample",record_first}});
            active_events=Json::array({event}); ++triggered;
            for(const auto& block:ring) append(block.bytes,block.first);
        }
        if(active && (next>=record_end || written_until-record_first>=file_limit)) close(written_until-record_first>=file_limit?"size_or_time_limit":"event_complete");
        if(diagnostic && diag_written>=diag_end)close_diagnostic();
        if(next-last_prune>=std::uint64_t(config.rate)*60) { prune(0); last_prune=next; }
    }
    Json status() const {
        std::uint64_t buffered=0; for(const auto& block:ring) buffered+=block.bytes.size();
        return {{"ring_bytes",buffered},{"ring_limit_bytes",ring_limit*stride},{"quota_bytes",p.quota_bytes},
            {"max_event_bytes",file_limit*stride},{"max_event_seconds",double(file_limit)/config.rate},
            {"max_files",p.max_files},{"max_age_hours",p.max_age_hours},{"triggered",triggered},
            {"completed",completed},{"discarded_css_candidates",discarded_candidates},{"retention_deleted",deleted},
            {"chirp_probes",probe_count},{"diagnostic_candidates",diagnostic_count},{"diagnostic_enabled",p.diagnostic_candidates},
            {"disk_refused",disk_refused},{"active",bool(active)},{"stream_pairs",next}};
    }
    std::filesystem::path root,active_path; Config config; RetentionPolicy p;
    std::unique_ptr<StoreLock> lock;
    std::deque<RingBlock> ring; std::unique_ptr<Recorder> active;
    std::unique_ptr<Recorder> diagnostic;Json diag_probe;std::uint64_t diag_first=0,diag_end=0,diag_written=0,diagnostic_count=0,probe_count=0;std::int64_t diag_expiry=0;
    Json active_events=Json::array();
    std::uint64_t ring_limit=0,file_limit=0,next=0,record_first=0,record_end=0,written_until=0,sequence=0,last_prune=0;
    std::uint64_t triggered=0,completed=0,discarded_candidates=0,deleted=0,disk_refused=0,stored_bytes=0;
    bool pre_truncated=false;
    const SampleFormat format;
    const std::size_t stride;
    Spectrometer tail_meter;
    std::vector<std::pair<double,double>> bands;
    int tail_mode=0;std::uint64_t last_activity=0,quiet_samples=0;
};
EventStore::EventStore(const std::filesystem::path& p,const Config& c,const RetentionPolicy& policy,SampleFormat format):impl_(std::make_unique<Impl>(p,c,policy,format)) {}
EventStore::~EventStore() { try {impl_->close("monitor_interrupted");impl_->cleanup_diagnostic(true);} catch(...) {} }
void EventStore::feed(std::span<const std::uint8_t> bytes,std::uint64_t first,const Json& events) {impl_->feed(bytes,first,events);}
void EventStore::finish(const std::string& reason) {impl_->close(reason);impl_->cleanup_diagnostic(true);impl_->prune(0);}
void EventStore::discontinuity(std::uint64_t first) {impl_->close("analysis_gap");impl_->close_diagnostic();impl_->ring.clear();impl_->next=first;}
Json EventStore::status() const {return impl_->status();}
}
