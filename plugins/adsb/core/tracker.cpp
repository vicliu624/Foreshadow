#include "tracker.hpp"
#include <algorithm>
#include <cmath>
namespace signalscope::adsb {
void Tracker::clear(){entries_.clear();initialized_=false;}
void Tracker::observe(const Fields& fields,double time) {
    if(!fields.aircraft_fields)return;
    if(!std::isfinite(time))return;
    if(initialized_ && time<clock_)clear();
    initialized_=true;clock_=time;
    for(auto it=entries_.begin();it!=entries_.end();)if(time-it->second.aircraft.last_seen>120)it=entries_.erase(it);else ++it;
    // DF18 control and DF17 address spaces must never merge by numeric ID.
    const auto key=aircraft_key(fields);
    if(!entries_.contains(key) && entries_.size()>=1024){const auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second.aircraft.last_seen<b.second.aircraft.last_seen;});entries_.erase(oldest);}
    auto& entry=entries_[key];auto& aircraft=entry.aircraft;
    aircraft.latest=fields;aircraft.last_seen=time;++aircraft.messages;
    if(!fields.callsign.empty())aircraft.callsign=fields.callsign;
    if(fields.altitude_ft)aircraft.altitude_ft=fields.altitude_ft;
    if(fields.ground_speed_kt)aircraft.ground_speed_kt=fields.ground_speed_kt;
    if(fields.track_deg)aircraft.track_deg=fields.track_deg;
    if(fields.cpr){if(fields.odd){entry.odd=fields;entry.odd_time=time;}else{entry.even=fields;entry.even_time=time;}
        if(entry.even && entry.odd)if(auto position=airborne_position(*entry.even,entry.even_time,*entry.odd,entry.odd_time)){
            aircraft.position=position;aircraft.position_time=time;
            if(aircraft.trail.size()==128)aircraft.trail.erase(aircraft.trail.begin());
            aircraft.trail.push_back(*position);
        }
    }
}
std::vector<Aircraft> Tracker::snapshot(double time) const {
    std::vector<Aircraft> result;
    for(const auto& [key,entry]:entries_){(void)key;if(time-entry.aircraft.last_seen>120)continue;
        auto value=entry.aircraft;if(time-value.position_time>15)value.position.reset();result.push_back(std::move(value));}
    return result;
}
}
