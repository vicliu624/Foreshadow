#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace signalscope {
// Environment text crosses the native OS/UTF-8 boundary in the same place as paths.
std::optional<std::string> environment_utf8(const char* name);

inline std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(),value.end()));
}

enum class PathTextFormat {native,generic};
inline std::string path_to_utf8(const std::filesystem::path& path,PathTextFormat format=PathTextFormat::native) {
    const auto value=format==PathTextFormat::generic?path.generic_u8string():path.u8string();
    return {value.begin(),value.end()};
}
}
