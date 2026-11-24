#pragma once
#include "codec.hpp"
#include <map>
#include <vector>
namespace signalscope::adsb {
inline std::uint64_t aircraft_key(const Fields& fields){return (std::uint64_t(fields.downlink_format)<<32)|(std::uint64_t(fields.downlink_format==18?fields.control:0)<<24)|fields.address;}
struct Aircraft {
    Fields latest;
    std::string callsign;
    std::optional<int> altitude_ft;
    std::optional<double> ground_speed_kt,track_deg;
    std::optional<Position> position;
    double last_seen=0,position_time=0;
    std::uint64_t messages=0;
    std::vector<Position> trail;
};
class Tracker {
public:
    void clear();
    void observe(const Fields&,double source_seconds);
    std::vector<Aircraft> snapshot(double source_seconds) const;
private:
    struct Entry {Aircraft aircraft;std::optional<Fields> even,odd;double even_time=0,odd_time=0;};
    std::map<std::uint64_t,Entry> entries_;
    double clock_=0;
    bool initialized_=false;
};
}
