#pragma once

#include <filesystem>

namespace signalscope {
// Copies an existing SigMF pair into a new directory and publishes metadata last.
void export_recording(const std::filesystem::path& metadata,const std::filesystem::path& destination);
}
