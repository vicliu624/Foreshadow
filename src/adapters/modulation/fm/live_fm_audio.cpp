#include "signalscope/adapters/modulation/fm/live_fm_audio.hpp"
#include "signalscope/backend/dsp/analysis_priority.hpp"
#include <array>
#include <deque>
#include <thread>

namespace signalscope {
struct LiveFmAudio::Impl {
    static constexpr std::size_t pcmLimit=fm_audio_rate/2;
    struct Boundary {std::uint64_t position=0,first=0;};
    // SPSC payload ring. Only discontinuities need metadata, so short reads
    // retain the same byte budget. Release/acquire cursors protect both rings;
    // DSP never takes the capture/control mutex, including while copying.
    Bytes input;
    std::array<Boundary,128> boundaries{};
    std::atomic<std::uint64_t> writeCursor=0,readCursor=0,boundaryWrite=0,boundaryRead=0,wake=0;
    std::uint64_t nextOffer=0;bool offeredBefore=false;
    std::size_t maxBlockBytes=0;
    std::mutex inputMutex,pcmMutex,signalMutex,errorMutex;
    std::deque<std::int16_t> pcm;
    FmSignalMetrics signal;
    std::string error;
    std::atomic<bool> active=false,failed=false;
    std::atomic<std::size_t> stride=1;
    std::atomic<std::uint64_t> dropped=0,gaps=0,offered=0,processed=0,droppedPairs=0;
    std::atomic<std::uint64_t> capacity=0,highWater=0,pcmFrames=0,pcmDiscarded=0;
    std::jthread worker;

