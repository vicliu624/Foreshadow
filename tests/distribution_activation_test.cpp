#include "distribution_fixture.hpp"
#include <QDir>
#include <QThread>
using namespace signalscope;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    try {
        if (arguments.contains("--fixture-echo")) {
            Json values = Json::array();
            for (const auto &value : arguments.mid(2))
                values.push_back(value.toStdString());
            std::cout << values.dump();
            return 7;
        }
        if (arguments.contains("--fixture-old")) {
            writeJsonAtomically(path_from_utf8(arguments.at(2).toStdString()), {{"recovered", true}});
            return 0;
        }
        if (arguments.contains("--fixture-target") || arguments.contains("--fixture-target-crash")) {
            const auto at = arguments.indexOf("--update-health-request");
            const auto path = path_from_utf8(arguments.at(at + 1).toStdString());
            const auto request = readJson(path);
            writeJsonAtomically(path.parent_path() / "health.json",
                                {{"nonce", request.at("nonce")}, {"version", request.at("target").at("version")}, {"pid", processId()}});
            QThread::msleep(arguments.contains("--fixture-target-crash") ? 100 : 2500);
            return 0;
        }
#if defined(__linux__)
        {
            const auto home = root / path_from_utf8("用户 launch options 📡");
            std::filesystem::create_directories(home);
            runner.setEnvironment("HOME", QString::fromStdString(path_to_utf8(home)));
            const auto storeRoot = root / "preserve-launch-options";
            Store store(storeRoot);
            const auto request = storeRoot / "updates/custom-launch/request.json";
            const auto desktopEntry = home / ".local/share/applications/signalscope.desktop";
            Json target = {{"root", path_to_utf8(root)}, {"version", "0.2.1"},
                           {"executables", {{"gui", path_to_utf8(self)}, {"cli", path_to_utf8(self)}}},
                           {"gui_sha256", sha256(self)}, {"arguments", Json::array({"--fixture-target"})}};
            Json transaction = {{"schema", 1}, {"store", path_to_utf8(storeRoot)},
                                {"application_id", "org.amc.ams"}, {"nonce", "fresh-launch"},
                                {"parent_pid", std::uint64_t(0)}, {"previous", target}, {"target", target},
                                {"old_installed", Json::object()}, {"target_installed", Json::object()},
                                {"launcher", arguments.at(2).toStdString()}, {"health_timeout_seconds", 2}};
            writeJsonAtomically(request, transaction);
            runner.run(arguments.at(1), {"--request", QString::fromStdString(path_to_utf8(request))});
            require(amc_test::read(QString::fromStdString(path_to_utf8(desktopEntry))).contains("Name=Foreshadow"),
                    "Fresh activation did not create its desktop entry");
            const QByteArray customEntry = "[Desktop Entry]\nType=Application\nName=Foreshadow\n"
                                           "Exec=\"/user launch wrapper\" --profile \"user options\"\n"
                                           "Icon=applications-science\nTerminal=false\n";
            amc_test::write(QString::fromStdString(path_to_utf8(desktopEntry)), customEntry);
            transaction["previous"] = target;
            transaction["target"]["version"] = "0.2.2";
            transaction["nonce"] = "preserve-launch";
            writeJsonAtomically(request, transaction);
            runner.run(arguments.at(1), {"--request", QString::fromStdString(path_to_utf8(request))});
            require(readJson(storeRoot / "application/current.json").at("version") == "0.2.2" &&
                    amc_test::read(QString::fromStdString(path_to_utf8(desktopEntry))) == customEntry &&
                    readJson(storeRoot / "application/bin/signalscope-gui.json").at("role") == "gui",
                    "Activation discarded custom launch options or failed to update the stable bootstrap");
        }
#endif
        if (arguments.contains("--fixture-fail"))
            return 5;
        require(arguments.size() == 3, "Updater/launcher test executables missing");
        QTemporaryDir temporary;
        const auto root = path_from_utf8(temporary.path().toStdString()) / path_from_utf8("更新 with spaces");
        std::filesystem::create_directories(root);
        const auto self = path_from_utf8(QCoreApplication::applicationFilePath().toStdString());
        amc_test::ChildRunner runner(temporary.path());
        for (const auto *scenario :
             {"success", "startup-failure", "post-health-failure", "hash-failure", "state-conflict"}) {
            const auto storeRoot = root / scenario;
            Store store(storeRoot);
            const Json installed = Json::object();
            const Json previous = {
                {"root", path_to_utf8(root)},
                {"version", "0.2.0"},
                {"executables", {{"gui", path_to_utf8(self)}, {"cli", path_to_utf8(self)}}},
                {"arguments", Json::array({"--fixture-old", path_to_utf8(storeRoot / "recovered.json")})}};
            const Json target = {
                {"root", path_to_utf8(root)},
                {"version", "0.2.1"},
                {"executables", {{"gui", path_to_utf8(self)}, {"cli", path_to_utf8(self)}}},
                {"gui_sha256", std::string(scenario) == "hash-failure" ? std::string(64, '0') : sha256(self)},
                {"arguments", Json::array({std::string(scenario) == "startup-failure"       ? "--fixture-fail"
                                           : std::string(scenario) == "post-health-failure" ? "--fixture-target-crash"
                                                                                            : "--fixture-target"})}};
            const auto request = storeRoot / "updates/test/request.json";
            writeJsonAtomically(
                request,
                {{"schema", 1},
                 {"store", path_to_utf8(storeRoot)},
                 {"application_id", "org.amc.ams"},
                 {"nonce", "fixture"},
                 {"parent_pid", std::uint64_t(0)},
                 {"previous", previous},
                 {"target", target},
                 {"old_installed", installed},
                 {"target_installed", std::string(scenario) == "state-conflict" ? Json{{"changed", true}} : installed},
                 {"skip_shortcuts", true},
                 {"health_timeout_seconds", 2}});
            const auto code = std::string(scenario) == "success"          ? 0
                              : std::string(scenario) == "state-conflict" ? 2
                                                                          : 1;
            runner.run(arguments.at(1), {"--request", QString::fromStdString(path_to_utf8(request))}, code);
            const auto result = readJson(request.parent_path() / "result.json");
            require(result.at("state") == (code == 0   ? "applied"
                                           : code == 2 ? "failed"
                                                       : "rolled_back"),
                    "Unexpected activation result");
            if (code == 0)
                require(readJson(storeRoot / "application/current.json").at("version") == "0.2.1",
                        "Application pointer was not switched");
            else {
                for (int attempt = 0; attempt < 100 && !std::filesystem::exists(storeRoot / "recovered.json");
                     ++attempt)
                    QThread::msleep(20);
                require(std::filesystem::exists(storeRoot / "recovered.json"),
                        "Failed activation did not relaunch previous application");
                require(store.state().at("installed") == installed, "Failed activation corrupted package state");
            }
        }
#ifdef _WIN32
        const auto launcher = root / "bootstrap.exe";
#else
        const auto launcher = root / "bootstrap";
#endif
        std::filesystem::copy_file(path_from_utf8(arguments.at(2).toStdString()), launcher);
        writeJsonAtomically(path_from_utf8(path_to_utf8(launcher) + ".json"),
                            {{"root", path_to_utf8(root / "bootstrap-store")}, {"role", "cli"}});
        writeJsonAtomically(root / "bootstrap-store/current.json", {{"executables", {{"cli", path_to_utf8(self)}}}});
        const QStringList inputs = {"--fixture-echo", QString::fromUtf8("中文 参数"), "with \"quotes\"", "trailing\\",
                                    ""};
        const auto output =
            Json::parse(runner.run(QString::fromStdString(path_to_utf8(launcher)), inputs, 7).toStdString());
        require(output == Json::array({"中文 参数", "with \"quotes\"", "trailing\\", ""}),
                "Bootstrap changed arguments or exit status");
        std::cout << "Activation, health failure rollback, hash rejection, concurrent state rejection and Unicode "
                     "bootstrap passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
