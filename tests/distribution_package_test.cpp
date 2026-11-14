#include "distribution_fixture.hpp"
using namespace signalscope;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(argc == 3 || argc == 4, "Expected unsigned release entries, staged CLI and optional private review store");
        const auto entriesFile = path_from_utf8(app.arguments().at(1).toStdString());
        const auto entries = readJson(entriesFile);
        Signing signing;
        const auto catalog = Catalog::verify(
            signing.sign({{"schema", 1}, {"sequence", 1u}, {"published", "fixture"}, {"packages", entries}}),
            signing.publicKey);
        QTemporaryDir temporary;
        amc_test::ChildRunner runner(temporary.path());
        const bool persist=argc==4;
        const auto root = persist?path_from_utf8(app.arguments().at(3).toStdString()):
            path_from_utf8((temporary.path()+QStringLiteral("/\u5b89\u88c5 package \U0001f4e1")).toStdString());
        if(persist)require(!std::filesystem::exists(root),"Use a fresh private review toolbox store");
        for (const auto &package : catalog.packages) {
            if(persist && package.kind=="application")continue;
            Store store(persist?root:root / package.id);
            store.install(package, entriesFile.parent_path() / package.url.substr(package.url.rfind('/') + 1),
                          {applicationVersion(), platformId(), {}});
            const auto installed =
                path_from_utf8(store.state().at("installed").at(package.id).at("directory").get<std::string>());
            if (package.kind == "application") {
                const auto cli = checkedRelative(installed, package.manifest.at("entrypoints").at("cli"));
                require(Json::parse(runner.run(QString::fromStdString(path_to_utf8(cli)), {"--build-info"}))
                                .at("version") == package.version,
                        "Packaged application did not report its declared version");
                amc_test::ChildRunner portable(temporary.path());
                portable.setEnvironment("SIGNALSCOPE_DISTRIBUTION_ROOT",
                                        QString::fromStdString(path_to_utf8(root / "portable-state")));
                const auto diagnostics = Json::parse(portable.run(QString::fromStdString(path_to_utf8(cli)), {"receiver-plugins"}));
                bool loaded = false;
                for (const auto &item : diagnostics) {
                    require(item.value("status", std::string()) != "rejected", "Portable application rejected a bundled receiver in a Unicode path");
                    loaded = loaded || item.value("status", std::string()) == "loaded";
                }
                require(loaded, "Portable application did not discover bundled receivers in a Unicode path");
            }
            if (package.kind == "receiver") {
                runner.setEnvironment("SIGNALSCOPE_DISTRIBUTION_ROOT",
                                      QString::fromStdString(path_to_utf8(store.root())));
                const auto diagnostics = Json::parse(runner.run(app.arguments().at(2), {"receiver-plugins"}));
                for (const auto &component : package.manifest.at("components")) {
                    bool loaded = false;
                    for (const auto &item : diagnostics)
                        if (item.value("status", std::string()) == "loaded" && item.at("id") == component.at("id") &&
                            !item.at("toolbox").is_null())
                            loaded = true;
                    require(loaded, "Native receiver from independent release package could not load");
                    amc_test::ChildRunner preflight(temporary.path());
                    preflight.setEnvironment("SIGNALSCOPE_RECEIVER_PLUGIN_PATH", QString::fromStdString(path_to_utf8(
                        checkedRelative(installed, component.at("path")).parent_path())));
                    const auto probed = Json::parse(preflight.run(app.arguments().at(2), {"receiver-plugins"}));
                    bool checked = false;
                    for (const auto &item : probed)
                        if (item.value("status", std::string()) == "loaded" && item.at("id") == component.at("id"))
                            checked = true;
                    require(checked, "Unregistered receiver candidate failed native preflight before activation");
                }
            }
        }
        std::cout << "All official release packages passed the real archive transaction and native "
                     "application/receiver loading.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
