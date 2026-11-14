#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
namespace signalscope::distribution {
std::uint64_t processId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}
bool processAlive(std::uint64_t pid) {
    if (pid == 0 || pid > 2147483647)
        return false;
#ifdef _WIN32
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const auto success = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return success && code == STILL_ACTIVE;
#else
    std::ifstream status("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (status && std::getline(status, line)) {
        const auto close = line.rfind(')');
        if (close != std::string::npos && close + 2 < line.size() && line[close + 2] == 'Z')
            return false;
    }
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}
namespace {
struct Snapshot {
    std::vector<ActiveComponent> components;
    std::map<std::string, std::set<std::string>> disabled;
    std::filesystem::path lease;
    Snapshot() {
        Store store;
        components = store.components("");
        const auto data = store.state();
        for (const auto &[id, item] : data.at("installed").items())
            if (!item.at("enabled").get<bool>())
                for (const auto &component : item.at("manifest").value("components", Json::array()))
                    disabled[component.at("kind")].insert(component.at("id"));
        Json roots = Json::array();
        for (const auto &component : components)
            roots.push_back(path_to_utf8(component.root));
        for (auto directory = application_directory(); !directory.empty() && directory != directory.parent_path();
             directory = directory.parent_path()) {
            if (std::filesystem::is_regular_file(directory / "toolbox.json")) {
                roots.push_back(path_to_utf8(directory));
                break;
            }
        }
        if (!roots.empty()) {
            lease = store.root() / "runtime" / (std::to_string(processId()) + ".json");
            writeJsonAtomically(lease, {{"pid", processId()}, {"roots", roots}});
        }
    }
    ~Snapshot() {
        if (!lease.empty()) {
            std::error_code ignored;
            std::filesystem::remove(lease, ignored);
        }
    }
};
Snapshot &snapshot() {
    static Snapshot value;
    return value;
}
} // namespace
std::vector<ActiveComponent> runtimeComponents(const std::string &kind) {
    std::vector<ActiveComponent> result;
    for (const auto &component : snapshot().components)
        if (component.kind == kind)
            result.push_back(component);
    return result;
}
std::set<std::string> runtimeDisabledComponents(const std::string &kind) {
    const auto found = snapshot().disabled.find(kind);
    return found == snapshot().disabled.end() ? std::set<std::string>{} : found->second;
}
} // namespace signalscope::distribution
