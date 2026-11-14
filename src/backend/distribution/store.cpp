#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace signalscope::distribution {
namespace fs = std::filesystem;
namespace {
std::string uniqueName() {
    static std::atomic<unsigned long long> count = 0;
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '-' + std::to_string(++count);
}
class Lock {
  public:
    explicit Lock(const fs::path &root, const char *name = "store.lock") {
        fs::create_directories(root);
#ifdef _WIN32
        handle_ = CreateFileW((root / name).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED at{};
        if (handle_ == INVALID_HANDLE_VALUE ||
            !LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at)) {
            if (handle_ != INVALID_HANDLE_VALUE)
                CloseHandle(handle_);
            throw Error("busy");
        }
#else
        handle_ = ::open((root / name).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (handle_ < 0 || flock(handle_, LOCK_EX | LOCK_NB) != 0) {
            if (handle_ >= 0)
                ::close(handle_);
            throw Error("busy");
        }
#endif
    }
    ~Lock() {
#ifdef _WIN32
        OVERLAPPED at{};
        UnlockFileEx(handle_, 0, 1, 0, &at);
        CloseHandle(handle_);
#else
        flock(handle_, LOCK_UN);
        ::close(handle_);
#endif
    }
    Lock(const Lock &) = delete;

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
struct Temporary {
    fs::path path;
    ~Temporary() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
Json emptyState() {
    return {{"schema", 1}, {"revision", 0}, {"highest_catalog_sequence", 0}, {"installed", Json::object()}};
}
Json loadState(const fs::path &root) {
    if (!fs::exists(root / "registry.json"))
        return emptyState();
    auto state = readJson(root / "registry.json");
    if (state.at("schema") != 1 || !state.at("installed").is_object())
        throw Error("registry");
    return state;
}
void saveState(const fs::path &root, Json state) {
    state["revision"] = state.at("revision").get<std::uint64_t>() + 1;
    writeJsonAtomically(root / "registry.json", state);
}
void requireAction(const Json &item, const std::string &action) {
    const auto actions =
        item.at("manifest")
            .value("actions", std::vector<std::string>{"update", "enable", "disable", "uninstall", "rollback"});
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        throw Error("action", {{"action", action}});
}
void validateSet(const Json &state, Environment env, const PackageTypes &types) {
    env.installed.clear();
    for (const auto &[id, item] : state.at("installed").items())
        if (item.value("enabled", true))
            env.installed[id] = item.at("version");
    std::set<std::string> components;
    for (const auto &[id, item] : state.at("installed").items())
        if (item.value("enabled", true)) {
            const auto &manifest = item.at("manifest");
            for (const auto &component : manifest.value("components", Json::array()))
                if (!components
                         .insert(component.at("kind").get<std::string>() + '\n' + component.at("id").get<std::string>())
                         .second)
                    throw Error("duplicate_component", {{"id", component.at("id")}});
            Package p;
            p.id = id;
            p.kind = manifest.at("kind");
            p.version = item.at("version");
            p.platform = manifest.at("platform");
            p.manifest = manifest;
            const auto type = types.find(manifest.at("kind"));
            if (type == types.end())
                throw Error("package_type");
            const auto result = type->second->validateInstalled(p, env);
            if (!result.compatible) {
                auto arguments = result.arguments;
                arguments["package"] = id;
                throw Error(result.code, arguments);
            }
        }
}
std::string lower(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return value;
}
void commitDirectory(const fs::path &source, const fs::path &destination) {
#ifdef _WIN32
    // Readers such as indexers/scanners may briefly hold a newly extracted
    // file without FILE_SHARE_DELETE. Keep the transaction uncommitted and
    // retry only the native access/sharing errors, with a finite deadline.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        std::error_code error;
        fs::rename(source, destination, error);
        if (!error)
            return;
        if ((error.value() != ERROR_ACCESS_DENIED && error.value() != ERROR_SHARING_VIOLATION &&
             error.value() != ERROR_LOCK_VIOLATION) || std::chrono::steady_clock::now() >= deadline)
            throw Error("storage", {{"operation", "commit_directory"}, {"source", path_to_utf8(source)},
                                    {"destination", path_to_utf8(destination)}, {"native_error", error.value()}});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#else
    fs::rename(source, destination);
#endif
}
void extract(const Package &p, const fs::path &archivePath, const fs::path &target, bool allowLinks) {
    std::unique_ptr<archive, decltype(&archive_read_free)> input(archive_read_new(), archive_read_free);
    archive_read_support_format_tar(input.get());
    archive_read_support_format_zip(input.get());
    archive_read_support_filter_none(input.get());
    archive_read_support_filter_gzip(input.get());
    archive_read_support_filter_zstd(input.get());
#ifdef _WIN32
    const auto opened = archive_read_open_filename_w(input.get(), archivePath.c_str(), 65536);
#else
    const auto opened = archive_read_open_filename(input.get(), archivePath.c_str(), 65536);
#endif
    if (opened != ARCHIVE_OK)
        throw Error("archive");
    archive_entry *entry = nullptr;
    std::uint64_t expanded = 0;
    std::size_t count = 0;
    std::set<std::string> paths;
    std::vector<fs::path> links;
    int status = 0;
    while ((status = archive_read_next_header(input.get(), &entry)) == ARCHIVE_OK) {
        if (++count > 50000)
            throw Error("archive");
        const auto *utf8 = archive_entry_pathname_utf8(entry);
        if (!utf8)
            throw Error("path");
        std::string name = utf8;
        while (name.ends_with('/'))
            name.pop_back();
        if (name.empty() || name == ".") {
            if (archive_entry_filetype(entry) == AE_IFDIR)
                continue;
            throw Error("path");
        }
        const auto output = checkedRelative(target, name);
        if (!paths.insert(lower(name)).second || archive_entry_hardlink(entry))
            throw Error("path", {{"path", name}});
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            fs::create_directories(output);
            continue;
        }
        fs::create_directories(output.parent_path());
        if (fs::exists(output) || fs::is_symlink(output))
            throw Error("path", {{"path", name}});
        if (archive_entry_filetype(entry) == AE_IFLNK) {
            const auto *to = archive_entry_symlink_utf8(entry);
            if (!allowLinks || !to || path_from_utf8(to).is_absolute())
                throw Error("path", {{"path", name}});
            fs::create_symlink(path_from_utf8(to), output);
            links.push_back(output);
            continue;
        }
        if (archive_entry_filetype(entry) != AE_IFREG || archive_entry_size(entry) < 0)
            throw Error("archive");
        const auto expected = static_cast<std::uint64_t>(archive_entry_size(entry));
        if (expected > p.installed_size - expanded)
            throw Error("expanded_size");
        std::ofstream stream(output, std::ios::binary);
        if (!stream)
            throw Error("storage");
        std::array<char, 65536> bytes{};
        std::uint64_t written = 0;
        la_ssize_t size = 0;
        while ((size = archive_read_data(input.get(), bytes.data(), bytes.size())) > 0) {
            written += static_cast<std::uint64_t>(size);
            if (written > expected)
                throw Error("expanded_size");
            stream.write(bytes.data(), size);
            if (!stream)
                throw Error("storage");
        }
        stream.close();
        if (!stream || size < 0 || written != expected)
            throw Error("archive");
        expanded += written;
        const auto permission = archive_entry_perm(entry);
        fs::permissions(output, (permission & 0111)
                                    ? fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                          fs::perms::others_read | fs::perms::others_exec
                                    : fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                          fs::perms::others_read);
    }
    if (status != ARCHIVE_EOF || expanded != p.installed_size)
        throw Error("archive");
    for (const auto &link : links) {
        const auto relative = fs::weakly_canonical(link).lexically_relative(fs::weakly_canonical(target));
        if (relative.empty() || *relative.begin() == ".." || !fs::exists(link))
            throw Error("path");
    }
}
} // namespace
Json readJson(const fs::path &path) {
    if (fs::file_size(path) > 8 * 1024 * 1024)
        throw Error("file");
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw Error("file", {{"path", path_to_utf8(path)}});
    try {
        return Json::parse(stream);
    } catch (const std::exception &e) {
        throw Error("json", {{"detail", e.what()}});
    }
}
void writeJsonAtomically(const fs::path &path, const Json &data) {
    fs::create_directories(path.parent_path());
    Temporary temporary{path.parent_path() / path_from_utf8(path_to_utf8(path.filename()) + ".tmp-" + uniqueName())};
    {
        std::ofstream stream(temporary.path, std::ios::binary);
        stream << data.dump(2) << '\n';
        stream.flush();
        if (!stream)
            throw Error("storage");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error("storage");
#else
    const auto handle = ::open(temporary.path.c_str(), O_RDONLY | O_CLOEXEC);
    if (handle < 0)
        throw Error("storage");
    const auto result = ::fsync(handle);
    ::close(handle);
    if (result != 0)
        throw Error("storage");
    fs::rename(temporary.path, path);
    const auto directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        ::fsync(directory);
        ::close(directory);
    }
#endif
}
fs::path checkedRelative(const fs::path &root, const std::string &relative) {
    if (relative.empty() || relative.find_first_of("\\:\0", 0, 3) != std::string::npos)
        throw Error("path", {{"path", relative}});
    const auto value = path_from_utf8(relative);
    if (value.is_absolute() || value.has_root_name())
        throw Error("path");
    for (const auto &part : value) {
        const auto name = path_to_utf8(part);
        if (name == ".." || name == "." || name.empty() || name.ends_with('.') || name.ends_with(' '))
            throw Error("path", {{"path", relative}});
        const auto stem = lower(path_to_utf8(part.stem()));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && stem[3] >= '1' &&
             stem[3] <= '9'))
            throw Error("path");
    }
    const auto base = fs::weakly_canonical(fs::absolute(root));
    const auto output = fs::weakly_canonical(base / value);
    const auto within = output.lexically_relative(base);
    if (within.empty() || *within.begin() == "..")
        throw Error("path");
    return base / value;
}
Store::Store(fs::path root, PackageTypes types, AvailableSpace available)
    : root_(fs::absolute(std::move(root)).lexically_normal()), types_(std::move(types)),
      available_(std::move(available)) {}
