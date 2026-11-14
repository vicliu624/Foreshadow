#include "signalscope/backend/iq/signal_capture.hpp"
#include "signalscope/backend/dsp/waveform.hpp"
#include <chrono>
#include <iostream>
#include <numbers>
#include <limits>
#include <thread>
#include <charconv>

using namespace signalscope;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void until(F action){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!action()){if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Signal capture did not reach its observable state");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
Bytes carrier(SampleFormat format,std::size_t count,std::uint64_t offset=0) {
    const auto stride=bytes_per_pair(format);Bytes bytes(count*stride);
    for(std::size_t n=0;n<count;++n){const auto value=std::polar(.56,.31+2*std::numbers::pi*((offset+n)%64)/64.);write_iq_pair(bytes.data()+n*stride,format,{float(value.real()),float(value.imag())});}return bytes;
}
void accepted(SignalCapture& capture,const Bytes& bytes,std::uint64_t first,IqStreamParameters stream,bool gap=false){until([&]{return capture.offer({bytes,first,stream,gap});});}
std::shared_ptr<const SignalCaptureSnapshot> freeze(SignalCapture& capture){const auto id=capture.requestSnapshot();std::shared_ptr<const SignalCaptureSnapshot> result;until([&]{result=capture.takeSnapshot();return bool(result);});require(result->request==id && result->window && result->error.empty(),"An accepted source window could not be frozen");return result;}
void sourceTruthAndBarrier(SampleFormat format) {
    SignalCaptureBudget budget;budget.retention.payload_bytes=32000;budget.retention.maximum_segments=64;SignalCapture capture(budget);
    const IqStreamParameters stream{1000000,433920000,format};constexpr auto origin=(1ULL<<54)+321;
    capture.start(stream,"independent.truth",{{"requested",{{"gain","manual"}}},{"applied",{{"calibrated",false}}}});
    Bytes all;
    for(std::size_t first=0;first<1024;first+=37){const auto count=std::min<std::size_t>(37,1024-first);const auto bytes=carrier(format,count,first);all.insert(all.end(),bytes.begin(),bytes.end());accepted(capture,bytes,origin+first,stream);}
    const auto request=capture.requestSnapshot();const auto later=carrier(format,256,1024);accepted(capture,later,origin+1024,stream);
    std::shared_ptr<const SignalCaptureSnapshot> frozen;until([&]{frozen=capture.takeSnapshot();return bool(frozen);});
    require(frozen->request==request && frozen->window->first_sample()==origin && frozen->window->end_sample()==origin+1024,"Freeze included samples accepted after its FIFO barrier or rounded their origin");
    require(frozen->window->sample_count()==1024 && frozen->window->processing[0].at("receiver_context").at("applied").at("calibrated")==false,"Capture lost sample data or hardware provenance");
    const auto analysis=analyze_waveform(frozen->window);const auto tolerance=format==SampleFormat::cu8?1900.:format==SampleFormat::ci16_le?7.:.1;
    for(std::size_t n=1;n<1024;++n)require(std::abs(analysis.trace("frequency").values[n]-15625)<tolerance,"Captured samples disagreed with the independent carrier truth");
    std::size_t at=0;const auto stride=bytes_per_pair(format);
    for(const auto& segment:frozen->window->segments)for(const auto sample:segment.samples){require(sample==read_iq_pair(all.data()+at*stride,format),"The capture retained borrowed memory or changed codec normalization");++at;}
    const auto stable=frozen->window->segments.front().samples.front();capture.stop();require(frozen->window->segments.front().samples.front()==stable,"Stopping input mutated a frozen source");
    capture.start({500000,435000000,format},"independent.new-epoch");accepted(capture,carrier(format,64),17,{500000,435000000,format});const auto second=freeze(capture);
    require(second->generation!=frozen->generation && second->window->first_sample()==17 && second->window->stream.sample_rate==500000,"A new source generation mixed prior samples or coordinates");
}
void overloadAndFragments() {
    SignalCaptureBudget budget;budget.queue_payload_bytes=4096;budget.maximum_block_bytes=1024;budget.maximum_queued_blocks=4;budget.retention={8000,1,4};SignalCapture capture(budget);
    const IqStreamParameters stream{1000000,433000000,SampleFormat::cf32_le};capture.start(stream,"independent.overload");
    accepted(capture,carrier(stream.format,64),0,stream);until([&]{return capture.status().processed_samples==64;});
    const auto oversized=carrier(stream.format,200);require(!capture.offer({oversized,64,stream,false}),"An oversized raw block bypassed the queue budget");
    accepted(capture,carrier(stream.format,64,264),264,stream);auto snap=freeze(capture);
    require(snap->status.oversized_blocks==1 && snap->status.rejected_samples>=200 && snap->status.window.known_missing_samples==200,"Input rejection and the corresponding source gap were not separately accounted");
    auto analysis=analyze_waveform(snap->window);require(std::isnan(analysis.trace("frequency").values[64]),"Live analysis differentiated across rejected input");
    for(std::uint64_t n=328;n<348;++n){accepted(capture,carrier(stream.format,1,n),n,stream);until([&]{return capture.status().processed_samples==128+n-327;});}
    snap=freeze(capture);require(snap->window->segments.size()<=4 && snap->status.window.expired_samples>=128,"Tiny short reads grew unbounded fragment metadata or were counted as input loss");
    const auto frozenCount=snap->window->sample_count();capture.stop();until([&]{const auto s=capture.status();return s.accepted_samples==s.processed_samples+s.processing_rejected_samples+s.canceled_samples;});
    require(!capture.status().active && snap->window->sample_count()==frozenCount,"Stopping input discarded the frozen observation");
}
void continuousInput(unsigned seconds=2) {
    SignalCaptureBudget budget;budget.queue_payload_bytes=65536;budget.maximum_block_bytes=8192;budget.maximum_queued_blocks=32;budget.retention={131072,1,128};SignalCapture capture(budget);
    const IqStreamParameters stream{1000000,434000000,SampleFormat::cf32_le};capture.start(stream,"independent.sustained");const auto bytes=carrier(stream.format,256);
    std::uint64_t offered=0;const auto start=std::chrono::steady_clock::now(),end=start+std::chrono::seconds(seconds);
    while(std::chrono::steady_clock::now()<end){capture.offer({bytes,offered,stream,false});offered+=256;
        std::this_thread::sleep_until(start+std::chrono::nanoseconds(offered*1000000000ULL/stream.sample_rate));
        if(offered%65536==0){const auto status=capture.status();require(status.queued_payload_bytes<=budget.queue_payload_bytes && status.peak_queued_payload_bytes<=budget.queue_payload_bytes && status.queued_blocks<=budget.maximum_queued_blocks && status.window.retained_payload_bytes<=budget.retention.payload_bytes,"Sustained input exceeded an actual payload budget");}}
    const auto snap=freeze(capture);const auto s=capture.status();require(s.accepted_samples+s.rejected_samples==offered && s.accepted_samples==s.processed_samples,"Sustained accepted/rejected accounting did not reconcile");
    require(snap->window->sample_count()<=16384 && snap->window->segments.size()<=128 && snap->status.window.expired_samples>0,"Sustained retention did not remain bounded");
    const auto first=snap->window->first_sample(),last=snap->window->end_sample();accepted(capture,bytes,offered,stream);freeze(capture);
    require(first==snap->window->first_sample() && last==snap->window->end_sample(),"Continuing capture rewrote an earlier frozen window");
    std::cout<<"paced_seconds="<<seconds<<" source_rate="<<stream.sample_rate<<" offered_samples="<<offered<<" accepted="<<s.accepted_samples<<" rejected="<<s.rejected_samples<<" peak_queue_bytes="<<s.peak_queued_payload_bytes<<" retained_bytes="<<s.window.retained_payload_bytes<<'\n';
}
void invalidAndStaleInput() {
    SignalCapture capture;const IqStreamParameters stream{1000000,433000000,SampleFormat::cf32_le};
    capture.start(stream,"independent.invalid");const auto emptyId=capture.requestSnapshot();std::shared_ptr<const SignalCaptureSnapshot> empty;
    until([&]{empty=capture.takeSnapshot();return bool(empty);});require(empty->request==emptyId && !empty->window && !empty->error.empty(),"Empty capture was represented as available samples");
    accepted(capture,carrier(stream.format,64),0,stream);
    auto bad=carrier(stream.format,64,64);sample_codec(stream.format).encode(bad.data(),{std::numeric_limits<float>::quiet_NaN(),0}); // Deliberately invalid fixture through the codec owner.
    accepted(capture,bad,64,stream);accepted(capture,carrier(stream.format,64,128),128,stream);
    const auto foreign=carrier(SampleFormat::ci16_le,64);require(!capture.offer({foreign,192,{1000000,433000000,SampleFormat::ci16_le},false}),"A new format entered an old source generation");
    const Bytes incomplete{0};require(!capture.offer({incomplete,192,stream,false}),"An incomplete sample pair entered the queue");
    const auto result=freeze(capture);require(result->window->sample_count()==128 && result->status.processing_rejected_samples==64 && result->status.window.known_missing_samples==64 && !result->status.error.empty(),"Malformed source payload poisoned the window or lost processing-stage loss");
    capture.requestSnapshot();capture.start({500000,435000000,SampleFormat::cf32_le},"independent.after-pending-freeze");
    accepted(capture,carrier(stream.format,32),700,{500000,435000000,SampleFormat::cf32_le});const auto changed=freeze(capture);
    require(changed->generation!=result->generation && changed->window->first_sample()==700 && changed->window->sample_count()==32,"A stale pending freeze escaped into a new source epoch");
    capture.stop();require(!capture.offer({incomplete,0,stream,false}) && capture.status().inactive_blocks==1,"A late callback revived a stopped capture");
    bool refused=false;try{capture.requestSnapshot();}catch(const std::logic_error&){refused=true;}require(refused,"Stopped input accepted a new freeze request");
    const auto previous=capture.start(stream,"independent.same-parameters-old");const auto next=capture.start(stream,"independent.same-parameters-new");
    const auto valid=carrier(stream.format,32);require(!capture.offer({valid,0,stream,false},previous),"A late callback entered a reconnected source with identical parameters");
    require(capture.status().rejected_samples==0 && capture.status().stale_generation_samples_total==32,"Stale input was misattributed to the current source's loss");
    until([&]{return capture.offer({valid,123,stream,false},next);});require(freeze(capture)->window->first_sample()==123,"Source token gating discarded the current generation");
    capture.close();require(!capture.offer({valid,155,stream,false},next),"A permanently closed worker accepted late samples");
    refused=false;try{capture.start(stream,"closed");}catch(const std::logic_error&){refused=true;}require(refused,"A closed capture restarted without its worker");
}
}
int main(int argc,char** argv){try{
    if(argc!=1){if(argc!=3)throw std::invalid_argument("Use --sustained-seconds 1..3600");unsigned seconds=0;const auto value=std::string_view(argv[2]);const auto parsed=std::from_chars(value.data(),value.data()+value.size(),seconds);if(argc!=3 || std::string_view(argv[1])!="--sustained-seconds" || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || seconds<1 || seconds>3600)throw std::invalid_argument("Use --sustained-seconds 1..3600");continuousInput(seconds);return 0;}
    for(const auto format:{SampleFormat::cu8,SampleFormat::ci16_le,SampleFormat::cf32_le}){std::cerr<<"capture truth "<<sample_datatype(format)<<'\n';sourceTruthAndBarrier(format);}std::cerr<<"capture overload\n";overloadAndFragments();std::cerr<<"capture invalid\n";invalidAndStaleInput();std::cerr<<"capture paced\n";continuousInput();std::cout<<"Independent capture truth, codecs, exact freeze barrier, source generations, gaps, bounded short reads and sustained input passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
