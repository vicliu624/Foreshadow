#include "session.hpp"

#include <QDeadlineTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QSerialPort>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace signalscope::pager {
namespace {
constexpr auto maxReplyBytes = 4096;

bool supportedFirmware(const QJsonObject& reply) {
    const auto firmware = reply.value("firmware").toString();
    return firmware == "amc-pager-tx-0.1" || firmware == "amc-pager-tx-0.2" ||
           firmware == "amc-pager-tx-0.3";
}

void sendCommand(QSerialPort& port, const QByteArray& command) {
    if (port.write(command) != command.size()) throw std::runtime_error(port.errorString().toStdString());
    QDeadlineTimer deadline(2000);
    while (port.bytesToWrite() > 0) {
        if (deadline.hasExpired() || !port.waitForBytesWritten(static_cast<int>(deadline.remainingTime())))
            throw std::runtime_error("Could not send Pager command: " + port.errorString().toStdString());
    }
}

QJsonObject readReply(QSerialPort& port, QByteArray& buffer, QDeadlineTimer deadline, const QStringList& kinds,
                      std::optional<qint64> identifier = {}) {
    while (!deadline.hasExpired()) {
        const auto newline = buffer.indexOf('\n');
        if (newline >= 0) {
            const auto reply = QJsonDocument::fromJson(buffer.left(newline)).object();
            buffer.remove(0, newline + 1);
            if (reply.value("ok") == QJsonValue(false))
                throw std::runtime_error("Pager rejected the command: " +
                                         QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString());
            if (!kinds.contains(reply.value("kind").toString())) continue;
            if (identifier && reply.value("id").toInteger(-1) != *identifier) continue;
            if (reply.value("ok") != QJsonValue(true)) throw std::runtime_error("Invalid Pager reply");
            return reply;
        }
        if (buffer.size() >= maxReplyBytes) throw std::runtime_error("Pager reply exceeded 4096 bytes");
        if (port.bytesAvailable() == 0 && !port.waitForReadyRead(static_cast<int>(deadline.remainingTime()))) {
            if (port.error() == QSerialPort::TimeoutError) return {};
            throw std::runtime_error(port.errorString().toStdString());
        }
        buffer += port.read(maxReplyBytes - buffer.size());
    }
    return {};
}

QJsonObject queryStatus(QSerialPort& port, QByteArray& buffer) {
    QDeadlineTimer deadline(8000);
    while (!deadline.hasExpired()) {
        sendCommand(port, "PING\n");
        const auto remaining = std::clamp<qint64>(deadline.remainingTime(), 0, 750);
        const auto reply = readReply(port, buffer, QDeadlineTimer(remaining), {"status"});
        if (reply.isEmpty()) continue;
        if (!supportedFirmware(reply)) throw std::runtime_error("Unrecognized AMC Pager firmware");
        return reply;
    }
    throw std::runtime_error("Timed out waiting for Pager status");
}

void resetInput(QSerialPort& port, QByteArray& buffer) {
    buffer.clear();
    if (!port.clear(QSerialPort::Input)) throw std::runtime_error(port.errorString().toStdString());
}

QJsonObject configureLoRa(QSerialPort& port, QByteArray& buffer, qint64 frequency,
                          const LoRaSettings& settings, bool reportsMode) {
    const QJsonObject expected{
        {"frequency_hz",frequency},{"bandwidth_hz",settings.bandwidth},{"sf",settings.sf},
        {"cr",settings.cr},{"power_dbm",settings.power},{"preamble",settings.preamble},
        {"sync",settings.sync},{"inverted",settings.inverted}
    };
    QByteArray command("SET");
    for (const auto value : {frequency,settings.bandwidth,settings.sf,settings.cr,settings.power,
                            settings.preamble,settings.sync,qint64(settings.inverted)}) {
        command += ' ';
        command += QByteArray::number(value);
    }
    resetInput(port,buffer);
    sendCommand(port, command+'\n');
    if (reportsMode && readReply(port,buffer,QDeadlineTimer(2000),{"mode"}).value("modulation")!="lora")
        throw std::runtime_error("Pager did not confirm LoRa mode");
    const auto configured=readReply(port,buffer,QDeadlineTimer(2000),{"status"});
    if (!configured.value("radio_ready").toBool()) throw std::runtime_error("Pager configuration was not confirmed");
    for (auto value=expected.begin(); value!=expected.end(); ++value)
        if (configured.value(value.key())!=value.value()) throw std::runtime_error("Pager configuration readback mismatch");
    return configured;
}

QJsonObject configureGfsk(QSerialPort& port, QByteArray& buffer, qint64 frequency) {
    resetInput(port,buffer);
    sendCommand(port,"GFSK "+QByteArray::number(frequency)+'\n');
    if (readReply(port,buffer,QDeadlineTimer(2000),{"mode"}).value("modulation")!="gfsk")
        throw std::runtime_error("Pager did not confirm GFSK mode");
    auto configured=readReply(port,buffer,QDeadlineTimer(2000),{"status"});
    if (!configured.value("radio_ready").toBool() || configured.value("frequency_hz").toInteger(-1)!=frequency ||
        configured.value("power_dbm").toInteger(0)!=-9)
        throw std::runtime_error("Pager GFSK configuration readback mismatch");
    configured.insert("modulation","gfsk");
    configured.insert("symbol_rate",2400);
    configured.insert("deviation_hz",4800);
    return configured;
}

QByteArray packetPayload(const Packet& packet) {
    QByteArray bytes("AMC1");
    auto random=static_cast<std::uint32_t>(packet.id);
    for (unsigned shift=0; shift<32; shift+=8) bytes.append(static_cast<char>((random>>shift)&255));
    random ^= 0x9e3779b9u;
    while (bytes.size()<packet.length) {
        random ^= random<<13;
        random ^= random>>17;
        random ^= random<<5;
        bytes.append(static_cast<char>(random&255));
    }
    return bytes;
}

QJsonObject sendPacket(QSerialPort& port, QByteArray& buffer, const Transmission& transmission,
                       const QJsonObject& configured) {
    const auto& packet=transmission.packet;
    const auto bytes=packetPayload(packet);
    std::uint32_t hash=2166136261u;
    for (const auto byte : bytes) hash=(hash^static_cast<unsigned char>(byte))*16777619u;
    const auto requested=std::chrono::system_clock::now();
    const auto requestMilliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(requested.time_since_epoch());
    if (transmission.sendBeforeUnixMs && requestMilliseconds.count()>=*transmission.sendBeforeUnixMs)
        throw std::runtime_error("Pager transmission deadline expired before TX");
    const auto startedWaiting=std::chrono::steady_clock::now();
    try {
        sendCommand(port,"TX "+QByteArray::number(packet.id)+' '+QByteArray::number(packet.length)+'\n');
        auto started=readReply(port,buffer,QDeadlineTimer(3000),{"tx_started"},packet.id);
        if (started.isEmpty()) {
            const auto recovered=queryStatus(port,buffer);
            if (configured.value("started_count").toInteger(-1)<0 ||
                recovered.value("last_started_id").toInteger(-1)!=packet.id ||
                recovered.value("started_count").toInteger(-1)<=configured.value("started_count").toInteger(-1))
                throw std::runtime_error("Pager start could not be confirmed");
            started={{"ok",true},{"kind","tx_started_status_recovery"},{"id",packet.id},
                     {"length",recovered.value("last_length")},{"airtime_us",recovered.value("last_airtime_us")},
                     {"start_us",recovered.value("last_start_us")},{"payload_fnv1a32",recovered.value("last_payload_hash")}};
        }
        if (started.value("length").toInteger(-1)!=packet.length || started.value("payload_fnv1a32").toInteger(-1)!=hash)
            throw std::runtime_error("Pager payload receipt does not match the requested packet");
        auto done=readReply(port,buffer,QDeadlineTimer(3000),{"tx_done"},packet.id);
        if (done.isEmpty()) {
            const auto recovered=queryStatus(port,buffer);
            if (configured.value("completed_count").toInteger(-1)<0 ||
                recovered.value("last_completed_id").toInteger(-1)!=packet.id ||
                recovered.value("completed_count").toInteger(-1)<=configured.value("completed_count").toInteger(-1) ||
                recovered.value("last_result").toInteger(-1)!=0)
                throw std::runtime_error("Pager completion could not be confirmed");
            done={{"ok",true},{"kind","tx_done_status_recovery"},{"id",packet.id},
                  {"elapsed_us",recovered.value("last_elapsed_us")},{"code",0}};
        }
        if (done.value("code").toInteger(-1)!=0) throw std::runtime_error("Pager reported a failed transmission");
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-startedWaiting).count();
        const auto requestedSeconds=std::chrono::duration<double>(requested.time_since_epoch()).count();
        return {{"configuration",configured},{"started",started},{"done",done},{"payload_hex",QString::fromLatin1(bytes.toHex())},
                {"host_request_unix_seconds",requestedSeconds},{"host_done_unix_seconds",requestedSeconds+elapsed},
                {"host_elapsed_seconds",elapsed},
                {"host_request_utc",QDateTime::fromMSecsSinceEpoch(requestMilliseconds.count(),QTimeZone::utc()).toString(Qt::ISODateWithMs)}};
    } catch (...) {
        try { sendCommand(port,"STOP\n"); } catch (const std::exception&) {}
        throw;
    }
}

