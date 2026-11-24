#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace signalscope::adsb {
using Message=std::array<std::uint8_t,14>;
// The remainder includes the received parity. No bit correction is applied.
std::uint32_t crc(std::span<const std::uint8_t> bytes);
struct Fields {
    std::uint32_t address=0;
    unsigned downlink_format=0,control=0,type_code=0;
    bool aircraft_fields=false;
    std::string hex,callsign;
    std::optional<int> altitude_ft;
    std::optional<double> ground_speed_kt,track_deg;
    std::optional<int> vertical_rate_fpm;
    bool cpr=false,odd=false;
    unsigned cpr_latitude=0,cpr_longitude=0;
};
// Only CRC-valid DF17/DF18 extended squitters enter semantic decoding.
// DF18 control remains explicit: its address is not necessarily an ICAO ID.
std::optional<Fields> decode(const Message&);
struct Position {double latitude,longitude;};
// Times are seconds on one continuous source clock, never wall-clock guesses.
std::optional<Position> airborne_position(const Fields& even,double even_time,const Fields& odd,double odd_time);
}
