#include "toolbox_services.hpp"
#include "plugins/desktop/research/external_research.hpp"
#include "signalscope/adapters/distribution/network.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/plugin_text.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QNetworkProxyFactory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>
#include <iostream>

namespace signalscope::desktop {
using namespace distribution;
namespace {
QString native(const std::filesystem::path &path) { return QString::fromStdString(path_to_utf8(path)); }
QString cliName() {
#ifdef _WIN32
    return "signalscope.exe";
#else
    return "signalscope";
#endif
}
Json runProbe(const QString &executable, const QStringList &arguments, const QProcessEnvironment &environment) {
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(executable, arguments);
    if (!process.waitForStarted(5000))
        throw Error("components", {{"detail", process.errorString().toStdString()}});
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        throw Error("timeout");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw Error("components", {{"detail", process.readAllStandardError().left(4096).toStdString()}});
    try {
        return Json::parse(process.readAllStandardOutput().toStdString());
    } catch (const std::exception &e) {
        throw Error("components", {{"detail", e.what()}});
    }
}
class CheckedType : public PackageType {
  public:
    CheckedType(std::shared_ptr<const PackageType> base,
                std::function<void(const Package &, const std::filesystem::path &)> check)
        : base_(std::move(base)), check_(std::move(check)) {}
    void validate(const Package &p, const std::filesystem::path &root) const override {
        base_->validate(p, root);
        check_(p, root);
    }
    bool allowInternalSymlinks() const override { return base_->allowInternalSymlinks(); }
    Environment installationEnvironment(const Package &p, Environment env) const override {
        return base_->installationEnvironment(p, std::move(env));
    }
    Compatibility validateInstalled(const Package &p, const Environment &env) const override {
        return base_->validateInstalled(p, env);
    }

  private:
    std::shared_ptr<const PackageType> base_;
    std::function<void(const Package &, const std::filesystem::path &)> check_;
};
} // namespace
QString toolboxText(const std::string &key, const Json &arguments) {
    static const auto catalog = TextCatalog::resource("distribution");
    Json node = {{"message_key", key}, {"arguments", arguments}};
    catalog.resolve(node);
    return pluginText(node.at("message"));
}
QString toolboxError(const std::exception &error) {
    if (const auto *structured = dynamic_cast<const Error *>(&error)) {
        auto arguments = structured->arguments;
        arguments["detail"] = arguments.value("detail", std::string());
        const auto text = toolboxText("error." + structured->code, arguments);
        return text.startsWith("error.") ? toolboxText("error.generic", {{"detail", structured->what()}}) : text;
    }
    return toolboxText("error.generic", {{"detail", error.what()}});
}
std::shared_ptr<Service> makeDistributionService() {
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    auto types = standardPackageTypes();
    types["receiver"] = std::make_shared<CheckedType>(
        types.at("receiver"), [](const Package &package, const std::filesystem::path &root) {
            std::set<std::string> expected;
            for (const auto &component : package.manifest.at("components"))
                expected.insert(component.at("id"));
            for (const auto &component : package.manifest.at("components")) {
                auto environment = QProcessEnvironment::systemEnvironment();
                environment.insert("SIGNALSCOPE_RECEIVER_PLUGIN_PATH",
                                   native(checkedRelative(root, component.at("path")).parent_path()));
                const auto loaded =
                    runProbe(native(application_directory()) + '/' + cliName(), {"receiver-plugins"}, environment);
                bool found = false;
                for (const auto &item : loaded) {
                    if (item.value("status", std::string()) == "loaded") {
                        if (!expected.contains(item.at("id")))
                            throw Error("components");
                        if (item.at("id") == component.at("id"))
                            found = true;
                    }
                }
                if (!found)
                    throw Error("components", {{"detail", loaded.dump()}});
            }
        });
    types["research"] = std::make_shared<CheckedType>(
        types.at("research"), [](const Package &package, const std::filesystem::path &root) {
            for (const auto &component : package.manifest.at("components")) {
                const auto plugin = readResearchPlugin(native(checkedRelative(root, component.at("path"))), "en");
                if (plugin.plugin.id.toStdString() != component.at("id").get<std::string>())
                    throw Error("components");
            }
        });
    types["application"] = std::make_shared<CheckedType>(
        types.at("application"), [](const Package &package, const std::filesystem::path &root) {
            const auto info = runProbe(native(checkedRelative(root, package.manifest.at("entrypoints").at("cli"))),
                                       {"--build-info"}, QProcessEnvironment::systemEnvironment());
            if (info.at("version") != package.version || info.at("platform") != package.platform)
                throw Error("components", {{"detail", info.dump()}});
        });
    Store store(defaultRoot(), std::move(types));
    store.synchronizeBundled(readJson(resource_directory("distribution") / "bundled.json").get<std::vector<Json>>());
    store.collectUnused();
    return std::make_shared<Service>(std::move(store), std::make_shared<NetworkTransport>());
}
bool scheduleDistributionRestart(Service &service, const QStringList &arguments) {
    if (!service.restartRequired())
        return false;
    QStringList retained;
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == "--update-health-request" || arguments[i] == "--update-health-nonce") {
            ++i;
            continue;
        }
        retained << arguments[i];
    }
    Json values = Json::array();
    for (const auto &argument : retained)
        values.push_back(argument.toStdString());
    const auto currentGui = path_from_utf8(QCoreApplication::applicationFilePath().toStdString());
    const auto directory = application_directory();
    auto installationRoot = directory.parent_path();
    for (auto candidate = directory; !candidate.empty() && candidate != candidate.parent_path();
         candidate = candidate.parent_path()) {
        if (std::filesystem::is_regular_file(candidate / "toolbox.json")) {
            const auto manifest = readJson(candidate / "toolbox.json");
            if (manifest.value("kind", std::string()) == "application" &&
                manifest.value("version", std::string()) == applicationVersion()) {
                installationRoot = candidate;
                break;
            }
        }
    }
    Json previous = {{"root", path_to_utf8(installationRoot)},
                     {"version", applicationVersion()},
                     {"executables",
                      {{"gui", path_to_utf8(currentGui)},
                       {"cli", path_to_utf8(directory / path_from_utf8(cliName().toStdString()))}}},
                     {"arguments", values}};
    auto target = previous;
    const auto state = service.store().state();
    const auto id = applicationPackageId();
    if (state.at("installed").contains(id)) {
        const auto &item = state.at("installed").at(id);
        if (item.at("version") != applicationVersion() && item.contains("directory")) {
            if (item.contains("activation"))
                target = item.at("activation");
            else {
                const auto root = path_from_utf8(item.at("directory").get<std::string>());
                const auto &entries = item.at("manifest").at("entrypoints");
                target = {{"root", path_to_utf8(root)},
                          {"version", item.at("version")},
                          {"executables",
                           {{"gui", path_to_utf8(checkedRelative(root, entries.at("gui")))},
                            {"cli", path_to_utf8(checkedRelative(root, entries.at("cli")))}}},
                          {"arguments", values}};
            }
        }
    }
    target["gui_sha256"] = sha256(path_from_utf8(target.at("executables").at("gui").get<std::string>()));
    const auto nonce = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto request = service.store().root() / "updates" / nonce / "request.json";
