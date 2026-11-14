#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <nlohmann/json.hpp>

namespace signalscope::transport {
// Explicit DVB selectors always take precedence. An unmarked national service
// name can use a declared receiver profile; it is never decoded by the local
// Windows code page or by the current UI language.
std::optional<std::string> decodeBroadcastText(std::span<const std::uint8_t>,const std::string& unmarkedEncoding="ISO_6937");
bool supportedBroadcastEncoding(const std::string&);
nlohmann::json broadcastTextParameter();
}
