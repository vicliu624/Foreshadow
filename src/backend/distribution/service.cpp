#include "signalscope/backend/distribution/distribution.hpp"
#include <chrono>
#include <fstream>
namespace signalscope::distribution {
namespace {
Json intent(Json installed) {
    for (auto &[id, item] : installed.items()) {
        item.erase("previous");
        item.erase("activation");
    }
    return installed;
}
} // namespace
bool Service::restartRequired() const { return intent(store_.state().at("installed")) != intent(loadedInstalled_); }
bool Service::pendingInstallation(const std::string &id) const {
    const auto current = intent(store_.state().at("installed"));
    const auto loaded = intent(loadedInstalled_);
    return current.contains(id) != loaded.contains(id) || (current.contains(id) && current.at(id) != loaded.at(id));
}
Service::Service(Store store, std::shared_ptr<Transport> transport, Authority authority)
    : store_(std::move(store)), transport_(std::move(transport)), authority_(std::move(authority)),
      loadedInstalled_(store_.state().at("installed")) {
    if (!transport_)
        throw Error("network");
}
Catalog Service::check(std::stop_token stop, const Progress &progress) {
    std::filesystem::create_directories(store_.root() / "catalog-transfers");
    const auto temporary = store_.root() / "catalog-transfers" /
                           ("catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{temporary};
    transport_->download(authority_.url, temporary, 8 * 1024 * 1024, stop, progress);
    std::ifstream stream(temporary, std::ios::binary);
    const std::string data{std::istreambuf_iterator<char>(stream), {}};
    auto catalog = Catalog::verify(data, authority_.public_key);
    store_.rememberCatalog(data, catalog);
    return catalog;
}
Catalog Service::cached() const {
    const auto data = store_.cachedCatalog();
    return data.empty() ? Catalog{} : Catalog::verify(data, authority_.public_key);
}
Catalog Service::importCatalog(const std::string &envelope) {
    auto catalog = Catalog::verify(envelope, authority_.public_key);
    store_.rememberCatalog(envelope, catalog);
    return catalog;
}
Environment Service::environment() const { return {applicationVersion(), platformId(), store_.installedVersions()}; }
void Service::install(const Package &p, std::stop_token stop, const Progress &progress,
                      const std::filesystem::path &offlineArchive) {
    const auto assessment = store_.assessInstall(p, environment());
    if (!assessment.compatible)
        throw Error(assessment.code, assessment.arguments);
    const auto archive = offlineArchive.empty() ? store_.download(p, *transport_, stop, progress) : offlineArchive;
    if (stop.stop_requested())
        throw Error("cancelled");
    store_.install(p, archive, environment());
}
} // namespace signalscope::distribution