QJsonObject sendAmplitude(QSerialPort& port, QByteArray& buffer, const AmplitudeTest& test) {
    resetInput(port,buffer);
    sendCommand(port,"AMP "+QByteArray::number(test.frequency)+' '+test.modulation+'\n');
    const auto reply=readReply(port,buffer,QDeadlineTimer(3000),{"amp_done"});
    if (reply.isEmpty()) throw std::runtime_error("Timed out waiting for Pager amplitude test confirmation");
    if (reply.value("modulation").toString().toLatin1()!=test.modulation ||
        reply.value("frequency_hz").toInteger(-1)!=test.frequency)
        throw std::runtime_error("Pager amplitude test confirmation did not match the request");
    return reply;
}

bool validFrequency(qint64 frequency) {
    return frequency>=470000000 && frequency<=510000000;
}

void validate(const Command& command) {
    if (const auto* test=std::get_if<AmplitudeTest>(&command)) {
        if (!validFrequency(test->frequency) || (test->modulation!="OOK" && test->modulation!="ASK"))
            throw std::invalid_argument("Invalid amplitude test frequency or modulation");
    }
    if (const auto* send=std::get_if<Transmission>(&command)) {
        const auto& packet=send->packet;
        if (!validFrequency(send->frequency) || packet.id<0 || packet.id>0xffffffffLL ||
            packet.length<8 || packet.length>240)
            throw std::invalid_argument("Invalid packet frequency, ID or length");
        if (const auto& lora=send->lora) {
            if (lora->bandwidth!=125000 && lora->bandwidth!=250000 && lora->bandwidth!=500000)
                throw std::invalid_argument("Invalid LoRa bandwidth");
            if (lora->sf<7 || lora->sf>12 || lora->cr<5 || lora->cr>8 || lora->power< -9 || lora->power>0 ||
                lora->preamble<6 || lora->preamble>64 || lora->sync<0 || lora->sync>255)
                throw std::invalid_argument("Invalid LoRa transmission settings");
        }
    }
}
}

