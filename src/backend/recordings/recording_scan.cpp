#include "signalscope/backend/recordings/recording_scan.hpp"
#include <cmath>

namespace signalscope {
void scanRecording(const ReplayInfo& info,const RecordingScanOptions& options,
                   const std::function<void(const Json&)>& emitWindow,
                   const std::function<bool()>& cancelled) {
    if(info.rate<225001 || info.rate>100000000)
        throw std::invalid_argument("Recording sample rate is outside the supported I/Q range");
    if(options.first>info.pairs || options.count>info.pairs-options.first ||
       !std::isfinite(options.search_width) || options.search_width<=0 || options.search_width>info.rate || !options.detectors.any())
        throw std::invalid_argument("Invalid recording scan range, search width or detector selection");
    const auto stride=bytes_per_pair(info.format);
    const std::size_t hop=info.rate/8;
    Bytes window;
    window.reserve(2*hop*stride);
    std::uint64_t windowFirst=options.first;
    std::uint64_t coveredUntil=options.first;
    const auto blockBytes=std::min<std::size_t>(hop*stride,(4*1024*1024/stride)*stride);
    replay(info,options.first,options.count,0,blockBytes,[&](auto bytes,auto){
      while(!bytes.empty() && !cancelled()) {
        const auto part=bytes.first(std::min(bytes.size(),2*hop*stride-window.size()));
        window.insert(window.end(),part.begin(),part.end());bytes=bytes.subspan(part.size());
        if(window.size()!=2*hop*stride)continue;
        auto candidates=detectSnapshot(window,info.format,info.rate,info.frequency,options.search_width,
                                       windowFirst,options.detectors);
        if(cancelled())return;
        coveredUntil=windowFirst+window.size()/stride;
        emitWindow({{"first_sample",windowFirst},{"end_sample_exclusive",coveredUntil},
                    {"sample_rate_hz",info.rate},{"capture_center_hz",info.frequency},
                    {"events",std::move(candidates)}});
        window.erase(window.begin(),window.begin()+hop*stride);
        windowFirst+=hop;
      }
    },cancelled);
    const auto end=windowFirst+window.size()/stride;
    if(end>coveredUntil && !cancelled()) {
        auto candidates=detectSnapshot(window,info.format,info.rate,info.frequency,options.search_width,
                                       windowFirst,options.detectors);
        if(!cancelled())
            emitWindow({{"first_sample",windowFirst},{"end_sample_exclusive",end},
                        {"sample_rate_hz",info.rate},{"capture_center_hz",info.frequency},
                        {"events",std::move(candidates)}});
    }
}
}
