#include "src/adapters/media/output_scheduler.hpp"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

using namespace signalscope;
using namespace signalscope::media;
using namespace std::chrono_literals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Message audio(std::int64_t pts){return {MessageType::audio,pts,Json::object(),Bytes(32,7)};}
Message video(std::int64_t pts){return {MessageType::video,pts,Json::object(),Bytes(80,9)};}
class Capture {
public:
    void write(std::span<const std::uint8_t> wire) {
        MessageParser parser([&](Message message){std::lock_guard lock(mutex);rows.emplace_back(message.type,message.pts_us);changed.notify_all();});
        parser.feed(wire);parser.finish();
    }
    bool await(MessageType type,std::int64_t pts,std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);return changed.wait_for(lock,timeout,[&]{return std::find(rows.begin(),rows.end(),std::pair(type,pts))!=rows.end();});
    }
    bool has(MessageType type,std::int64_t pts){return await(type,pts,0ms);}
    std::mutex mutex;std::condition_variable changed;std::vector<std::pair<MessageType,std::int64_t>> rows;
};
}
int main() {
    try {
        {
            Capture output;OutputScheduler scheduler(true,[&](auto bytes){output.write(bytes);});
            scheduler.submit(audio(0));require(output.await(MessageType::audio,0,200ms),"Initial audio was not emitted");
            const auto start=std::chrono::steady_clock::now();scheduler.submit(video(800000));scheduler.submit(audio(24000));scheduler.submit(audio(48000));
            require(output.await(MessageType::audio,48000,150ms),"An outstanding future video blocked earlier audio");
            require(!output.has(MessageType::video,800000),"A future frame bypassed output pacing");
            scheduler.cancel();require(std::chrono::steady_clock::now()-start<300ms,"Cancel waited for a future video deadline");
        }
        {
            Capture output;OutputScheduler scheduler(true,[&](auto bytes){output.write(bytes);});
            scheduler.submit(audio(0));scheduler.submit(video(350000));
            // No further producer calls, like a demuxer waiting on live input.
            require(output.await(MessageType::video,350000,700ms),"Output depended on the next demux/input call");
            scheduler.drain();scheduler.submit({MessageType::ended});scheduler.finish();
            require(output.rows.back().first==MessageType::ended,"EOF overtook queued media");
        }
        {
            std::promise<void> release;auto gate=release.get_future().share();std::atomic_bool entered=false;
            OutputScheduler::Limits limits;limits.video_bytes=256;limits.audio_bytes=256;limits.control_bytes=256;limits.ahead_us=0;
            OutputScheduler scheduler(true,[&](auto){entered=true;gate.wait();},limits);
            scheduler.submit(video(0));
            auto producer=std::async(std::launch::async,[&]{scheduler.submit(video(0));scheduler.submit(video(0));});
            const auto blocked=producer.wait_for(60ms)==std::future_status::timeout;const auto held=scheduler.snapshot();
            release.set_value();producer.get();scheduler.finish();
            require(entered && blocked,"A full byte budget did not apply backpressure to its producer");
            require(held.at("video").at("held_bytes")==encode(video(0)).size()*2 && held.at("video").at("peak_bytes")<=256,
                "Budget accounting excluded the in-flight encoded message or used callback counts");
            require(scheduler.snapshot().at("video").at("held_bytes")==0,"Draining retained encoded media bytes");
        }
        {
            OutputScheduler scheduler(true,[](auto){throw OutputFailure("fixture writer failure");});scheduler.submit(audio(0));bool failed=false;
            try{scheduler.drain();}catch(const OutputFailure& error){failed=std::string(error.what())=="fixture writer failure";}
            require(failed,"Writer failure was lost or deadlocked the decoder producer");
        }
        {
            OutputScheduler::Limits limits;limits.video_bytes=80;
            OutputScheduler scheduler(true,[](auto){},limits);bool rejected=false;
            try{scheduler.submit(video(0));}catch(const OutputFailure&){rejected=true;}
            require(rejected && scheduler.snapshot().at("video").at("held_bytes")==0,"An oversized encoded message entered the bounded queue");
        }
        {
            Capture output;OutputScheduler scheduler(true,[&](auto bytes){output.write(bytes);});
            scheduler.submit(audio(0));require(output.await(MessageType::audio,0,200ms),"Clock-reset setup did not emit audio");
            scheduler.submit(video(9000000));const auto reset=scheduler.submit(audio(-3000000));scheduler.drain();
            const auto state=scheduler.snapshot();require(reset && state.at("clock_resets")==1 && state.at("clock_discarded_video_frames")==1 &&
                !output.has(MessageType::video,9000000) && output.has(MessageType::audio,-3000000),
                "A clock discontinuity retained stale media or lost its discard accounting");scheduler.finish();
        }
        {
            Capture output;OutputScheduler scheduler(false,[&](auto bytes){output.write(bytes);});
            scheduler.submit(video(400000));scheduler.submit(audio(0));scheduler.submit(audio(std::numeric_limits<std::int64_t>::min()));
            scheduler.finish();require(output.rows.size()==3 && output.rows[0].first==MessageType::video,"Unpaced output changed decoder order or rejected unknown timestamps");
        }
        std::cout<<"Independent output deadlines, audio/video scheduling, live-input stalls, byte budgets, cancellation, clock jumps and writer failures passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
