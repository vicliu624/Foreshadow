#include "test_process.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <cmath>
#include <poll.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

namespace {
using amc_test::require;

class TerminalPair {
public:
    TerminalPair() {
        char path[128]{};
        require(openpty(&master, &slave, path, nullptr, nullptr) == 0,"Could not open a virtual serial port");
        name=QFile::decodeName(path);
    }
    ~TerminalPair() {
        close(master);
        close(slave);
    }
    TerminalPair(const TerminalPair&)=delete;
    TerminalPair& operator=(const TerminalPair&)=delete;
    int master=-1;
    QString name;
private:
    int slave=-1;
};

QByteArray readCommand(int master,int timeout=3000) {
    QByteArray command;
    QDeadlineTimer deadline(timeout);
    while(!command.contains('\n') && !deadline.hasExpired()) {
        pollfd input{master,POLLIN,0};
        require(poll(&input,1,static_cast<int>(deadline.remainingTime()))==1,
                "Pager query did not send a serial command");
        char fragment[64];
        const auto count=read(master,fragment,sizeof(fragment));
        require(count>0,"Could not read the Pager query");
        command.append(fragment,count);
    }
    return command;
}

void checkReply(const QString& executable,const QByteArray& firmware,bool supported,
                bool stop=false,const QByteArray& confirmation={}) {
    TerminalPair terminal;
    QProcess client;
    QStringList arguments{"--port",terminal.name};
    if(stop)arguments.append("--stop");
    client.start(executable,arguments);
    require(client.waitForStarted(3000),"Could not launch the Pager query");
    require(readCommand(terminal.master)=="PING\n","Pager status sent an unexpected command");
    const QByteArray first="boot banner\r\n{\"kind\":\"status\",";
    const QByteArray second="\"ok\":true,\"firmware\":\""+firmware+"\",\"radio_ready\":true}\r\n";
    require(write(terminal.master,first.constData(),first.size())==first.size(),"Could not write the first reply fragment");
    QThread::msleep(20);
    require(write(terminal.master,second.constData(),second.size())==second.size(),"Could not write the second reply fragment");
    if(stop && supported) {
        require(readCommand(terminal.master)=="STOP\n","Pager did not send exactly one stop after the handshake");
        const QByteArray reply="{\"ok\":true,\"kind\":\""+confirmation+"\"}\n";
        require(write(terminal.master,reply.constData(),reply.size())==reply.size(),"Could not send stop confirmation");
    }
    require(client.waitForFinished(3000) && client.exitStatus()==QProcess::NormalExit,
            "Pager query did not exit normally");
    const auto output=client.readAllStandardOutput();
    if(supported && stop) {
        const auto result=QJsonDocument::fromJson(output).object();
        require(client.exitCode()==0 && result.value("kind").toString().toUtf8()==confirmation,
                "Pager lost the stop confirmation");
    } else if(supported) {
        const auto status=QJsonDocument::fromJson(output).object();
        require(client.exitCode()==0 && status.value("firmware").toString().toUtf8()==firmware &&
                status.value("radio_ready").toBool(),"Pager query lost a valid status reply");
    } else {
        require(client.exitCode()==1 && output.isEmpty() &&
                client.readAllStandardError().contains("Unrecognized AMC Pager firmware"),
                "Pager query accepted unsupported firmware");
    }
    pollfd remaining{terminal.master,POLLIN,0};
    require(poll(&remaining,1,0)==0,"Pager query sent a command after the status exchange");
}

void checkSend(const QString& executable,bool validConfig,bool loseCompletion=false,bool gfsk=false,bool expired=false) {
    TerminalPair terminal;
    QProcess client;
    QStringList arguments{"--port",terminal.name,gfsk?"--send-gfsk":"--send","42","--length","8"};
    if(expired)arguments.append({"--send-before-unix-ms","1"});
    client.start(executable,arguments);
    require(client.waitForStarted(3000),"Could not launch the Pager send test");
    const auto reply=[&](const QByteArray& bytes) {
        require(write(terminal.master,bytes.constData(),bytes.size())==bytes.size(),"Could not emulate Pager reply");
    };
    require(readCommand(terminal.master)=="PING\n","Send did not start with a firmware handshake");
    reply("{\"kind\":\"status\",\"ok\":true,\"firmware\":\"amc-pager-tx-0.3\"}\n");
    require(readCommand(terminal.master)==(gfsk?"GFSK 478625000\n":"SET 478875000 250000 11 5 -9 8 18 0\n"),
            "Unexpected Pager configuration");
    reply(gfsk?"{\"ok\":true,\"kind\":\"mode\",\"modulation\":\"gfsk\"}\n":
               "{\"ok\":true,\"kind\":\"mode\",\"modulation\":\"lora\"}\n");
    reply("{\"ok\":true,\"kind\":\"status\",\"radio_ready\":true,\"frequency_hz\":"+
          QByteArray(gfsk?"478625000":"478875000")+","+
          "\"bandwidth_hz\":250000,\"sf\":11,\"cr\":5,\"power_dbm\":"+
          QByteArray(validConfig?"-9":"-6")+
          ",\"preamble\":8,\"sync\":18,\"inverted\":false,\"started_count\":0,\"completed_count\":0}\n");
    if(validConfig && !expired) {
        require(readCommand(terminal.master)=="TX 42 8\n","Expected one explicit transmission request");
        reply("{\"ok\":true,\"kind\":\"tx_started\",\"id\":42,\"length\":8,\"payload_fnv1a32\":2675790473}\n");
        if(loseCompletion) {
            require(readCommand(terminal.master,4500)=="PING\n","Missing completion triggered a retry instead of a status query");
            reply("{\"kind\":\"status\",\"ok\":true,\"firmware\":\"amc-pager-tx-0.3\","
                  "\"last_completed_id\":42,\"completed_count\":1,\"last_result\":0,\"last_elapsed_us\":1000}\n");
        } else {
            reply("{\"ok\":true,\"kind\":\"tx_done\",\"id\":42,\"code\":0}\n");
        }
    }
    require(client.waitForFinished(3000) && client.exitStatus()==QProcess::NormalExit,"Pager send test did not finish");
    if(validConfig && !expired) {
        const auto result=QJsonDocument::fromJson(client.readAllStandardOutput()).object();
        require(client.exitCode()==0 && result.value("payload_hex")=="414d43312a000000" &&
                result.value("done").toObject().value("kind")==
                    (loseCompletion?"tx_done_status_recovery":"tx_done"),"Pager send receipt did not match the request");
        const auto request=result.value("host_request_unix_seconds").toDouble();
        const auto completion=result.value("host_done_unix_seconds").toDouble();
        const auto elapsed=result.value("host_elapsed_seconds").toDouble(-1);
        const auto utc=QDateTime::fromString(result.value("host_request_utc").toString(),Qt::ISODateWithMs);
        require(request>0 && elapsed>=0 && completion>=request &&
                std::abs((completion-request)-elapsed)<0.000002 && utc.isValid() && utc.offsetFromUtc()==0 &&
                std::abs(request-utc.toMSecsSinceEpoch()/1000.0)<0.002,
                "Pager send report lost its host correlation timestamps");
        if(gfsk) {
            const auto configuration=result.value("configuration").toObject();
            require(configuration.value("modulation")=="gfsk" && configuration.value("symbol_rate").toInt()==2400 &&
                    configuration.value("deviation_hz").toInt()==4800,"GFSK report lost its modulation parameters");
        }
    } else {
        const auto expected=expired?"transmission deadline expired":"configuration readback mismatch";
        require(client.exitCode()==1 && client.readAllStandardError().contains(expected),
                "Pager accepted a mismatched configuration or an expired request");
    }
    pollfd remaining{terminal.master,POLLIN,0};
    require(poll(&remaining,1,0)==0,"Pager sent an unexpected additional serial command");
}

void checkAmplitude(const QString& executable,const QByteArray& mode,bool cooldown=false) {
    TerminalPair terminal;
    QProcess client;
    client.start(executable,{"--port",terminal.name,"--send-amp",QString::fromLatin1(mode)});
    require(client.waitForStarted(3000),"Could not launch the Pager amplitude test");
    const auto reply=[&](const QByteArray& bytes) {
        require(write(terminal.master,bytes.constData(),bytes.size())==bytes.size(),"Could not emulate amplitude test reply");
    };
    require(readCommand(terminal.master)=="PING\n","Amplitude test skipped the firmware handshake");
    reply("{\"kind\":\"status\",\"ok\":true,\"firmware\":\"amc-pager-tx-0.3\"}\n");
    require(readCommand(terminal.master)=="AMP 478725000 "+mode+'\n',"Unexpected Pager amplitude command");
    if(cooldown)
        reply("{\"ok\":false,\"error\":\"cooldown\",\"code\":0}\n");
    else
        reply("{\"ok\":true,\"kind\":\"amp_done\",\"frequency_hz\":478725000,\"modulation\":\""+mode+"\"}\n");
    require(client.waitForFinished(3000) && client.exitStatus()==QProcess::NormalExit,
            "Pager amplitude test did not exit normally");
    if(cooldown) {
        require(client.exitCode()==1 && client.readAllStandardError().contains("cooldown"),
                "Pager amplitude test lost the cooldown error");
    } else {
        const auto result=QJsonDocument::fromJson(client.readAllStandardOutput()).object();
        require(client.exitCode()==0 && result.value("modulation").toString().toLatin1()==mode,
                "Pager amplitude test lost the waveform confirmation");
    }
    pollfd remaining{terminal.master,POLLIN,0};
    require(poll(&remaining,1,0)==0,"Pager sent an additional amplitude command");
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=2)return 2;
    try {
        checkReply(app.arguments()[1],"amc-pager-tx-0.3",true);
        checkReply(app.arguments()[1],"amc-pager-tx-0.3",true,true,"idle");
        checkReply(app.arguments()[1],"amc-pager-tx-0.3",true,true,"aborted");
        checkReply(app.arguments()[1],"unknown-firmware",false,true);
        checkSend(app.arguments()[1],true);
        checkSend(app.arguments()[1],false);
        checkSend(app.arguments()[1],true,true);
        checkSend(app.arguments()[1],true,false,true);
        checkSend(app.arguments()[1],false,false,true);
        checkSend(app.arguments()[1],true,false,false,true);
        checkAmplitude(app.arguments()[1],"OOK");
        checkAmplitude(app.arguments()[1],"ASK");
        checkAmplitude(app.arguments()[1],"OOK",true);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
