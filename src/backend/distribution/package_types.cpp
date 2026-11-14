#include "signalscope/backend/distribution/distribution.hpp"
namespace signalscope::distribution {
namespace {
template <class Validate>
void components(const Package &p, const std::filesystem::path &root, const std::string &kind, Validate validate) {
    if (p.kind != kind || !p.manifest.at("components").is_array() || p.manifest.at("components").empty())
        throw Error("components");
    std::set<std::string> ids;
    for (const auto &component : p.manifest.at("components")) {
        if (component.at("kind") != kind || !ids.insert(component.at("id")).second)
            throw Error("components");
        const auto path = checkedRelative(root, component.at("path"));
        if (!std::filesystem::is_regular_file(path))
            throw Error("components");
        validate(component, path);
    }
}
class Receiver final : public PackageType {
    // Native bundles may contain framework version links. The extractor still
    // confines every link and its resolved target to this package.
    bool allowInternalSymlinks() const override { return true; }
    void validate(const Package &p, const std::filesystem::path &root) const override {
        components(p, root, "receiver", [](const Json &component, const std::filesystem::path &path) {
            const auto ext = path.extension();
            if (component.at("abi") != 1 || (ext != ".so" && ext != ".dll" && ext != ".dylib"))
                throw Error("components");
        });
    }
};
class Research final : public PackageType {
    void validate(const Package &p, const std::filesystem::path &root) const override {
        components(p, root, "research", [](const Json &component, const std::filesystem::path &path) {
            const auto manifest = readJson(path);
            if ((manifest.at("api") != 1 && manifest.at("api") != 2) || manifest.at("id") != component.at("id") ||
                !std::filesystem::is_regular_file(checkedRelative(path.parent_path(), manifest.at("executable"))))
                throw Error("components");
        });
    }
};
class Application final : public PackageType {
    void validate(const Package &p, const std::filesystem::path &root) const override {
        for (const auto *entry : {"gui", "cli", "updater", "launcher"})
            if (!std::filesystem::is_regular_file(checkedRelative(root, p.manifest.at("entrypoints").at(entry))))
                throw Error("components");
    }
    bool allowInternalSymlinks() const override { return true; }
    Environment installationEnvironment(const Package &p, Environment env) const override {
        env.application_version = p.version;
        return env;
    }
    Compatibility validateInstalled(const Package &, const Environment &) const override { return {}; }
};
class Builtin final : public PackageType {
    void validate(const Package &, const std::filesystem::path &) const override { throw Error("bundled"); }
};
} // namespace
PackageTypes standardPackageTypes() {
    return {{"receiver", std::make_shared<Receiver>()},
            {"research", std::make_shared<Research>()},
            {"application", std::make_shared<Application>()},
            {"builtin", std::make_shared<Builtin>()}};
}
} // namespace signalscope::distribution