Json Store::state() const { return loadState(root_); }
std::string Store::cachedCatalog() const {
    const auto data = state();
    return data.contains("catalog") ? data.at("catalog").dump() : std::string();
}
void Store::rememberCatalog(const std::string &envelope, const Catalog &catalog) {
    Lock lock(root_);
    auto data = state();
    const auto previous = data.at("highest_catalog_sequence").get<std::uint64_t>();
    const auto signedData = Json::parse(envelope);
    const auto hash = sha256(signedData.at("payload").get<std::string>());
    if (catalog.sequence < previous ||
        (catalog.sequence == previous && data.contains("catalog_sha256") && data.at("catalog_sha256") != hash))
        throw Error("catalog_rollback");
    data["catalog"] = signedData;
    data["highest_catalog_sequence"] = catalog.sequence;
    data["catalog_sha256"] = hash;
    saveState(root_, data);
}
fs::path Store::download(const Package &p, Transport &transport, std::stop_token stop, const Progress &progress) {
    Lock cacheLock(root_, "cache.lock");
    if (p.download_size == 0 || p.download_size > 512ull * 1024 * 1024)
        throw Error("download_size");
    if (p.sha256.size() != 64 || p.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw Error("hash");
    if (stop.stop_requested())
        throw Error("cancelled");
    fs::create_directories(root_ / "cache");
    const auto output = root_ / "cache" / (p.sha256 + ".package");
    if (fs::is_regular_file(output) && fs::file_size(output) == p.download_size && sha256(output) == p.sha256)
        return output;
    if (available_(root_) < p.download_size + 64 * 1024 * 1024)
        throw Error("space");
    Temporary temporary{root_ / "cache" / (p.sha256 + ".download-" + uniqueName())};
    transport.download(p.url, temporary.path, p.download_size, stop, progress);
    if (stop.stop_requested())
        throw Error("cancelled");
    if (fs::file_size(temporary.path) != p.download_size || sha256(temporary.path) != p.sha256)
        throw Error("hash");
    std::error_code ec;
    if (fs::exists(output)) {
        fs::remove(output, ec);
        if (ec)
            throw Error("storage");
    }
    fs::rename(temporary.path, output, ec);
    if (ec && (!fs::is_regular_file(output) || sha256(output) != p.sha256))
        throw Error("storage");
    return output;
}
Compatibility Store::assessInstall(const Package &p, const Environment &environment) const {
    try {
        const auto type = types_.find(p.kind);
        if (type == types_.end())
            throw Error("package_type", {{"kind", p.kind}});
        const auto result = compatible(p, environment);
        if (!result.compatible)
            return result;
        auto data = state();
        auto &installed = data.at("installed");
        if (installed.contains(p.id))
            requireAction(installed.at(p.id), "update");
        if (installed.contains(p.id) && Version::parse(p.version) <= Version::parse(installed.at(p.id).at("version")))
            throw Error("not_newer");
        installed[p.id] = {{"version", p.version}, {"enabled", true}, {"manifest", p.manifest}};
        validateSet(data, type->second->installationEnvironment(p, environment), types_);
        return {};
    } catch (const Error &error) {
        return {false, error.code, error.arguments};
    }
}
void Store::install(const Package &p, const fs::path &archive, const Environment &environment) {
    ActivationGuard activation(root_);
    Lock lock(root_);
    if (p.installed_size == 0 || p.installed_size > 2ull * 1024 * 1024 * 1024)
        throw Error("expanded_size");
    const auto type = types_.find(p.kind);
    if (type == types_.end())
        throw Error("package_type", {{"kind", p.kind}});
    const auto compatibility = compatible(p, environment);
    if (!compatibility.compatible)
        throw Error(compatibility.code, compatibility.arguments);
    if (fs::file_size(archive) != p.download_size || sha256(archive) != p.sha256)
        throw Error("hash");
    if (available_(root_) < p.installed_size + 64 * 1024 * 1024)
        throw Error("space");
    auto data = state();
    auto &installed = data.at("installed");
    if (installed.contains(p.id))
        requireAction(installed.at(p.id), "update");
    if (installed.contains(p.id) && Version::parse(p.version) <= Version::parse(installed.at(p.id).at("version")))
        throw Error("not_newer");
    const auto directory = checkedRelative(root_ / "packages", p.id + '/' + p.version);
    Temporary staging{root_ / (".staging-" + uniqueName())};
    fs::create_directory(staging.path);
    extract(p, archive, staging.path, type->second->allowInternalSymlinks());
    if (readJson(staging.path / "toolbox.json") != p.manifest)
        throw Error("manifest");
    type->second->validate(p, staging.path);
    Json item = {
        {"version", p.version}, {"enabled", true}, {"manifest", p.manifest}, {"directory", path_to_utf8(directory)}};
    if (installed.contains(p.id)) {
        auto previous = installed.at(p.id);
        previous.erase("previous");
        item["previous"] = std::move(previous);
    }
    installed[p.id] = std::move(item);
    auto targetEnvironment = type->second->installationEnvironment(p, environment);
    validateSet(data, targetEnvironment, types_);
    fs::create_directories(directory.parent_path());
    if (fs::exists(directory)) {
        if (readJson(directory / "toolbox.json") != p.manifest)
            throw Error("manifest");
    } else
        commitDirectory(staging.path, directory);
    saveState(root_, data);
}
void Store::setEnabled(const std::string &id, bool enabled) {
    ActivationGuard activation(root_);
    Lock lock(root_);
    auto data = state();
    if (!data.at("installed").contains(id))
        throw Error("not_installed");
    requireAction(data.at("installed").at(id), enabled ? "enable" : "disable");
    data["installed"][id]["enabled"] = enabled;
    validateSet(data, {applicationVersion(), platformId(), {}}, types_);
    saveState(root_, data);
}
void Store::uninstall(const std::string &id) {
    ActivationGuard activation(root_);
    Lock lock(root_);
    auto data = state();
    if (!data.at("installed").contains(id))
        throw Error("not_installed");
    if (data["installed"][id].value("bundled", false))
        throw Error("bundled");
    requireAction(data.at("installed").at(id), "uninstall");
    if (data.contains("bundled") && data["bundled"].contains(id)) {
        const auto manifest = data["bundled"][id];
        data["installed"][id] = {
            {"version", manifest.at("version")}, {"enabled", true}, {"bundled", true}, {"manifest", manifest}};
    } else
        data["installed"].erase(id);
    validateSet(data, {applicationVersion(), platformId(), {}}, types_);
    saveState(root_, data);
}
void Store::rollback(const std::string &id) {
    ActivationGuard activation(root_);
    Lock lock(root_);
    auto data = state();
    auto &installed = data.at("installed");
    if (!installed.contains(id) || !installed.at(id).contains("previous"))
        throw Error("no_previous");
    requireAction(installed.at(id), "rollback");
    auto previous = installed.at(id).at("previous");
    auto current = installed.at(id);
    current.erase("previous");
    previous["previous"] = std::move(current);
    installed[id] = std::move(previous);
    validateSet(data, {applicationVersion(), platformId(), {}}, types_);
    saveState(root_, data);
}
void Store::clearCache() {
    Lock cacheLock(root_, "cache.lock");
    Lock lock(root_);
    std::error_code ec;
    fs::remove_all(root_ / "cache", ec);
    if (ec)
        throw Error("storage");
}
std::vector<ActiveComponent> Store::components(const std::string &kind) const {
    std::vector<ActiveComponent> result;
    const auto data = state();
    std::set<std::string> ids;
    for (const auto &[package, item] : data.at("installed").items())
        if (item.at("enabled").get<bool>() && !item.value("bundled", false) &&
            !item.at("manifest").value("components", Json::array()).empty()) {
            const auto root = path_from_utf8(item.at("directory").get<std::string>());
            if (root != checkedRelative(root_ / "packages", package + '/' + item.at("version").get<std::string>()))
                throw Error("registry");
            for (const auto &component : item.at("manifest").value("components", Json::array()))
                if (kind.empty() || component.at("kind") == kind) {
                    const auto id = component.at("id").get<std::string>();
                    if (!ids.insert(component.at("kind").get<std::string>() + '\n' + id).second)
                        throw Error("duplicate_component", {{"id", id}});
                    result.push_back({package, id, component.at("kind"), item.at("version").get<std::string>(),
                                      checkedRelative(root, component.at("path")), root});
                }
        }
    return result;
}
std::map<std::string, std::string> Store::installedVersions() const {
    std::map<std::string, std::string> result;
    const auto data = state();
    for (const auto &[id, item] : data.at("installed").items())
        if (item.at("enabled").get<bool>())
            result[id] = item.at("version");
    return result;
}
void Store::synchronizeBundled(const std::vector<Json> &manifests) {
    Lock lock(root_);
    auto data = state();
    const auto original = data;
    data["bundled"] = Json::object();
    for (const auto &manifest : manifests) {
        const auto id = manifest.at("id").get<std::string>();
        data["bundled"][id] = manifest;
        if (!data["installed"].contains(id) || data["installed"][id].value("bundled", false)) {
            const bool enabled = data["installed"].contains(id) ? data["installed"][id].value("enabled", true) : true;
            data["installed"][id] = {
                {"version", manifest.at("version")}, {"enabled", enabled}, {"bundled", true}, {"manifest", manifest}};
        }
    }
    if (data != original)
        saveState(root_, data);
}
std::set<std::string> Store::disabledComponents(const std::string &kind) const {
    std::set<std::string> result;
    const auto data = state();
    for (const auto &[id, item] : data.at("installed").items())
        if (!item.at("enabled").get<bool>())
            for (const auto &component : item.at("manifest").value("components", Json::array()))
                if (component.at("kind") == kind)
                    result.insert(component.at("id").get<std::string>());
    return result;
}
void Store::collectUnused() {
    Lock lock(root_);
    std::set<fs::path> retained;
    const auto pointer = root_ / "application/current.json";
    if (fs::is_regular_file(pointer))
        retained.insert(fs::weakly_canonical(path_from_utf8(readJson(pointer).at("root").get<std::string>())));
    const auto data = state();
    for (const auto &[id, item] : data.at("installed").items()) {
        if (item.contains("directory"))
            retained.insert(fs::weakly_canonical(path_from_utf8(item.at("directory").get<std::string>())));
        if (item.contains("previous") && item.at("previous").contains("directory"))
            retained.insert(
                fs::weakly_canonical(path_from_utf8(item.at("previous").at("directory").get<std::string>())));
    }
    const auto runtime = root_ / "runtime";
    if (fs::is_directory(runtime))
        for (const auto &entry : fs::directory_iterator(runtime)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            const auto lease = readJson(entry.path());
            if (processAlive(lease.at("pid")))
                for (const auto &path : lease.at("roots"))
                    retained.insert(fs::weakly_canonical(path_from_utf8(path.get<std::string>())));
            else {
                std::error_code ignored;
                fs::remove(entry.path(), ignored);
            }
        }
    const auto packages = root_ / "packages";
    if (!fs::is_directory(packages))
        return;
    for (const auto &package : fs::directory_iterator(packages)) {
        if (!package.is_directory() || package.is_symlink())
            continue;
        for (const auto &version : fs::directory_iterator(package.path())) {
            if (!version.is_directory() || version.is_symlink())
                continue;
            const auto path = fs::weakly_canonical(version.path());
            const auto within = path.lexically_relative(fs::weakly_canonical(packages));
            if (within.empty() || *within.begin() == "..")
                throw Error("path");
            if (!retained.contains(path)) {
                std::error_code error;
                fs::remove_all(path, error);
                if (error)
                    throw Error("storage");
            }
        }
    }
}
class ActivationGuard::Impl {
  public:
    Lock lock;
    explicit Impl(const fs::path &root) : lock(root / "activation") {}
};
ActivationGuard::ActivationGuard(const fs::path &root) : impl_(std::make_unique<Impl>(root)) {}
ActivationGuard::~ActivationGuard() = default;
} // namespace signalscope::distribution
