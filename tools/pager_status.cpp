#include "pager/session.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>

#include <iostream>
#include <stdexcept>

using namespace signalscope::pager;

namespace {
qint64 integerOption(const QCommandLineParser& parser,const QString& name,int base=10) {
    bool ok=false;
    const auto value=parser.value(name).toLongLong(&ok,base);
    if (!ok)
        throw std::runtime_error("Invalid value for --"+name.toStdString());
    return value;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Query, stop or transmit a bounded test signal on an AMC Pager.");
    const auto help = parser.addHelpOption();
    parser.addOption({QStringLiteral("port"), QStringLiteral("Serial port name or device path."), QStringLiteral("port")});
    parser.addOption({QStringLiteral("stop"), QStringLiteral("Stop transmission after confirming the firmware.")});
    parser.addOptions({
        {"send", "Send one LoRa packet with the given ID.", "id"},
        {"send-gfsk", "Send one GFSK packet with the given ID.", "id"},
        {"send-amp", "Send one fixed OOK or ASK test waveform.", "mode"},
        {"length", "Payload length in bytes.", "bytes", "32"},
        {"frequency", "Center frequency in Hz.", "hz"},
        {"send-before-unix-ms", "Cancel a packet request if TX has not begun before this Unix timestamp.", "ms"},
        {"bw", "LoRa bandwidth in Hz.", "hz", "250000"},
        {"sf", "Spreading factor.", "sf", "11"},
        {"cr", "Coding-rate denominator.", "cr", "5"},
        {"power", "Nominal power in dBm.", "dbm", "-9"},
        {"preamble", "Preamble length in symbols.", "symbols", "8"},
        {"sync", "Sync word, decimal or hexadecimal.", "word", "0x12"},
        {"inverted", "Invert I/Q for the LoRa transmission."}
    });
    if (!parser.parse(app.arguments())) {
        std::cerr << parser.errorText().toStdString() << '\n';
        return 2;
    }
    if (parser.isSet(help) || parser.isSet("help-all")) {
        std::cout << parser.helpText().toStdString();
        return 0;
    }
    if (parser.value("port").isEmpty() || !parser.positionalArguments().isEmpty()) {
        std::cerr << parser.helpText().toStdString();
        return 2;
    }
    try {
        const bool lora=parser.isSet("send"), gfsk=parser.isSet("send-gfsk"), stop=parser.isSet("stop");
        const bool amp=parser.isSet("send-amp");
        if (int(lora)+int(gfsk)+int(stop)+int(amp)>1) throw std::runtime_error("Select only one Pager operation");
        if (!lora) {
            for (const auto* option : {"bw","sf","cr","power","preamble","sync","inverted"})
                if (parser.isSet(option)) throw std::runtime_error("LoRa settings require --send");
        }
        if (parser.isSet("length") && !lora && !gfsk) throw std::runtime_error("--length requires a packet operation");
        if (parser.isSet("send-before-unix-ms") && !lora && !gfsk)
            throw std::runtime_error("--send-before-unix-ms requires a packet operation");
        if (parser.isSet("frequency") && !lora && !gfsk && !amp)
            throw std::runtime_error("--frequency requires a transmit operation");
        std::optional<AmplitudeTest> amplitude;
        if (amp) {
            const auto mode=parser.value("send-amp").toUpper();
            const auto frequency=parser.isSet("frequency")
                ? integerOption(parser,"frequency") : 478725000;
            amplitude=AmplitudeTest{mode.toLatin1(),frequency};
        }
        std::optional<Transmission> transmission;
        if (lora || gfsk) {
            const auto frequency=parser.isSet("frequency")
                ? integerOption(parser,"frequency") : lora?478875000:478625000;
            transmission=Transmission{{integerOption(parser,lora?"send":"send-gfsk"),
                                       integerOption(parser,"length")},frequency,{}};
            if (parser.isSet("send-before-unix-ms"))
                transmission->sendBeforeUnixMs=integerOption(parser,"send-before-unix-ms");
        }
        if (lora) {
            transmission->lora=LoRaSettings{
                integerOption(parser,"bw"),integerOption(parser,"sf"),integerOption(parser,"cr"),
                integerOption(parser,"power"),integerOption(parser,"preamble"),integerOption(parser,"sync",0),
                parser.isSet("inverted")
            };
        }
        Command command=Status{};
        if (stop) command=Stop{};
        if (transmission) command=*transmission;
        if (amplitude) command=*amplitude;
        std::cout << QJsonDocument(runCommand(parser.value("port"),command)).toJson().constData();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
