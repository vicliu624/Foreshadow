#include "signalscope/backend/iq/path.hpp"
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace signalscope {
std::optional<std::string> environment_utf8(const char *name) {
#ifdef _WIN32
    const auto key = path_from_utf8(name);
    std::wstring value(128, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const auto count = GetEnvironmentVariableW(key.c_str(), value.data(), static_cast<DWORD>(value.size()));
        if (count == 0 && GetLastError() != ERROR_SUCCESS)
            return std::nullopt;
        if (count >= value.size()) {
            value.resize(count);
            continue;
        }
        value.resize(count);
        return path_to_utf8(std::filesystem::path(value));
    }
#else
    if (const auto *value = std::getenv(name))
        return std::string(value);
    return std::nullopt;
#endif
}
}
