#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <shobjidl.h>
#else
#include <signal.h>
#endif
using namespace signalscope;
using namespace signalscope::distribution;
namespace fs = std::filesystem;
namespace {
QString native(const fs::path &p) { return QString::fromStdString(path_to_utf8(p)); }
fs::path path(const Json &p) { return path_from_utf8(p.get<std::string>()); }
qint64 launch(const Json &descriptor, const QStringList &extra = {}) {
    QStringList arguments;
    for (const auto &argument : descriptor.at("arguments"))
        arguments << QString::fromStdString(argument.get<std::string>());
    arguments << extra;
    qint64 pid = 0;
    if (!QProcess::startDetached(native(path(descriptor.at("executables").at("gui"))), arguments,
                                 native(path(descriptor.at("root"))), &pid))
        throw Error("activation");
    return pid;
}
void terminate(std::uint64_t pid) {
#ifdef _WIN32
    const auto handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (handle) {
        TerminateProcess(handle, 1);
        CloseHandle(handle);
    }
#else
    kill(static_cast<pid_t>(pid), SIGTERM);
#endif
}
void copyFile(const fs::path &source, const fs::path &destination) {
    fs::create_directories(destination.parent_path());
    auto temporary = destination;
    temporary += ".next";
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
        fs::remove(destination, error);
        error.clear();
        fs::rename(temporary, destination, error);
        if (error)
            throw Error("storage");
    }
}
void shortcuts(const Json &request, const fs::path &appRoot) {
    const auto source = path(request.at("launcher"));
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    for (const auto &[name, role] :
         std::map<std::string, std::string>{{"signalscope", "cli"}, {"signalscope-gui", "gui"}}) {
        const auto launcher = appRoot / "bin" / (name + suffix);
        copyFile(role == "gui" && request.contains("gui_launcher") ? path(request.at("gui_launcher")) : source,
                 launcher);
        writeJsonAtomically(path_from_utf8(path_to_utf8(launcher) + ".json"),
                            {{"root", path_to_utf8(appRoot)}, {"role", role}});
    }
#if defined(__linux__)
    const auto home = environment_utf8("HOME");
    if (!home)
        throw Error("home");
    const auto base = path_from_utf8(*home);
    fs::create_directories(base / ".local/bin");
    for (const auto *name : {"signalscope", "signalscope-gui"}) {
        const auto link = base / ".local/bin" / name;
        auto temporary = link;
        temporary += ".next";
        std::error_code ignored;
        fs::remove(temporary, ignored);
        fs::create_symlink(appRoot / "bin" / name, temporary);
        fs::rename(temporary, link);
    }
    const auto applications = base / ".local/share/applications";
    fs::create_directories(applications);
    auto executable = path_to_utf8(appRoot / "bin/signalscope-gui");
    std::string escaped;
    for (const auto c : executable) {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
            escaped += '\\';
        if (c == '%')
            escaped += '%';
        escaped += c;
    }
    std::ofstream desktop(applications / "signalscope.desktop");
    desktop << "[Desktop Entry]\nType=Application\nName=Foreshadow\nExec=\"" << escaped
            << "\"\nTerminal=false\nCategories=Science;\n";
    if (!desktop)
        throw Error("storage");
#elif defined(_WIN32)
    const auto profile = environment_utf8("USERPROFILE");
    if (profile) {
        const auto destination = path_from_utf8(*profile) / "Desktop/Foreshadow.lnk";
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IShellLinkW *link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                       reinterpret_cast<void **>(&link)))) {
            link->SetPath((appRoot / "bin/signalscope-gui.exe").c_str());
            IPersistFile *file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file)))) {
                file->Save(destination.c_str(), TRUE);
                file->Release();
            }
            link->Release();
        }
        CoUninitialize();
    }
#elif defined(__APPLE__)
    const auto home = environment_utf8("HOME");
    if (home) {
        const auto bundle = path_from_utf8(*home) / "Applications/Foreshadow.app/Contents";
        const auto launcher = bundle / "MacOS/Foreshadow";
        copyFile(source, launcher);
        writeJsonAtomically(path_from_utf8(path_to_utf8(launcher) + ".json"),
                            {{"root", path_to_utf8(appRoot)}, {"role", "gui"}});
        std::ofstream info(bundle / "Info.plist");
        info << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist "
                "version=\"1.0\"><dict><key>CFBundleExecutable</key><string>Foreshadow</string><key>CFBundleIdentifier</"
                "key><string>org.signalscope.launcher</string><key>CFBundleName</key><string>Foreshadow</"
                "string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>";
    }
