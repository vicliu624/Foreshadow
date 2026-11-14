#pragma once
#include "signalscope/adapters/modulation/detection_selection.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <memory>

namespace signalscope {
inline bool overlaps_broadcast_fm(double frequency,double width) {
    return frequency+width/2>=65e6 && frequency-width/2<=110e6;
}

class NarrowbandEvents {
public:
    void append(Json event,Json fm,std::uint32_t rate);
    void publish(Json& status) const;
private:
    Json events_=Json::array();
    Json fm_=Json::array();
};

class FskDetector {
public:
    struct Detections {Json candidate;Json broadcasts=Json::array();};
    explicit FskDetector(SampleFormat format=SampleFormat::cu8,NarrowbandSelection selection={},bool exclude_dc=true);
    ~FskDetector();
    Json detect(std::span<const std::uint8_t> iq,std::uint64_t first,const Config&,double search_width,
                Json& broadcast_fm);
    Detections detectAll(std::span<const std::uint8_t> iq,std::uint64_t first,const Config&,double search_width);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
