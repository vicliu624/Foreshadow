#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <variant>

namespace signalscope::pager {
struct Status {};
struct Stop {};

struct Packet {
    qint64 id, length;
};

struct LoRaSettings {
    qint64 bandwidth, sf, cr, power, preamble, sync;
    bool inverted;
};

struct Transmission {
    Packet packet;
    qint64 frequency;
    std::optional<LoRaSettings> lora;
    std::optional<qint64> sendBeforeUnixMs;
};

struct AmplitudeTest {
    QByteArray modulation;
    qint64 frequency;
};

using Command=std::variant<Status,Stop,Transmission,AmplitudeTest>;

QJsonObject runCommand(const QString& portName,const Command& command);
}