#endif
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    fs::path requestPath;
    Json recovery;
    bool parentExited = false;
    Json result;
    std::uint64_t startedPid = 0;
    try {
        if (app.arguments().size() != 3 || app.arguments()[1] != "--request")
            return 2;
        requestPath = path_from_utf8(QFileInfo(app.arguments()[2]).absoluteFilePath().toStdString());
        const auto request = readJson(requestPath);
        if (request.at("schema") != 1)
            throw Error("activation");
        const auto root = fs::weakly_canonical(path(request.at("store")));
        const auto within = fs::weakly_canonical(requestPath).lexically_relative(root / "updates");
        if (within.empty() || *within.begin() == "..")
            throw Error("path");
        const auto parent = request.at("parent_pid").get<std::uint64_t>();
        if (parent == processId())
            throw Error("activation");
        const auto waitStarted = std::chrono::steady_clock::now();
        while (processAlive(parent)) {
            if (std::chrono::steady_clock::now() - waitStarted > std::chrono::seconds(120))
                throw Error("timeout");
            QThread::msleep(50);
        }
        parentExited = true;
        recovery = request.at("previous");
        ActivationGuard guard(root);
        Store store(root);
        if (store.state().at("installed") != request.at("target_installed"))
            throw Error("activation_state");
        const auto &target = request.at("target");
        const auto targetRoot = path(target.at("root"));
        const auto appRoot = root / "application";
        fs::create_directories(appRoot);
        const auto previous = request.at("previous");
        const auto previousPointer =
            fs::exists(appRoot / "current.json") ? readJson(appRoot / "current.json") : previous;
        const auto lease = root / "runtime" / (std::to_string(processId()) + ".json");
        writeJsonAtomically(lease,
                            {{"pid", processId()}, {"roots", Json::array({previous.at("root"), target.at("root")})}});
        result = {{"schema", 1}, {"requested_version", target.at("version")}, {"nonce", request.at("nonce")}};
        try {
            if (sha256(path(target.at("executables").at("gui"))) != target.at("gui_sha256").get<std::string>())
                throw Error("hash");
            if (!request.value("skip_shortcuts", false))
                shortcuts(request, appRoot);
            writeJsonAtomically(appRoot / "current.json", target);
            startedPid = static_cast<std::uint64_t>(
                launch(target, {"--update-health-request", native(requestPath), "--update-health-nonce",
                                QString::fromStdString(request.at("nonce").get<std::string>())}));
            const auto timeout = request.value("health_timeout_seconds", 30);
            bool healthy = false;
            std::optional<std::chrono::steady_clock::time_point> received;
            const auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout) &&
                   processAlive(startedPid)) {
                const auto health = requestPath.parent_path() / "health.json";
                if (fs::is_regular_file(health)) {
                    const auto receipt = readJson(health);
                    if (receipt.value("nonce", std::string()) == request.at("nonce").get<std::string>() &&
                        receipt.value("version", std::string()) == target.at("version").get<std::string>() &&
                        receipt.value("pid", std::uint64_t(0)) == startedPid) {
                        if (!received)
                            received = std::chrono::steady_clock::now();
                        if (std::chrono::steady_clock::now() - *received >= std::chrono::milliseconds(300)) {
                            healthy = true;
                            break;
                        }
                    }
                }
                QThread::msleep(50);
            }
            if (!healthy)
                throw Error("activation_health");
            auto state = store.state();
            const auto id = request.at("application_id").get<std::string>();
            if (state.at("installed").contains(id) && state["installed"][id].contains("previous")) {
                auto &retained = state["installed"][id]["previous"];
                retained["activation"] = previous;
                retained["directory"] = previous.at("root");
                retained["bundled"] = false;
                state["revision"] = state.at("revision").get<std::uint64_t>() + 1;
                writeJsonAtomically(root / "registry.json", state);
            }
            result["state"] = "applied";
            result["pid"] = startedPid;
        } catch (const std::exception &error) {
            if (startedPid && processAlive(startedPid))
                terminate(startedPid);
            auto state = store.state();
            state["installed"] = request.at("old_installed");
            state["revision"] = state.at("revision").get<std::uint64_t>() + 1;
            writeJsonAtomically(root / "registry.json", state);
            writeJsonAtomically(appRoot / "current.json", previousPointer);
            result["state"] = "rolled_back";
            result["error"] = error.what();
            result["pid"] = launch(previous);
        }
        writeJsonAtomically(requestPath.parent_path() / "result.json", result);
        writeJsonAtomically(root / "last-activation.json", result);
        std::error_code ignored;
        fs::remove(lease, ignored);
        return result.at("state") == "applied" ? 0 : 1;
    } catch (const std::exception &error) {
        Json failure = {{"state", "failed"}, {"error", error.what()}};
        if (parentExited && !recovery.is_null()) {
            try {
                failure["pid"] = launch(recovery);
            } catch (const std::exception &restartError) {
                failure["restart_error"] = restartError.what();
            }
        }
        if (!requestPath.empty())
            try {
                writeJsonAtomically(requestPath.parent_path() / "result.json", failure);
            } catch (...) {
            }
        std::cerr << error.what() << '\n';
        return 2;
    }
}
