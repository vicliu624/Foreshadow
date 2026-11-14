#include "signalscope/backend/sessions/snapshot_analysis.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>
#ifdef __linux__
#include <sched.h>
#endif

namespace signalscope {
SnapshotCollector::SnapshotCollector(std::uint32_t rate,double center,double width,SampleFormat format,SnapshotObservation observation)
    :rate_(rate),center_(center),width_(width),format_(format),observation_(observation) {
    if(!std::isfinite(observation.duration_seconds) || observation.duration_seconds<=0 || observation.duration_seconds>1./3 ||
       !std::isfinite(observation.interval_seconds) || observation.interval_seconds<observation.duration_seconds || observation.interval_seconds>3600)
        throw std::invalid_argument("Invalid snapshot observation window or interval");
}

ModulationSnapshot SnapshotCollector::feed(std::span<const std::uint8_t> bytes,std::uint64_t first) {
    if(first!=next_) {previous_.clear();snapshot_.clear();if(observation_.trigger==SnapshotTrigger::periodic)eligible_=first;}
    const auto stride=bytes_per_pair(format_);
    const auto target=stride*std::size_t(rate_*observation_.duration_seconds);
    if(!target || bytes.empty() || bytes.size()%stride ||
       bytes.size()/stride>std::numeric_limits<std::uint64_t>::max()-first) {
        previous_.clear();
        snapshot_.clear();
        return {};
    }
    next_=first+bytes.size()/stride;
    if(observation_.trigger==SnapshotTrigger::periodic) {
        std::size_t offset=0;
        if(snapshot_.empty()) {
            if(next_<=eligible_)return {};
            start_=std::max(first,eligible_);offset=static_cast<std::size_t>(start_-first)*stride;snapshot_.reserve(target);
        }
        const auto captured=bytes.subspan(offset,std::min(bytes.size()-offset,target-snapshot_.size()));
        snapshot_.insert(snapshot_.end(),captured.begin(),captured.end());
        if(snapshot_.size()<target)return {};
        const auto interval=std::max<std::uint64_t>(target/stride,static_cast<std::uint64_t>(rate_*observation_.interval_seconds));
        eligible_=start_+std::min(interval,std::numeric_limits<std::uint64_t>::max()-start_);
        return {std::exchange(snapshot_,{}),start_,rate_,center_,width_,format_,observation_};
    }
    double energy=0;
    for(std::size_t i=0;i+stride<=bytes.size();i+=64) {
        const auto value=read_iq_pair(bytes.data()+i,format_)*128.f;
        const double x=value.real(),y=value.imag();
        energy+=x*x+y*y;
    }
    energy/=std::max(1.,std::ceil(bytes.size()/64.));
    if(baseline_==0)baseline_=energy;
    const bool triggered=snapshot_.empty() && first>=eligible_ && energy>baseline_*1.3;
    if(triggered) {
        start_=first-previous_.size()/stride;
        snapshot_.reserve(target);
        snapshot_=previous_;
    }
    if(triggered || !snapshot_.empty()) {
        const auto captured=bytes.first(std::min(bytes.size(),target-snapshot_.size()));
        snapshot_.insert(snapshot_.end(),captured.begin(),captured.end());
    } else baseline_+=.025*(energy-baseline_);
    const auto tail=bytes.last(std::min(bytes.size(),target));
    previous_.assign(tail.begin(),tail.end());
    if(snapshot_.size()<target)return {};
    eligible_=next_+std::min<std::uint64_t>(static_cast<std::uint64_t>(rate_*observation_.interval_seconds),std::numeric_limits<std::uint64_t>::max()-next_);
    return {std::exchange(snapshot_,{}),start_,rate_,center_,width_,format_,observation_};
}

struct SnapshotAnalysis::Impl {
    struct Lane {
        SnapshotObservation observation;
        std::mutex mutex;
        ModulationSnapshot pending;
        std::future<Json> native;
        std::atomic<bool> busy=false,active=false;
        std::atomic<std::uint64_t> triggered=0,skipped=0,completed=0,offered_bytes=0,skipped_bytes=0,pending_bytes=0,inflight_bytes=0;
        explicit Lane(SnapshotObservation policy):observation(policy){}
    };
    struct Collection {SnapshotCollector collector;Lane* lane;};
    static bool samePolicy(const SnapshotObservation& a,const SnapshotObservation& b) {
        return a.trigger==b.trigger && a.duration_seconds==b.duration_seconds && a.interval_seconds==b.interval_seconds;
    }
    ~Impl(){stop();}
    void stop() {
        for(auto& lane:lanes_)if(lane->native.valid()){try{lane->native.get();}catch(...){}}
    }
    void reset(SnapshotSelection selection) {
        stop();
        selection_=selection;
        lanes_.clear();
        // Provision stable lane addresses before either producer or poller can
        // run. Actual stream-rate eligibility is resolved by the producer.
        for(const auto& policy:snapshotObservations(selection,100000000))lanes_.push_back(std::make_unique<Lane>(policy));
        extended_events_=Json::array();
        extended_enabled_=selection.any();
        extended_failed_=false;
    }
    IqBlockSubscriber makeFeed(double width) {
        if(!extended_enabled_)return {};
        return [this,width,collectors=std::vector<Collection>{},initialized=false](const IqBlockView& block) mutable {
            const auto& stream=block.stream;
            if(!initialized) {
                for(const auto& policy:snapshotObservations(selection_,stream.sample_rate)) {
                    const auto found=std::find_if(lanes_.begin(),lanes_.end(),[&](const auto& lane){return samePolicy(lane->observation,policy);});
                    if(found==lanes_.end())continue;
                    (*found)->active=true;collectors.push_back({SnapshotCollector(stream.sample_rate,stream.center_frequency_hz,width,stream.format,policy),found->get()});
                }
                initialized=true;
            }
            for(auto& entry:collectors) {
                auto snapshot=entry.collector.feed(block.bytes,block.first_sample);
                if(snapshot.bytes.empty())continue;
                auto& lane=*entry.lane;++lane.triggered;lane.offered_bytes+=snapshot.bytes.size();
                if(lane.busy.load()){++lane.skipped;lane.skipped_bytes+=snapshot.bytes.size();continue;}
                std::unique_lock lock(lane.mutex,std::try_to_lock);
                if(lock.owns_lock() && !lane.busy.exchange(true)){lane.pending_bytes=snapshot.bytes.size();lane.pending=std::move(snapshot);}
                else {++lane.skipped;lane.skipped_bytes+=snapshot.bytes.size();}
            }
        };
    }
    void poll() {
        if(!extended_enabled_)return;
        for(auto& pointer:lanes_) {
        auto& lane=*pointer;
        if(lane.native.valid() && lane.native.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {append(lane.native.get());++lane.completed;}
            catch(...) {extended_failed_=true;}
            lane.inflight_bytes=0;lane.busy=false;
        }
        if(lane.native.valid())continue;
        ModulationSnapshot snapshot;
        {std::lock_guard lock(lane.mutex);snapshot=std::exchange(lane.pending,{});lane.pending_bytes=0;}
        if(snapshot.bytes.empty())continue;
        lane.inflight_bytes=snapshot.bytes.size();
        if(selection_.any())try {
            lane.native=std::async(std::launch::async,[snapshot=std::move(snapshot),selection=selection_]{
#ifdef __linux__
                sched_param priority{};
                sched_setscheduler(0,SCHED_IDLE,&priority);
#endif
                return detectSnapshot(snapshot.bytes,snapshot.format,
                                 snapshot.rate,snapshot.center,snapshot.width,snapshot.first,
                                 snapshotObservationSelection(selection,snapshot.observation,snapshot.rate));
            });
        } catch(...) {extended_failed_=true;}
        if(!lane.native.valid()){lane.busy=false;lane.inflight_bytes=0;}
        }
    }
    void append(const Json& events) {
        for(const auto& event:events) {
            extended_events_.push_back(event);
            if(extended_events_.size()>16)extended_events_.erase(extended_events_.begin());
        }
    }
    Json status() const {
        std::uint64_t triggered=0,skipped=0,completed=0,offered=0,skippedBytes=0,pending=0,inflight=0;
        bool busy=false;Json policies=Json::array();
        for(const auto& lane:lanes_) {
            if(!lane->active.load())continue;
            const auto t=lane->triggered.load(),s=lane->skipped.load(),c=lane->completed.load(),o=lane->offered_bytes.load(),
                sb=lane->skipped_bytes.load(),p=lane->pending_bytes.load(),f=lane->inflight_bytes.load();
            triggered+=t;skipped+=s;completed+=c;offered+=o;skippedBytes+=sb;pending+=p;inflight+=f;busy|=lane->busy.load();
            policies.push_back({{"trigger",lane->observation.trigger==SnapshotTrigger::periodic?"periodic":"energy"},
                {"duration_seconds",lane->observation.duration_seconds},{"interval_seconds",lane->observation.interval_seconds},
                {"triggered",t},{"skipped_busy",s},{"completed",c},{"pending_bytes",p},{"inflight_bytes",f}});
        }
        return {{"triggered",triggered},{"skipped_busy",skipped},{"completed",completed},{"busy",busy},{"failed",extended_failed_},
            {"offered_snapshot_bytes",offered},{"skipped_snapshot_bytes",skippedBytes},{"pending_bytes",pending},{"inflight_bytes",inflight},
            {"observation_policies",policies.size()},{"policies",std::move(policies)}};
    }
    std::vector<std::unique_ptr<Lane>> lanes_;
    Json extended_events_=Json::array();
    SnapshotSelection selection_;
    bool extended_enabled_=false,extended_failed_=false;
};

SnapshotAnalysis::SnapshotAnalysis():impl_(std::make_unique<Impl>()){}
SnapshotAnalysis::~SnapshotAnalysis()=default;
void SnapshotAnalysis::reset(SnapshotSelection selection){impl_->reset(selection);}
void SnapshotAnalysis::poll(){impl_->poll();}
IqBlockSubscriber SnapshotAnalysis::makeFeed(double width) {
    return impl_->makeFeed(width);
}
bool SnapshotAnalysis::available() const {return impl_->extended_enabled_&&!impl_->extended_failed_;}
const Json& SnapshotAnalysis::events() const {return impl_->extended_events_;}
Json SnapshotAnalysis::status() const {return impl_->status();}
}
