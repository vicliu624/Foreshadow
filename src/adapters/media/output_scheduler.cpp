#include "output_scheduler.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace signalscope::media {
class OutputScheduler::Impl {
public:
    using Clock=std::chrono::steady_clock;
    struct Entry {Bytes wire;Clock::time_point due,ready;};
    struct Lane {
        std::deque<Entry> queue;
        std::size_t held=0,peak=0,budget=0;
        std::uint64_t submitted=0,written=0;
        std::int64_t maximumLatenessUs=0,maximumWriteUs=0,maximumWriteGapUs=0;
        std::optional<Clock::time_point> lastWritten;
        std::optional<std::int64_t> lastPts;
        std::optional<Clock::time_point> lastDue;
    };
    Impl(bool enabled,Sink writer,Limits limits):realtime(enabled),sink(std::move(writer)),options(limits) {
        if(!sink || !limits.video_bytes || !limits.audio_bytes || !limits.control_bytes || limits.ahead_us<0 || limits.ahead_us>2000000)
            throw std::invalid_argument("media.output_budget");
        lanes[0].budget=limits.control_bytes;lanes[1].budget=limits.video_bytes;lanes[2].budget=limits.audio_bytes;
        if(realtime)worker=std::jthread([this]{run();});
    }
    ~Impl(){cancel();}
    static unsigned laneFor(MessageType type) {return type==MessageType::video?1:type==MessageType::audio?2:0;}
    void checkpoint() const {
        if(failure)std::rethrow_exception(failure);
        if(stopped || closing)throw OutputFailure("media.output_finished");
    }
    bool empty() const {return lanes[0].queue.empty() && lanes[1].queue.empty() && lanes[2].queue.empty() && !inFlight;}
    void discard(unsigned index,bool clockReset) {
        auto& lane=lanes[index];
        for(const auto& entry:lane.queue) {
            lane.held-=entry.wire.size();
            if(clockReset){++clockDiscarded[index];clockDiscardedBytes+=entry.wire.size();}
            else ++cancelledMessages;
        }
        lane.queue.clear();lane.lastPts.reset();lane.lastDue.reset();
    }
    bool submit(Message message) {
        const auto index=laneFor(message.type);const auto pts=message.pts_us;
        auto wire=encode(message);
        // The encoded representation is the actual retained payload, including
        // JSON and framing. The producer holds at most this one extra message.
        message={MessageType::status};
        std::unique_lock lock(mutex);checkpoint();auto& lane=lanes[index];
        if(wire.size()>lane.budget)throw OutputFailure("media.output_budget");
        if(!realtime) {
            lane.peak=std::max(lane.peak,wire.size());++lane.submitted;
            lock.unlock();sink(wire);lock.lock();++lane.written;return false;
        }
        bool reset=false;
        const auto now=Clock::now();auto due=now;
        if(index && pts!=std::numeric_limits<std::int64_t>::min()) {
            const auto previous=lane.lastPts?lane.lastPts:anchor;
            if(previous) {
                const auto delta=static_cast<long double>(pts)-*previous;
                if(delta< -2000000 || delta>10000000) {
                    discard(1,true);discard(2,true);anchor.reset();++clockResets;reset=true;
                }
            }
            if(!anchor){anchor=pts;wall=now;}
            const auto distance=static_cast<long double>(pts)-*anchor-options.ahead_us;
            // Bound an individual wait even for malformed, near-limit PTS.
            // Large jumps were reanchored above; ordinary long streams retain
            // their full monotonic timeline instead of resetting every 10 s.
            if(distance<std::numeric_limits<std::int64_t>::min() || distance>std::numeric_limits<std::int64_t>::max())
                throw OutputFailure("media.output_timestamp");
            due=wall+std::chrono::microseconds(static_cast<std::int64_t>(distance));lane.lastPts=pts;
        }
        if(index && lane.lastDue)due=std::max(due,*lane.lastDue);
        if(lane.held>lane.budget-wire.size())++producerWaits;
        changed.wait(lock,[&]{return failure || stopped || closing || lane.held<=lane.budget-wire.size();});checkpoint();
        lane.held+=wire.size();lane.peak=std::max(lane.peak,lane.held);++lane.submitted;
        lane.lastDue=due;lane.queue.push_back({std::move(wire),due,Clock::now()});changed.notify_all();return reset;
    }
    void run() noexcept {
        std::unique_lock lock(mutex);
#ifdef __APPLE__
        // This thread supplies interactive playback, including while the
        // decoder waits on live input. Do not inherit a batch parent's QoS.
        qosRequested=true;qosError=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
#endif
        while(!stopped) {
            unsigned index=0;
            if(lanes[0].queue.empty()) {
                if(lanes[1].queue.empty())index=2;
                else if(lanes[2].queue.empty())index=1;
                else index=lanes[1].queue.front().due<lanes[2].queue.front().due?1:2;
            }
            auto& lane=lanes[index];
            if(lane.queue.empty()) {
                if(closing)break;
                changed.wait(lock);continue;
            }
            const auto due=lane.queue.front().due;
            if(index && due>Clock::now()){changed.wait_until(lock,due);continue;}
            auto entry=std::move(lane.queue.front());lane.queue.pop_front();const auto size=entry.wire.size();inFlight=true;
            const auto started=Clock::now();
            if(index)lane.maximumLatenessUs=std::max(lane.maximumLatenessUs,
                std::chrono::duration_cast<std::chrono::microseconds>(started-std::max(due,entry.ready)).count());
            lock.unlock();std::exception_ptr error;
            try{sink(entry.wire);}catch(...){error=std::current_exception();}
            const auto written=Clock::now();
            Bytes{}.swap(entry.wire);lock.lock();lane.held-=size;inFlight=false;
            lane.maximumWriteUs=std::max(lane.maximumWriteUs,
                std::chrono::duration_cast<std::chrono::microseconds>(written-started).count());
            if(error){failure=error;stopped=true;}else {
                ++lane.written;
                if(lane.lastWritten)lane.maximumWriteGapUs=std::max(lane.maximumWriteGapUs,
                    std::chrono::duration_cast<std::chrono::microseconds>(written-*lane.lastWritten).count());
                lane.lastWritten=written;
            }
            changed.notify_all();
        }
        finished=true;changed.notify_all();
    }
    void drain() {
        std::unique_lock lock(mutex);if(failure)std::rethrow_exception(failure);
        if(!realtime)return;
        changed.wait(lock,[&]{return empty() || failure || stopped;});
        if(failure)std::rethrow_exception(failure);
        if(stopped)throw OutputFailure("media.output_finished");
    }
    void finish() {
        {
            std::unique_lock lock(mutex);if(failure)std::rethrow_exception(failure);
            if(stopped)throw OutputFailure("media.output_finished");
            closing=true;changed.notify_all();
            if(realtime)changed.wait(lock,[&]{return finished;});
            if(failure)std::rethrow_exception(failure);
        }
        if(worker.joinable())worker.join();
    }
    void cancel() noexcept {
        {
            std::lock_guard lock(mutex);stopped=true;
            for(unsigned index=0;index<3;++index)discard(index,false);
            changed.notify_all();
        }
        if(worker.joinable())worker.join();
    }
    Json snapshot() const {
        std::lock_guard lock(mutex);Json result={{"realtime",realtime},{"ahead_us",options.ahead_us},
            {"producer_waits",producerWaits},{"clock_resets",clockResets},{"clock_discarded_video_frames",clockDiscarded[1]},
            {"clock_discarded_audio_messages",clockDiscarded[2]},{"clock_discarded_wire_bytes",clockDiscardedBytes},
            {"cancelled_messages",cancelledMessages},{"writer_active",inFlight}};
        result["interactive_qos_requested"]=qosRequested;result["interactive_qos_error"]=qosError;
        for(unsigned index=0;index<3;++index) {
            const auto& lane=lanes[index];result[index==0?"control":index==1?"video":"audio"]={
                {"held_bytes",lane.held},{"peak_bytes",lane.peak},{"budget_bytes",lane.budget},
                {"submitted_messages",lane.submitted},{"written_messages",lane.written},
                {"maximum_deadline_lateness_us",lane.maximumLatenessUs},{"maximum_write_duration_us",lane.maximumWriteUs},
                {"maximum_write_gap_us",lane.maximumWriteGapUs}};
        }
        return result;
    }
    bool realtime;Sink sink;Limits options;mutable std::mutex mutex;std::condition_variable changed;
    std::array<Lane,3> lanes;std::optional<std::int64_t> anchor;Clock::time_point wall;
    std::uint64_t producerWaits=0,clockResets=0,clockDiscardedBytes=0,cancelledMessages=0;
    std::array<std::uint64_t,3> clockDiscarded{};
    bool stopped=false,closing=false,finished=false,inFlight=false;std::exception_ptr failure;std::jthread worker;
    bool qosRequested=false;int qosError=0;
};
OutputScheduler::OutputScheduler(bool realtime,Sink sink):OutputScheduler(realtime,std::move(sink),Limits{}){}
OutputScheduler::OutputScheduler(bool realtime,Sink sink,Limits limits):impl_(std::make_unique<Impl>(realtime,std::move(sink),limits)){}
OutputScheduler::~OutputScheduler()=default;
bool OutputScheduler::submit(Message message){return impl_->submit(std::move(message));}
void OutputScheduler::drain(){impl_->drain();}
void OutputScheduler::finish(){impl_->finish();}
void OutputScheduler::cancel() noexcept {impl_->cancel();}
Json OutputScheduler::snapshot() const{return impl_->snapshot();}
}