    void run(const FmAudioConfig& config,std::unique_ptr<FmDemodulator> decoder,std::size_t blockBytes) {
        lowerAnalysisPriority();
        try {
            Bytes block(blockBytes);std::uint64_t expected=0,cursorSample=0;bool havePrevious=false;
            for(;;) {
                const auto epoch=wake.load(std::memory_order_acquire);
                if(!active)return;
                const auto head=readCursor.load(std::memory_order_relaxed);
                const auto end=writeCursor.load(std::memory_order_acquire);
                if(head==end){wake.wait(epoch,std::memory_order_acquire);continue;}
                auto boundary=boundaryRead.load(std::memory_order_relaxed);
                const auto published=boundaryWrite.load(std::memory_order_acquire);
                if(boundary!=published && boundaries[boundary%boundaries.size()].position==head) {
                    cursorSample=boundaries[boundary%boundaries.size()].first;
                    boundaryRead.store(++boundary,std::memory_order_release);
                }
                auto size=std::min<std::uint64_t>(blockBytes,end-head);
                if(boundary!=published)size=std::min(size,boundaries[boundary%boundaries.size()].position-head);
                const auto offset=head%input.size(),leading=std::min<std::uint64_t>(size,input.size()-offset);
                std::copy_n(input.begin()+offset,leading,block.begin());
                std::copy_n(input.begin(),size-leading,block.begin()+leading);
                readCursor.store(head+size,std::memory_order_release);
                const auto first=cursorSample;cursorSample+=size/stride;
                if(havePrevious && first!=expected) {
                    ++gaps;decoder=std::make_unique<FmDemodulator>(config);
                    std::lock_guard lock(pcmMutex);pcmDiscarded+=pcm.size();pcm.clear();pcmFrames=0;
                }
                expected=first+size/stride;havePrevious=true;
                decoder->feed(std::span(block).first(size),first,[&](auto audio){
                    std::lock_guard lock(pcmMutex);
                    if(audio.size()>pcmLimit){pcmDiscarded+=audio.size()-pcmLimit;audio=audio.last(pcmLimit);}
                    while(pcm.size()+audio.size()>pcmLimit){pcm.pop_front();++pcmDiscarded;}
                    pcm.insert(pcm.end(),audio.begin(),audio.end());pcmFrames=pcm.size();
                });
                processed+=size/stride;
                const auto latest=decoder->signalMetrics();
                {std::lock_guard lock(signalMutex);signal=latest;}
            }
        } catch(const std::exception& e) {
            {std::lock_guard lock(errorMutex);error=e.what();}failed=true;active=false;
        } catch(...) {
            {std::lock_guard lock(errorMutex);error="unknown_exception";}failed=true;active=false;
        }
    }
    void reject(std::size_t bytes) {++dropped;droppedPairs+=bytes/stride;}
};

LiveFmAudio::LiveFmAudio():impl_(std::make_unique<Impl>()){}
LiveFmAudio::~LiveFmAudio(){stop();}

void LiveFmAudio::start(const FmAudioConfig& config,std::size_t maximumBlockBytes) {
    stop();const auto stride=bytes_per_pair(config.format);
    if(!maximumBlockBytes || maximumBlockBytes>fm_live_maximum_block_bytes || maximumBlockBytes%stride)
        throw std::invalid_argument("Live FM input block exceeds the supported limit or contains an incomplete I/Q pair");
    auto decoder=std::make_unique<FmDemodulator>(config);auto& state=*impl_;
    state.stride=stride;state.maxBlockBytes=maximumBlockBytes;
    const auto budget=std::max<std::size_t>(maximumBlockBytes,std::min<std::size_t>(8*1024*1024,std::uint64_t(config.input_rate)*stride/4));
    state.input.resize(budget/stride*stride);state.capacity=state.input.size();
    state.failed=false;state.dropped=0;state.gaps=0;state.offered=0;state.processed=0;state.droppedPairs=0;
    state.writeCursor=0;state.readCursor=0;state.boundaryWrite=0;state.boundaryRead=0;
    state.nextOffer=0;state.offeredBefore=false;
    state.highWater=0;state.pcmFrames=0;state.pcmDiscarded=0;
    {std::lock_guard lock(state.signalMutex);state.signal={};}
    {std::lock_guard lock(state.errorMutex);state.error.clear();}
    state.active=true;
    const auto quantum=std::min<std::size_t>(maximumBlockBytes,65536/stride*stride);
    try {state.worker=std::jthread([&state,config,quantum,decoder=std::move(decoder)]() mutable {
        state.run(config,std::move(decoder),quantum);
    });}catch(...){state.active=false;throw;}
}

void LiveFmAudio::stop() {
    auto& state=*impl_;
    {std::lock_guard lock(state.inputMutex);state.active=false;}
    ++state.wake;state.wake.notify_one();if(state.worker.joinable())state.worker.join();
    state.readCursor=state.writeCursor.load();state.boundaryRead=state.boundaryWrite.load();
    std::lock_guard lock(state.pcmMutex);state.pcm.clear();state.pcmFrames=0;
}

void LiveFmAudio::offer(std::span<const std::uint8_t> bytes,std::uint64_t first) noexcept {
    auto& state=*impl_;if(!state.active || bytes.empty())return;
    state.offered+=bytes.size()/state.stride;
    try {
        std::unique_lock lock(state.inputMutex,std::try_to_lock);
        if(!lock.owns_lock()){state.reject(bytes.size());return;}
        if(!state.active)return;
        const auto head=state.readCursor.load(std::memory_order_acquire),end=state.writeCursor.load(std::memory_order_relaxed);
        if(bytes.size()%state.stride || bytes.size()>state.maxBlockBytes || bytes.size()>state.input.size()-(end-head)){state.reject(bytes.size());return;}
        const bool boundary=!state.offeredBefore || state.nextOffer!=first;
        const auto published=state.boundaryWrite.load(std::memory_order_relaxed);
        if(boundary && published-state.boundaryRead.load(std::memory_order_acquire)==state.boundaries.size()){state.reject(bytes.size());return;}
        const auto tail=static_cast<std::size_t>(end%state.input.size()),leading=std::min(bytes.size(),state.input.size()-tail);
        std::copy_n(bytes.begin(),leading,state.input.begin()+tail);
        std::copy(bytes.begin()+leading,bytes.end(),state.input.begin());
        if(boundary) {
            state.boundaries[published%state.boundaries.size()]={end,first};
            state.boundaryWrite.store(published+1,std::memory_order_release);
        }
        state.nextOffer=first+bytes.size()/state.stride;state.offeredBefore=true;
        state.writeCursor.store(end+bytes.size(),std::memory_order_release);
        if(end+bytes.size()-head>state.highWater)state.highWater=end+bytes.size()-head;
    }catch(...){state.reject(bytes.size());return;}
    ++state.wake;state.wake.notify_one();
}

std::size_t LiveFmAudio::read(std::span<std::int16_t> destination) {
    auto& state=*impl_;std::lock_guard lock(state.pcmMutex);
    const auto count=std::min(state.pcm.size(),destination.size());std::copy_n(state.pcm.begin(),count,destination.begin());
    state.pcm.erase(state.pcm.begin(),state.pcm.begin()+count);state.pcmFrames=state.pcm.size();return count;
}

LiveFmAudioStatus LiveFmAudio::status() const {
    auto& s=*impl_;LiveFmAudioStatus result;
    result.dropped_blocks=s.dropped;result.discontinuities=s.gaps;result.failed=s.failed;
    result.input_pairs=s.offered;result.processed_pairs=s.processed;result.dropped_pairs=s.droppedPairs;
    const auto read=s.readCursor.load(std::memory_order_acquire);result.queue_bytes=s.writeCursor.load(std::memory_order_acquire)-read;
    result.queue_capacity_bytes=s.capacity;result.queue_high_water_bytes=s.highWater;
    result.pcm_frames=s.pcmFrames;result.pcm_discarded_frames=s.pcmDiscarded;
    {std::lock_guard lock(s.signalMutex);result.signal=s.signal;}
    if(result.failed){std::lock_guard lock(s.errorMutex);result.error_detail=s.error;}
    return result;
}
}
