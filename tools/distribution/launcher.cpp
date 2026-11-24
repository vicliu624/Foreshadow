#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif
namespace fs = std::filesystem;
fs::path selfPath() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(count);
    return path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    _NSGetExecutablePath(path.data(), &size);
    return fs::weakly_canonical(path.c_str());
#else
    return fs::canonical("/proc/self/exe");
#endif
}
nlohmann::json read(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("launcher_configuration");
    return nlohmann::json::parse(file);
}
fs::path fromUtf8(const std::string &path) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(path.data()), path.size()));
}
void configureDistributionRoot(const nlohmann::json &config) {
    if (!config.contains("distribution_root"))
        return;
    const auto value = config.at("distribution_root").get<std::string>();
    const auto root = fromUtf8(value);
    if (value.empty() || value.find('\0') != std::string::npos || !root.is_absolute())
        throw std::runtime_error("launcher_configuration");
#ifdef _WIN32
    if (!SetEnvironmentVariableW(L"SIGNALSCOPE_DISTRIBUTION_ROOT", root.c_str()))
#else
    if (setenv("SIGNALSCOPE_DISTRIBUTION_ROOT", root.c_str(), 1) != 0)
#endif
        throw std::runtime_error("launcher_environment");
}
#ifdef _WIN32
std::wstring quote(const std::wstring &text) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : text) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += c;
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
int launchMain(int argc, wchar_t **argv) {
    try {
        auto configPath = selfPath();
        configPath += L".json";
        const auto config = read(configPath);
        configureDistributionRoot(config);
        const auto descriptor = read(fromUtf8(config.at("root")) / "current.json");
        const auto executable = fromUtf8(descriptor.at("executables").at(config.at("role").get<std::string>()));
        std::wstring command = quote(executable.wstring());
        for (int i = 1; i < argc; ++i)
            command += L' ' + quote(argv[i]);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        const bool console = config.at("role") == "cli";
        if (console) {
            startup.dwFlags |= STARTF_USESTDHANDLES;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, console ? TRUE : FALSE,
                            console ? 0 : CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
            throw std::runtime_error("launcher_process");
        CloseHandle(process.hThread);
        DWORD result = 0;
        if (console) {
            WaitForSingleObject(process.hProcess, INFINITE);
            GetExitCodeProcess(process.hProcess, &result);
        }
        CloseHandle(process.hProcess);
        return static_cast<int>(result);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#ifdef AMC_LAUNCHER_GUI
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    const auto result = launchMain(argc, argv);
    LocalFree(argv);
    return result;
}
#else
int wmain(int argc, wchar_t **argv) { return launchMain(argc, argv); }
#endif
#else
int main(int argc, char **argv) {
    try {
        auto configPath = selfPath();
        configPath += ".json";
        const auto config = read(configPath);
        configureDistributionRoot(config);
        const auto descriptor = read(fromUtf8(config.at("root")) / "current.json");
        const auto executable = fromUtf8(descriptor.at("executables").at(config.at("role").get<std::string>()));
        const auto name = executable.string();
        std::vector<char *> arguments{const_cast<char *>(name.c_str())};
        for (int i = 1; i < argc; ++i)
            arguments.push_back(argv[i]);
        arguments.push_back(nullptr);
        execv(executable.c_str(), arguments.data());
        throw std::runtime_error("launcher_process");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
