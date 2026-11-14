#include "distribution_fixture.hpp"
#include "signalscope/adapters/distribution/network.hpp"
#include "toolbox_center.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
using namespace signalscope::desktop;
namespace {
template <class F> void until(F predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        require(timer.elapsed() < 10000, "Asynchronous toolbox action timed out");
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        const auto root = signalscope::path_from_utf8(temporary.path().toStdString());
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost), "Loopback fixture server failed");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    const auto request = socket->readAll();
                    if (request.contains("/stall"))
                        return;
                    if (request.contains("/missing"))
                        socket->write("HTTP/1.1 404 Missing\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    else
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const auto address = "http://127.0.0.1:" + std::to_string(server.serverPort());
        NetworkTransport production, network(true, 150);
        rejected([&] { production.download(address, root / "denied", 10); }, "https");
        network.download(address, root / "ok", 5);
        require(amc_test::read(temporary.path() + "/ok") == "hello", "Network stream was corrupted");
        rejected([&] { network.download(address, root / "large", 2); }, "download_size");
        rejected([&] { network.download(address + "/missing", root / "missing", 10); }, "network");
        rejected([&] { network.download(address + "/stall", root / "timeout", 10); }, "timeout");
        std::stop_source cancel;
        QTimer::singleShot(10, &app, [&] { cancel.request_stop(); });
        rejected([&] { network.download(address + "/stall", root / "cancel", 10, cancel.get_token()); }, "cancelled");
        const auto first = package(root / "first.tgz", manifest("example.research", "1.0.0"));
        Signing signing;
        auto files = std::make_shared<Files>();
        files->source = root / "first.tgz";
        auto service = std::make_shared<Service>(Store(root / "store"), files,
                                                 Authority{"https://fixture.invalid", signing.publicKey});
        service->importCatalog(signing.sign(catalogData(first)));
        bool allowed = false;
        ToolboxCenter center(service, [&] { return allowed; }, {});
        center.show();
        auto *table = center.findChild<QTableWidget *>("toolbox-list");
        auto *install = center.findChild<QPushButton *>("toolbox-install");
        auto *enable = center.findChild<QPushButton *>("toolbox-enable");
        require(table && table->rowCount() == 1 && install && install->isEnabled(),
                "Verified catalog did not appear in toolbox center");
        install->click();
        until([&] { return !center.busy(); });
        require(service->store().installedVersions().empty(), "Active tasks did not block toolbox installation");
        allowed = true;
        install->click();
        until([&] { return !center.busy() && !service->store().installedVersions().empty(); });
        require(service->restartRequired() && table->item(0, 2)->text() != table->item(0, 1)->text(),
                "Pending and loaded versions were not distinguished");
        enable->click();
        until([&] { return !center.busy(); });
        require(service->store().disabledComponents("research").contains("fixture.example.research"),
                "Disable action did not update toolbox state");
        enable->click();
        until([&] { return !center.busy(); });
        require(!service->store().components("research").empty(), "Enable action did not restore toolbox components");
        center.resize(1140, 680);
        if (argc > 1)
            center.grab().save(QString::fromLocal8Bit(argv[1]));
        std::cout << "Network streaming, HTTP failures, timeout, cancellation and guarded desktop "
                     "install/disable/enable passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