QJsonObject runCommand(const QString& name,const Command& command) {
    validate(command);
    QSerialPort port(name);
    port.setReadBufferSize(maxReplyBytes);
    if (!port.setBaudRate(QSerialPort::Baud115200) || !port.open(QIODevice::ReadWrite))
        throw std::runtime_error(port.errorString().toStdString());
    port.setRequestToSend(false);
    port.setDataTerminalReady(true);
    QByteArray buffer;
    const auto status = queryStatus(port, buffer);
    const bool reportsMode=status.value("firmware")=="amc-pager-tx-0.3";
    if (const auto* amplitude=std::get_if<AmplitudeTest>(&command)) {
        if (!reportsMode) throw std::runtime_error("Amplitude tests require AMC Pager firmware 0.3");
        return sendAmplitude(port,buffer,*amplitude);
    }
    if (const auto* transmission=std::get_if<Transmission>(&command)) {
        if (!transmission->lora && !reportsMode) throw std::runtime_error("GFSK requires AMC Pager firmware 0.3");
        const auto configured=transmission->lora
            ? configureLoRa(port,buffer,transmission->frequency,*transmission->lora,reportsMode)
            : configureGfsk(port,buffer,transmission->frequency);
        return sendPacket(port,buffer,*transmission,configured);
    }
    if (!std::holds_alternative<Stop>(command)) return status;
    buffer.clear();
    sendCommand(port, "STOP\n");
    const auto reply = readReply(port, buffer, QDeadlineTimer(2000), {"idle", "aborted"});
    if (reply.isEmpty()) throw std::runtime_error("Timed out waiting for Pager stop confirmation");
    return reply;
}
}
