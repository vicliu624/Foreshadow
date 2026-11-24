// Source-field conventions follow WSJT-X wsprd_utils.c:
// Copyright 2001-2015 Joe Taylor, K1JT; 2014-2015 Steven Franke, K9AN.
// GPL-3.0-or-later; see ../vendor/wsjtx/COPYING.
// Foreshadow adaptation (2026-11-24): bounded strings and explicit unresolved hashes.
#include "message.hpp"
namespace signalscope::wspr {
namespace {
constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
bool power(int value) {
    return value >= 0 && value <= 60 && (value % 10 == 0 || value % 10 == 3 || value % 10 == 7);
}
std::string trim(std::string value) {
    const auto begin = value.find_first_not_of(' ');
    if (begin == std::string::npos)
        return {};
    return value.substr(begin, value.find_last_not_of(' ') - begin + 1);
}
std::optional<std::string> call(std::uint32_t n) {
    if (n >= 262177560)
        return {};
    std::string value(6, ' ');
    for (int k = 5; k >= 3; --k) {
        value[k] = alphabet[n % 27 + 10];
        n /= 27;
    }
    value[2] = alphabet[n % 10];
    n /= 10;
    value[1] = alphabet[n % 36];
    n /= 36;
    value[0] = alphabet[n];
    return value;
}
} // namespace
std::optional<Message> unpack_message(const std::array<std::uint8_t, 11> &bytes) {
    Message out;
    for (unsigned bit = 0; bit < 50; ++bit)
        out.payload = (out.payload << 1) | ((bytes[bit / 8] >> (7 - bit % 8)) & 1u);
    for (unsigned bit = 50; bit < 88; ++bit)
        if ((bytes[bit / 8] >> (7 - bit % 8)) & 1u)
            return {};
    const auto n = std::uint32_t(out.payload >> 22), m = std::uint32_t(out.payload & 0x3fffff);
    const auto decoded = call(n);
    if (!decoded)
        return {};
    const auto type = int(m & 127) - 64;
    if (type < 0) {
        out.kind = MessageKind::hashed;
        out.power_dbm = -type - 1;
        if (!power(out.power_dbm))
            return {};
        out.locator = decoded->substr(5, 1) + decoded->substr(0, 5);
        if (out.locator[0] < 'A' || out.locator[0] > 'R' || out.locator[1] < 'A' || out.locator[1] > 'R' ||
            out.locator[2] < '0' || out.locator[2] > '9' || out.locator[3] < '0' || out.locator[3] > '9' ||
            out.locator[4] < 'A' || out.locator[4] > 'X' || out.locator[5] < 'A' || out.locator[5] > 'X')
            return {};
        out.callsign_hash = m >> 7;
        return out;
    }
    if (type > 62)
        return {};
    out.callsign = trim(*decoded);
    if (out.callsign.empty())
        return {};
    if (power(type)) {
        out.power_dbm = type;
        const auto grid = m >> 7;
        if (grid >= 32400)
            return {};
        const auto longitude = 179 - grid / 180, latitude = grid % 180;
        out.locator = {char('A' + longitude / 10), char('A' + latitude / 10), char('0' + longitude % 10),
                       char('0' + latitude % 10)};
        return out;
    }
    out.kind = MessageKind::compound;
    const auto unit = type % 10;
    const auto extra = unit > 7 ? unit - 7 : unit > 3 ? unit - 3 : unit;
    if (extra < 1 || extra > 3)
        return {};
    out.power_dbm = type - extra;
    if (!power(out.power_dbm))
        return {};
    auto prefix = (m >> 7) + 32768 * (extra - 1);
    if (prefix < 60000) {
        if (prefix >= 37 * 37 * 37)
            return {};
        std::string value(3, ' ');
        for (int k = 2; k >= 0; --k) {
            value[k] = alphabet[prefix % 37];
            prefix /= 37;
        }
        value = trim(value);
        if (value.empty())
            return {};
        out.callsign = value + '/' + out.callsign;
    } else {
        const auto suffix = prefix - 60000;
        if (suffix <= 35)
            out.callsign += '/' + std::string(1, alphabet[suffix]);
        else if (suffix <= 125)
            out.callsign += '/' + std::to_string(suffix - 26);
        else
            return {};
    }
    return out;
}
} // namespace signalscope::wspr
