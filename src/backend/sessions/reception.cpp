#include "signalscope/backend/sessions/reception.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace signalscope {
namespace {
struct Use final:ReceptionLease {
    ReceptionDemand demand;ReceptionConsumer consumer;std::atomic_bool active=true;
    std::shared_ptr<std::atomic<std::size_t>> reservations;
    void release() noexcept override {if(active.exchange(false) && reservations)--*reservations;}
    ~Use() override {release();}
};
struct Handle final:ReceptionLease {
    std::shared_ptr<Use> use;
    explicit Handle(std::shared_ptr<Use> value):use(std::move(value)){}
    void release() noexcept override {if(use)use->release();}
    ~Handle() override {release();}
};
void publish(const std::shared_ptr<Use>& use,const ReceptionStatus& status) noexcept {
    if(!use->active)return;
    try {if(use->consumer.changed)use->consumer.changed(status);}
    catch(...) {use->release();}
}
bool covers(const ReceptionDemand& d,const Config& cfg,const ReceiverReadback& readback) {
    if(!std::isfinite(d.frequency_hz) || !std::isfinite(d.passband_hz) || d.frequency_hz<0 || d.passband_hz<=0 || !d.minimum_rate || d.minimum_rate>d.maximum_rate ||
       !std::isfinite(readback.sample_rate_hz) || readback.sample_rate_hz<1 || readback.sample_rate_hz>UINT32_MAX || !std::isfinite(readback.center_frequency_hz) || readback.center_frequency_hz<0 || readback.center_frequency_hz>=std::ldexp(1.,64) ||
       !std::isfinite(readback.filter_bandwidth_hz) || readback.filter_bandwidth_hz<0)return false;
    const auto span=!d.exact && readback.filter_bandwidth_hz>0?std::min(readback.sample_rate_hz,readback.filter_bandwidth_hz):readback.sample_rate_hz;
    if(readback.sample_rate_hz<d.minimum_rate || readback.sample_rate_hz>d.maximum_rate ||
       std::abs(d.frequency_hz-readback.center_frequency_hz)+d.passband_hz/2>span/2+.5)return false;
    if(d.exact) {
        auto expected=requestedReceiverConfiguration(*d.exact),actual=requestedReceiverConfiguration(cfg);
        expected.erase("required_passband_hz");actual.erase("required_passband_hz");
        if(expected!=actual || std::abs(readback.sample_rate_hz-d.exact->rate)>.5 || std::abs(readback.center_frequency_hz-d.exact->frequency)>.5)return false;
    }
    return true;
}
}
struct ReceiverSession::Impl {
    const ReceiverBackend& backend;Config identity;std::optional<ReceiverCapabilities> capabilities;
    mutable std::mutex mutex;std::condition_variable changed;std::deque<std::shared_ptr<Use>> pending;
    std::vector<std::shared_ptr<Use>> users;std::atomic_bool closing=false;
    std::shared_ptr<std::atomic<std::size_t>> reservations=std::make_shared<std::atomic<std::size_t>>(0);
    Json diagnostics=Json::object(),description=Json::object(),lastReceiverReport=Json::object();bool probeRequested=false;std::exception_ptr probeError;
    std::uint64_t captureGeneration=0,reportGeneration=0;
    Config current;std::optional<Config> nextConfiguration;ReceiverReadback readback;bool receiving=false,restart=false;
    std::jthread worker;
    Impl(const ReceiverBackend& b,Config c,std::optional<ReceiverCapabilities> caps):backend(b),identity(std::move(c)),capabilities(std::move(caps)) {if(capabilities)description={{"capabilities",*capabilities}};worker=std::jthread([this]{run();});}
    void ensureCapabilities() {
        if(capabilities)return;
        try {auto value=receiverProbe(backend,identity.device,identity.device_serial,identity.receiver_settings);capabilities=value.at("capabilities").get<ReceiverCapabilities>();
            {std::lock_guard lock(mutex);description=std::move(value);probeError=nullptr;}changed.notify_all();
        }catch(...){std::lock_guard lock(mutex);probeError=std::current_exception();changed.notify_all();throw;}
    }
    void status(const std::string& state,const std::string& detail={}) {
        ReceptionStatus value{state,detail};value.requested=requestedReceiverConfiguration(current);value.applied=readback.configuration;
        value.conditions=readback.conditions;
        value.generation=captureGeneration;
        if(receiving)value.stream=IqStreamParameters{static_cast<std::uint32_t>(std::llround(readback.sample_rate_hz)),readback.center_frequency_hz,backend.format};
        {std::lock_guard lock(mutex);diagnostics={{"state",state},{"detail",detail},{"users",users.size()},{"requested",value.requested},{"applied",value.applied},{"acquisition_conditions",value.conditions}};}
        for(const auto& use:users)publish(use,value);
    }
    bool plan(const ReceptionDemand& demand,Config& result) {
        if(!std::isfinite(demand.frequency_hz) || !std::isfinite(demand.passband_hz) || demand.frequency_hz<0 ||
           demand.frequency_hz>=std::ldexp(1.,64) || demand.passband_hz<=0 || demand.minimum_rate>demand.maximum_rate)return false;
        ensureCapabilities();
        auto base=demand.preferences.value_or(identity);base.seconds=0;base.frequency=static_cast<std::uint64_t>(std::round(demand.frequency_hz));base.required_passband_hz=demand.passband_hz;
        auto choices=demand.exact?std::vector<Config>{*demand.exact}:backend.recommend(*capabilities,base);
        std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.rate<b.rate;});
        for(auto cfg:choices) {
            cfg.seconds=0;cfg.required_passband_hz=std::max(cfg.required_passband_hz,demand.passband_hz);
            const auto p=backend.plan(*capabilities,cfg);
            if(!p.hardwareValid())continue;
            ReceiverReadback anticipated{double(cfg.rate),double(cfg.frequency),p.filter_bandwidth_hz};
            if(covers(demand,cfg,anticipated)){result=std::move(cfg);return true;}
        }return false;
    }
    void reject(const std::shared_ptr<Use>& use,const char* state,const std::string& detail={},Json diagnostic=Json::object()) {
        ReceptionStatus value{state,detail};value.diagnostic=std::move(diagnostic);value.generation=captureGeneration;publish(use,value);use->release();
    }
    // Only the capture worker mutates users/current/readback. Requests and
    // releases are observed at source heartbeats, never by a UI blocking wait.
    bool drain() {
        bool removed=false;
        std::erase_if(users,[&](const auto& use){if(use->active)return false;removed=true;return true;});
        std::deque<std::shared_ptr<Use>> incoming;
        {std::lock_guard lock(mutex);incoming.swap(pending);}
        for(auto& use:incoming) {
            if(!use->active)continue;
            if(restart){std::lock_guard lock(mutex);pending.push_back(std::move(use));continue;}
            if(users.empty()) {users.push_back(use);restart=true;continue;}
            if(covers(use->demand,current,readback)) {
                users.push_back(use);ReceptionStatus value{receiving?"receiving":"starting"};
                value.generation=captureGeneration;
                value.requested=requestedReceiverConfiguration(current);value.applied=readback.configuration;
                value.conditions=readback.conditions;
                if(receiving)value.stream=IqStreamParameters{static_cast<std::uint32_t>(std::llround(readback.sample_rate_hz)),readback.center_frequency_hz,backend.format};
                publish(use,value);
            }else if(use->demand.exact && std::none_of(users.begin(),users.end(),[](const auto& u){return u->demand.exact.has_value();})) {
                // Starting a wider, compatible exact task may upgrade an input
                // used only by minimum-demand consumers. Existing exact tasks
                // (including recording) are never reconfigured underneath them.
                try {Config upgraded;
                    if(plan(use->demand,upgraded)) {
                        const auto p=backend.plan(*capabilities,upgraded);const ReceiverReadback next{double(upgraded.rate),double(upgraded.frequency),p.filter_bandwidth_hz};
                        if(std::all_of(users.begin(),users.end(),[&](const auto& u){return covers(u->demand,upgraded,next);})) {users.insert(users.begin(),use);restart=true;continue;}
                    }
                    reject(use,"conflict");
                }catch(const std::exception& error){reject(use,"error",error.what());}
            }else reject(use,"conflict");
        }
        // An exact monitor lease pins its settings. Once it leaves, remaining
        // minimum-demand users may use a cheaper plugin-recommended input.
        if(removed && !users.empty() && std::none_of(users.begin(),users.end(),[](const auto& u){return u->demand.exact.has_value();})) {
            try {
                auto combined=users.front()->demand;double low=combined.frequency_hz-combined.passband_hz/2,high=combined.frequency_hz+combined.passband_hz/2;
                for(const auto& u:users){const auto& d=u->demand;low=std::min(low,d.frequency_hz-d.passband_hz/2);high=std::max(high,d.frequency_hz+d.passband_hz/2);combined.minimum_rate=std::max(combined.minimum_rate,d.minimum_rate);combined.maximum_rate=std::min(combined.maximum_rate,d.maximum_rate);}
                combined.frequency_hz=(low+high)/2;combined.passband_hz=high-low;
                Config smaller;if(plan(combined,smaller) && smaller.rate<current.rate) {
                const auto p=backend.plan(*capabilities,smaller);ReceiverReadback next{double(smaller.rate),double(smaller.frequency),p.filter_bandwidth_hz};
                if(std::all_of(users.begin(),users.end(),[&](const auto& u){return covers(u->demand,smaller,next);})) {nextConfiguration=smaller;restart=true;}
            }}catch(const std::exception& error){std::lock_guard lock(mutex);diagnostics["replan_error"]=error.what();}
        }
        {std::lock_guard lock(mutex);diagnostics["users"]=users.size();}
        return closing || restart || users.empty();
    }
    void run() noexcept {
        while(!closing) {
            try {
                if(users.empty()) {
                    nextConfiguration.reset();
                    receiving=false;status("idle");
                    std::unique_lock lock(mutex);changed.wait(lock,[&]{return closing || probeRequested || !pending.empty();});
                    if(closing)break;
                    if(probeRequested){probeRequested=false;lock.unlock();ensureCapabilities();continue;}
                    users.push_back(std::move(pending.front()));pending.pop_front();
                }
                std::erase_if(users,[](const auto& u){return !u->active;});if(users.empty()){nextConfiguration.reset();continue;}
                receiving=false;restart=false;
                if(nextConfiguration){current=std::move(*nextConfiguration);nextConfiguration.reset();}
                else if(!plan(users.front()->demand,current)) {for(auto& u:users)reject(u,"unsupported");users.clear();continue;}
                const auto p=backend.plan(*capabilities,current);readback={double(current.rate),double(current.frequency),p.filter_bandwidth_hz};status("starting");
                // Existing shared leases must remain compatible after replanning.
                for(auto& u:users)if(!covers(u->demand,current,readback))reject(u,"conflict");
                if(drain())continue;
                {std::lock_guard lock(mutex);++captureGeneration;}
                std::uint64_t next=0;
                const auto result=backend.stream(current,[&](auto bytes,auto first){
                    if(drain())return;
                    if(!receiving)throw std::runtime_error("receiver_readback_missing");
                    const auto stride=bytes_per_pair(backend.format);
                    if(bytes.empty() || bytes.size()%stride)throw std::runtime_error("receiver_block_invalid");
                    const bool gap=first!=next;next=first+bytes.size()/stride;
                    const IqBlockView block{bytes,first,{static_cast<std::uint32_t>(std::llround(readback.sample_rate_hz)),readback.center_frequency_hz,backend.format},gap};
                    for(auto& u:users)if(u->active && u->consumer.offer)try{u->consumer.offer(block);}
                        catch(const ReceptionError& e){reject(u,"consumer_error",e.what(),e.status.diagnostic);}
                        catch(const SourceError& e){reject(u,"consumer_error",e.what(),e.details);}
                        catch(const std::exception& e){reject(u,"consumer_error",e.what());}catch(...){reject(u,"consumer_error");}
                },[&]{return drain();},[&](const auto& actual){
                    if(receiving)throw std::runtime_error("receiver_readback_repeated");
                    readback=actual;receiving=true;
                    readback.configuration["sample_rate_hz"]=readback.sample_rate_hz;readback.configuration["frequency_hz"]=readback.center_frequency_hz;readback.configuration["filter_bandwidth_hz"]=readback.filter_bandwidth_hz;
                    for(auto& u:users)if(!covers(u->demand,current,readback))reject(u,"readback_conflict");
                    status("receiving");
                });
                {std::lock_guard lock(mutex);lastReceiverReport=result;reportGeneration=captureGeneration;}changed.notify_all();
                receiving=false;
                if(!restart && !closing && !users.empty()) {for(auto& u:users)reject(u,"source_ended");users.clear();}
            }catch(const SourceError& error){receiving=false;{std::lock_guard lock(mutex);lastReceiverReport=error.details;reportGeneration=captureGeneration;}changed.notify_all();for(auto& u:users)reject(u,"error",error.what(),error.details);users.clear();}
            catch(const std::exception& error){receiving=false;for(auto& u:users)reject(u,"error",error.what());users.clear();}
            catch(...){receiving=false;for(auto& u:users)reject(u,"error");users.clear();}
        }
        for(auto& u:users)reject(u,"disconnected");
        std::deque<std::shared_ptr<Use>> remaining;{std::lock_guard lock(mutex);remaining.swap(pending);diagnostics["state"]="disconnected";}
        for(auto& u:remaining)reject(u,"disconnected");
    }
};
ReceiverSession::ReceiverSession(const ReceiverBackend& b,Config c,std::optional<ReceiverCapabilities> caps):impl_(std::make_unique<Impl>(b,std::move(c),std::move(caps))){}
ReceiverSession::~ReceiverSession(){disconnect();}
void ReceiverSession::disconnect(){impl_->closing=true;impl_->changed.notify_all();if(impl_->worker.joinable())impl_->worker.join();}
std::shared_ptr<ReceptionLease> ReceiverSession::acquire(ReceptionDemand demand,ReceptionConsumer consumer) {
    auto use=std::make_shared<Use>();use->demand=std::move(demand);use->consumer=std::move(consumer);
    auto handle=std::make_shared<Handle>(use);
    {std::lock_guard lock(impl_->mutex);if(impl_->closing)throw std::runtime_error("disconnected");++*impl_->reservations;use->reservations=impl_->reservations;impl_->pending.push_back(use);}impl_->changed.notify_one();
    return handle;
}
Json ReceiverSession::snapshot() const {
    std::lock_guard lock(impl_->mutex);auto result=impl_->diagnostics;const auto count=impl_->reservations->load();const auto state=result.value("state",std::string("idle"));result["users"]=count;
    result["capture_generation"]=impl_->captureGeneration;result["driver_report_generation"]=impl_->reportGeneration;result["receiver"]=impl_->lastReceiverReport;
    if(count && (state=="idle" || state.empty()))result["state"]="starting";
    else if(!count && state=="receiving")result["state"]="stopping";
    const auto facts=impl_->description.value("capabilities",Json::object());
    result["identity"]={{"backend",impl_->backend.id?impl_->backend.id:impl_->backend.source},{"device",impl_->identity.device},{"serial",impl_->identity.device_serial},{"model",facts.value("model",std::string(impl_->backend.display_name?impl_->backend.display_name:""))}};return result;
}
Json ReceiverSession::describe() {
    std::unique_lock lock(impl_->mutex);impl_->probeRequested=true;impl_->changed.notify_all();
    impl_->changed.wait(lock,[&]{return impl_->closing || impl_->probeError || !impl_->description.empty();});
    if(impl_->probeError)std::rethrow_exception(impl_->probeError);
    if(impl_->description.empty())throw std::runtime_error("disconnected");
    return impl_->description;
}
Json ReceiverSession::stream(const Config& cfg,const BlockSink& sink,const std::function<bool()>& cancelled,const ReceiverReady& ready) {
    struct Queue {std::mutex mutex;std::condition_variable changed;std::deque<std::pair<Bytes,std::uint64_t>> blocks;std::size_t bytes=0,peak=0;std::uint64_t input=0,rejected=0;ReceptionStatus status;bool notified=false;};
    auto q=std::make_shared<Queue>();const auto budget=std::max<std::size_t>(cfg.block_bytes,cfg.analysis_queue_bytes);
    ReceptionDemand demand{double(cfg.frequency),cfg.required_passband_hz>0?cfg.required_passband_hz:double(cfg.rate),cfg.rate,cfg.rate,cfg};
    auto lease=acquire(demand,{[q](const auto& value){std::lock_guard lock(q->mutex);q->status=value;q->notified=true;q->changed.notify_one();},
        [q,budget](const auto& block){std::lock_guard lock(q->mutex);const auto pairs=block.bytes.size()/bytes_per_pair(block.stream.format);q->input+=pairs;
            if(block.bytes.size()>budget-q->bytes){q->rejected+=pairs;ReceptionStatus failure{"consumer_error","monitor_input_queue_full"};failure.diagnostic={{"message_key","reception.monitor_queue_full"},{"stage","monitor_input"},{"input_pairs",q->input},{"rejected_pairs",q->rejected},{"queue_capacity_bytes",budget},{"queue_high_water_bytes",q->peak},{"driver_report_final",false}};throw ReceptionError(std::move(failure));}
            q->blocks.emplace_back(Bytes(block.bytes.begin(),block.bytes.end()),block.first_sample);q->bytes+=block.bytes.size();q->peak=std::max(q->peak,q->bytes);q->changed.notify_one();}});
    std::optional<std::uint64_t> first;std::uint64_t next=0,generation=0;const auto stride=bytes_per_pair(impl_->backend.format);bool announced=false;
    while(!cancelled()) {
        std::unique_lock lock(q->mutex);q->changed.wait_for(lock,std::chrono::milliseconds(20),[&]{return q->notified || !q->blocks.empty();});
        const auto status=q->status;q->notified=false;
        if(status.state!="starting" && status.state!="receiving") {if(status.state=="error" && !status.diagnostic.empty())throw SourceError(status.detail,status.diagnostic);throw ReceptionError(status);}
        if(!announced && status.stream) {announced=true;generation=status.generation;lock.unlock();if(ready)ready({double(status.stream->sample_rate),status.stream->center_frequency_hz,status.applied.value("filter_bandwidth_hz",0.),status.applied,status.conditions});lock.lock();}
        if(q->blocks.empty())continue;
        auto block=std::move(q->blocks.front());q->blocks.pop_front();q->bytes-=block.first.size();lock.unlock();
        if(!first)first=block.second;
        if(block.second-*first!=next)throw std::runtime_error("monitor_input_discontinuous");
        for(std::size_t at=0;at<block.first.size();at+=cfg.block_bytes){const auto size=std::min(cfg.block_bytes,block.first.size()-at);sink(std::span(block.first).subspan(at,size),next);next+=size/stride;}
    }
    lease->release();
    // A shared input may continue after monitoring stops. Its final driver
    // counters do not yet exist; never substitute zero or another generation.
    if(impl_->reservations->load()==0) {std::unique_lock lock(impl_->mutex);impl_->changed.wait_for(lock,std::chrono::milliseconds(500),[&]{return impl_->closing || impl_->reportGeneration==generation;});}
    const auto source=snapshot();const bool final=generation && source.value("driver_report_generation",0ULL)==generation;
    auto result=final?source.value("receiver",Json::object()):Json::object();result["shared_reception"]=true;result["reception_generation"]=generation;result["consumer_input_pairs"]=next;result["driver_report_final"]=final;{std::lock_guard lock(q->mutex);result["input_queue"]={{"capacity_bytes",budget},{"high_water_bytes",q->peak},{"input_pairs",q->input},{"rejected_pairs",q->rejected}};}return result;
}
}