#ifdef _WIN32
    const auto helper = directory / "signalscope-update.exe";
    const auto launcher = directory / "signalscope-launcher.exe";
#else
    const auto helper = directory / "signalscope-update";
    const auto launcher = directory / "signalscope-launcher";
#endif
    if (!std::filesystem::is_regular_file(helper) || !std::filesystem::is_regular_file(launcher))
        throw Error("activation");
    Json transaction = {{"schema", 1},
                        {"store", path_to_utf8(service.store().root())},
                        {"application_id", id},
                        {"parent_pid", processId()},
                        {"nonce", nonce},
                        {"previous", previous},
                        {"target", target},
                        {"old_installed", service.loadedInstallations()},
                        {"target_installed", state.at("installed")},
                        {"launcher", path_to_utf8(launcher)}};
#ifdef _WIN32
    transaction["gui_launcher"] = path_to_utf8(directory / "signalscope-launcher-gui.exe");
#endif
    writeJsonAtomically(request, transaction);
    qint64 pid = 0;
    if (!QProcess::startDetached(native(helper), {"--request", native(request)}, native(directory), &pid))
        throw Error("activation");
    return true;
}
void confirmDistributionStartup(const QStringList &arguments) {
    const auto flag = arguments.indexOf("--update-health-request");
    const auto nonceAt = arguments.indexOf("--update-health-nonce");
    if (flag < 0)
        return;
    if (flag + 1 >= arguments.size() || nonceAt < 0 || nonceAt + 1 >= arguments.size())
        throw Error("activation");
    const auto requestPath = path_from_utf8(arguments[flag + 1].toStdString());
    const auto request = readJson(requestPath);
    const auto within = std::filesystem::weakly_canonical(requestPath)
                            .lexically_relative(std::filesystem::weakly_canonical(defaultRoot() / "updates"));
    if (within.empty() || *within.begin() == ".." || request.at("nonce") != arguments[nonceAt + 1].toStdString() ||
        request.at("target").at("version") != applicationVersion())
        throw Error("activation");
    const auto nonce = request.at("nonce");
    QTimer::singleShot(750, QCoreApplication::instance(), [requestPath, nonce] {
        try {
            writeJsonAtomically(requestPath.parent_path() / "health.json",
                                {{"nonce", nonce}, {"version", applicationVersion()}, {"pid", processId()}});
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            QCoreApplication::exit(1);
        }
    });
}
} // namespace signalscope::desktop
