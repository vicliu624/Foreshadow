#include "signalscope/backend/iq/path.hpp"
#include "toolbox_services.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace signalscope::distribution;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("SignalScope");
    app.setApplicationName("AMS");
    try {
        const auto args = app.arguments();
        if (args.size() < 2) {
            std::cout << "signalscope-toolboxes check|list|install ID|enable ID|disable ID|uninstall ID|rollback ID\n";
            return 0;
        }
        auto service = signalscope::desktop::makeDistributionService();
        const auto command = args.at(1);
        if (command == "check") {
            const auto catalog = service->check();
            Json entries = Json::array();
            for (const auto &package : catalog.packages) {
                const auto status = compatible(package, service->environment());
                entries.push_back({{"id", package.id},
                                   {"version", package.version},
                                   {"platform", package.platform},
                                   {"compatible", status.compatible},
                                   {"reason", status.code}});
            }
            std::cout << Json{{"sequence", catalog.sequence}, {"packages", entries}}.dump(2) << '\n';
            return 0;
        }
        if (command == "list") {
            std::cout << service->store().state().dump(2) << '\n';
            return 0;
        }
        if (args.size() != 3)
            throw Error("arguments");
        const auto id = args.at(2).toStdString();
        if (command == "install") {
            const auto catalog = service->check();
            const auto *package = catalog.latest(id, service->environment());
            if (!package)
                throw Error("not_available", {{"id", id}});
            service->install(*package);
        } else if (command == "enable" || command == "disable")
            service->store().setEnabled(id, command == "enable");
        else if (command == "uninstall")
            service->store().uninstall(id);
        else if (command == "rollback")
            service->store().rollback(id);
        else
            throw Error("arguments");
        std::cout << Json{{"restart_required", service->restartRequired()},
                          {"installed", service->store().state().at("installed")}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const Error &error) {
        std::cerr << Json{{"error", error.code}, {"arguments", error.arguments}}.dump() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << Json{{"error", error.what()}}.dump() << '\n';
        return 1;
    }
}
