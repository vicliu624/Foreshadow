#include "codec.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace signalscope::adsb {
std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t state=0;
    for(auto byte:bytes)for(int bit=7;bit>=0;--bit) {
        const bool high=(state&0x800000)!=0;
        state=((state<<1)|((byte>>bit)&1))&0xffffff;
        if(high)state^=0xfff409;
    }
    return state;
}
std::optional<Fields> decode(const Message& message) {
    Fields out;out.downlink_format=message[0]>>3;out.control=message[0]&7;
    if((out.downlink_format!=17 && out.downlink_format!=18) || crc(message))return {};
    const auto bits=[&](unsigned first,unsigned count){unsigned value=0;for(unsigned n=0;n<count;++n){const auto at=first+n;value=(value<<1)|((message[at/8]>>(7-at%8))&1);}return value;};
    out.address=bits(8,24);out.type_code=bits(32,5);
    constexpr char hex[]="0123456789ABCDEF";
    for(auto byte:message){out.hex+=hex[byte>>4];out.hex+=hex[byte&15];}
    out.aircraft_fields=out.downlink_format==17 || out.control==0 || out.control==1 || out.control==2 || out.control==5 || out.control==6;
    if(!out.aircraft_fields){out.type_code=0;return out;}
    if(out.type_code>=1 && out.type_code<=4) {
        for(unsigned n=0;n<8;++n){const auto c=bits(40+6*n,6);out.callsign+=c>=1 && c<=26?char('A'+c-1):c>=48 && c<=57?char('0'+c-48):' ';}
        while(!out.callsign.empty() && out.callsign.back()==' ')out.callsign.pop_back();
    }
    if((out.type_code>=9 && out.type_code<=18) || (out.type_code>=20 && out.type_code<=22)) {
        const auto altitude=bits(40,12);
        // Q=0 Gillham coding remains unavailable rather than guessed.
        if(out.type_code<=18 && (altitude&16))out.altitude_ft=int(((altitude&0xfe0)>>1)|(altitude&15))*25-1000;
        out.cpr=true;out.odd=bits(53,1);out.cpr_latitude=bits(54,17);out.cpr_longitude=bits(71,17);
    }
    if(out.type_code==19) {
        const auto subtype=bits(37,3);
        if(subtype==1 || subtype==2) {
            const auto east=bits(46,10),north=bits(57,10);const double scale=subtype==2?4:1;
            if(east && north){const double e=(int(east)-1)*scale*(bits(45,1)?-1:1),n=(int(north)-1)*scale*(bits(56,1)?-1:1);
                out.ground_speed_kt=std::hypot(e,n);out.track_deg=std::fmod(std::atan2(e,n)*180/std::numbers::pi+360,360);}
        }
        const auto vertical=bits(69,9);if(vertical)out.vertical_rate_fpm=(int(vertical)-1)*64*(bits(68,1)?-1:1);
    }
    return out;
}
std::optional<Position> airborne_position(const Fields& even,double even_time,const Fields& odd,double odd_time) {
    if(!even.cpr || !odd.cpr || even.odd || !odd.odd || even.address!=odd.address ||
       even.downlink_format!=odd.downlink_format || even.control!=odd.control ||
       (even.type_code<=18)!=(odd.type_code<=18) || !std::isfinite(even_time) || !std::isfinite(odd_time) ||
       std::abs(even_time-odd_time)>10)return {};
    const auto mod=[](double a,double b){return a-b*std::floor(a/b);};
    const auto nl=[](double latitude){
        latitude=std::abs(latitude);if(latitude>=87)return latitude>87?1:2;
        const auto c=std::cos(latitude*std::numbers::pi/180);
        const auto x=1-(1-std::cos(std::numbers::pi/30))/(c*c);
        return int(std::floor(2*std::numbers::pi/std::acos(std::clamp(x,-1.,1.))));
    };
    const double y0=even.cpr_latitude/131072.,y1=odd.cpr_latitude/131072.;
    const double j=std::floor(59*y0-60*y1+.5);
    double lat0=6*(mod(j,60)+y0),lat1=360./59*(mod(j,59)+y1);
    if(lat0>=270)lat0-=360;
    if(lat1>=270)lat1-=360;
    if(std::abs(lat0)>90 || std::abs(lat1)>90 || nl(lat0)!=nl(lat1))return {};
    const bool latestOdd=odd_time>even_time;const auto zones=nl(lat0);
    const auto n=std::max(1,zones-int(latestOdd));
    const double x0=even.cpr_longitude/131072.,x1=odd.cpr_longitude/131072.;
    const auto m=std::floor(x0*(zones-1)-x1*zones+.5);
    auto longitude=360./n*(mod(m,n)+(latestOdd?x1:x0));if(longitude>=180)longitude-=360;
    return Position{latestOdd?lat1:lat0,longitude};
}
}
