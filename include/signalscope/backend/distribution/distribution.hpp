#pragma once
#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace signalscope::distribution {
using Json = nlohmann::json;
using Progress = std::function<void(std::uint64_t, std::uint64_t)>;
struct Version {
    std::uint64_t major = 0, minor = 0, patch = 0;
    std::string prerelease;
    static Version parse(const std::string &);
    std::strong_ordering operator<=>(const Version &) const;
    bool operator==(const Version &) const = default;
};
struct Environment {
    std::string application_version, platform;
    std::map<std::string, std::string> installed;
};
struct Compatibility {
    bool compatible = true;
    std::string code;
    Json arguments = Json::object();
};
struct Package {
    std::string id, kind, version, platform, url, sha256;
    std::uint64_t download_size = 0, installed_size = 0;
    Json manifest, description, notes;
};
struct Catalog {
    std::uint64_t sequence = 0;
    std::string published;
    std::vector<Package> packages;
    static Catalog verify(const std::string &envelope, const std::string &publicKeyHex);
    static Catalog official(const std::string &envelope);
    const Package *latest(const std::string &id, const Environment &) const;
};
Compatibility compatible(const Package &, const Environment &);
std::string sha256(const std::filesystem::path &);
std::string sha256(const std::string &);
std::string officialCatalogUrl();
std::string applicationVersion();
std::string platformId();
std::string applicationPackageId();
struct Authority {
    std::string url, public_key;
};
Authority officialAuthority();
std::filesystem::path defaultRoot();

class Transport {
  public:
    virtual ~Transport() = default;
    virtual void download(const std::string &url, const std::filesystem::path &output, std::uint64_t maximum,
                          std::stop_token = {}, const Progress &progress = {}) = 0;
};
class PackageType {
  public:
    virtual ~PackageType() = default;
    virtual void validate(const Package &, const std::filesystem::path &extracted) const = 0;
    virtual bool allowInternalSymlinks() const { return false; }
    virtual Environment installationEnvironment(const Package &, Environment environment) const { return environment; }
    virtual Compatibility validateInstalled(const Package &package, const Environment &environment) const {
        return compatible(package, environment);
    }
};
using PackageTypes = std::map<std::string, std::shared_ptr<const PackageType>>;
PackageTypes standardPackageTypes();
struct ActiveComponent {
    std::string package_id, id, kind, version;
    std::filesystem::path path, root;
};
class Store {
  public:
    using AvailableSpace = std::function<std::uintmax_t(const std::filesystem::path &)>;
    explicit Store(
        std::filesystem::path root = defaultRoot(), PackageTypes types = standardPackageTypes(),
        AvailableSpace available = [](const auto &path) { return std::filesystem::space(path).available; });
    const std::filesystem::path &root() const { return root_; }
    Json state() const;
    void rememberCatalog(const std::string &envelope, const Catalog &);
    std::string cachedCatalog() const;
    std::filesystem::path download(const Package &, Transport &, std::stop_token = {}, const Progress &progress = {});
    void install(const Package &, const std::filesystem::path &archive, const Environment &);
    Compatibility assessInstall(const Package &, const Environment &) const;
    void setEnabled(const std::string &id, bool);
    void uninstall(const std::string &id);
    void rollback(const std::string &id);
    void clearCache();
    void synchronizeBundled(const std::vector<Json> &manifests);
    void collectUnused();
    std::vector<ActiveComponent> components(const std::string &kind) const;
    std::set<std::string> disabledComponents(const std::string &kind) const;
    std::map<std::string, std::string> installedVersions() const;

  private:
    std::filesystem::path root_;
    PackageTypes types_;
    AvailableSpace available_;
};
class ActivationGuard {
  public:
    explicit ActivationGuard(const std::filesystem::path &root);
    ~ActivationGuard();
    ActivationGuard(const ActivationGuard &) = delete;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
void writeJsonAtomically(const std::filesystem::path &, const Json &);
Json readJson(const std::filesystem::path &);
std::filesystem::path checkedRelative(const std::filesystem::path &root, const std::string &relative);
struct Error : std::runtime_error {
    std::string code;
    Json arguments;
    explicit Error(std::string code, Json arguments = Json::object())
        : std::runtime_error(code), code(std::move(code)), arguments(std::move(arguments)) {}
};
std::vector<ActiveComponent> runtimeComponents(const std::string &kind);
std::set<std::string> runtimeDisabledComponents(const std::string &kind);
std::uint64_t processId();
bool processAlive(std::uint64_t pid);
class Service {
  public:
    explicit Service(Store store, std::shared_ptr<Transport> transport, Authority authority = officialAuthority());
    Catalog check(std::stop_token = {}, const Progress &progress = {});
    Catalog cached() const;
    Catalog importCatalog(const std::string &envelope);
    std::filesystem::path download(const Package &package, std::stop_token stop = {}, const Progress &progress = {}) {
        return store_.download(package, *transport_, stop, progress);
    }
    Environment environment() const;
    void install(const Package &, std::stop_token = {}, const Progress &progress = {},
                 const std::filesystem::path &offlineArchive = {});
    Store &store() { return store_; }
    const Store &store() const { return store_; }
    bool restartRequired() const;
    bool pendingInstallation(const std::string &id) const;
    const Json &loadedInstallations() const { return loadedInstalled_; }

  private:
    Store store_;
    std::shared_ptr<Transport> transport_;
    Authority authority_;
    Json loadedInstalled_;
};
} // namespace signalscope::distribution
