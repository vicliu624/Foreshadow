#include "distribution_fixture.hpp"
using namespace signalscope;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(argc == 3, "CLI and third native receiver are required");
        QTemporaryDir temporary;
        const auto root = path_from_utf8(temporary.path().toStdString());
        const auto module = path_from_utf8(app.arguments().at(2).toStdString());
        std::vector<Entry> entries = {
            {"receiver/" + module.filename().string(), amc_test::read(app.arguments().at(2)).toStdString()}};
        for (const auto &file :
             std::filesystem::recursive_directory_iterator(module.parent_path() / "amc-receiver-test.i18n"))
            if (file.is_regular_file())
                entries.push_back({"receiver/amc-receiver-test.i18n/" + file.path().filename().string(),
                                   amc_test::read(QString::fromStdString(path_to_utf8(file.path()))).toStdString()});
        auto description = manifest("example.independent-receiver", "1.0.0", "receiver");
        description["components"] = Json::array({{{"kind", "receiver"},
                                                  {"id", "test.receiver"},
                                                  {"abi", 1},
                                                  {"path", "receiver/" + module.filename().string()}}});
        Store store(root / "store");
        amc_test::ChildRunner runner(temporary.path());
        runner.setEnvironment("SIGNALSCOPE_DISTRIBUTION_ROOT", QString::fromStdString(path_to_utf8(store.root())));
        const auto cli = app.arguments().at(1);
        const auto probe = [&](const char *version) {
            const auto loaded = Json::parse(runner.run(cli, {"receiver-plugins"}));
            bool found = false;
            for (const auto &item : loaded)
                if (item.value("id", std::string()) == "test.receiver" &&
                    item.value("status", std::string()) == "loaded") {
                    require(version && item.at("toolbox").at("version") == version,
                            "Fresh process loaded wrong managed toolbox version");
                    found = true;
                }
            require(found == (version != nullptr), "Managed third receiver discovery/disable/uninstall failed");
        };
        Environment environment{applicationVersion(), platformId(), {}};
        for (const auto *version : {"1.0.0", "1.1.0"}) {
            description["version"] = version;
            const auto p = package(root / "native.tgz", description, entries);
            store.install(p, root / "native.tgz", environment);
            probe(version);
        }
        store.rollback(description.at("id"));
        probe("1.0.0");
        store.setEnabled(description.at("id"), false);
        probe(nullptr);
        store.setEnabled(description.at("id"), true);
        probe("1.0.0");
        const auto activeRoot = store.components("receiver").at(0).root;
        writeJsonAtomically(store.root() / "runtime/test-lease.json",
                            {{"pid", processId()}, {"roots", Json::array({path_to_utf8(activeRoot)})}});
        store.uninstall(description.at("id"));
        probe(nullptr);
        store.collectUnused();
        require(std::filesystem::exists(activeRoot), "Garbage collection removed a live native module");
        std::filesystem::remove(store.root() / "runtime/test-lease.json");
        store.collectUnused();
        require(!std::filesystem::exists(activeRoot), "Unreferenced native module was never collected");
        std::cout << "Independent third receiver installed, updated, rolled back, disabled and uninstalled across "
                     "fresh processes; leases preserved live modules.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
