#include "signalscope/adapters/distribution/network.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace signalscope::distribution {
void NetworkTransport::download(const std::string &address, const std::filesystem::path &output, std::uint64_t maximum,
                                std::stop_token stop, const Progress &progress) {
    const QUrl url(QString::fromStdString(address));
    const bool loopback = loopback_ && url.scheme() == "http" &&
                          (url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1");
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || (url.scheme() != "https" && !loopback))
        throw Error("https");
    if (stop.stop_requested())
        throw Error("cancelled");
    QFile file(QString::fromStdString(path_to_utf8(output)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw Error("storage");
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Foreshadow/" + QString::fromStdString(applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = manager.get(request);
    QEventLoop loop;
    QTimer idle, cancel;
    idle.setSingleShot(true);
    idle.setInterval(timeout_);
    cancel.setInterval(50);
    bool exceeded = false, timedOut = false, writeFailed = false;
    std::uint64_t received = 0;
    QObject::connect(&idle, &QTimer::timeout, reply, [&] {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(&cancel, &QTimer::timeout, reply, [&] {
        if (stop.stop_requested())
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&] {
        const auto bytes = reply->readAll();
        idle.start();
        if (static_cast<std::uint64_t>(bytes.size()) > maximum - received) {
            exceeded = true;
            reply->abort();
            return;
        }
        received += static_cast<std::uint64_t>(bytes.size());
        if (file.write(bytes) != bytes.size()) {
            writeFailed = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [&](qint64 current, qint64 total) {
        if (current > static_cast<qint64>(maximum)) {
            exceeded = true;
            reply->abort();
        }
        if (progress)
            try {
                progress(static_cast<std::uint64_t>(std::max<qint64>(0, current)),
                         static_cast<std::uint64_t>(std::max<qint64>(0, total)));
            } catch (...) {
                writeFailed = true;
                reply->abort();
            }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    idle.start();
    cancel.start();
    loop.exec();
    if (!file.flush())
        writeFailed = true;
    file.close();
    const auto error = reply->error();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto detail = reply->errorString().toStdString();
    if (stop.stop_requested())
        throw Error("cancelled");
    if (exceeded)
        throw Error("download_size");
    if (timedOut)
        throw Error("timeout");
    if (writeFailed)
        throw Error("storage");
    if (error != QNetworkReply::NoError || status < 200 || status >= 300)
        throw Error("network", {{"detail", detail}, {"status", status}});
}
} // namespace signalscope::distribution
