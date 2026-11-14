#pragma once

#include <QProcess>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace signalscope::tools {
inline nlohmann::json runOfflineDetect(const QString& executable,const QStringList& arguments) {
    QProcess process;
    process.start(executable,arguments);
    if(!process.waitForStarted(5000))throw std::runtime_error(process.errorString().toStdString());
    if(!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(5000);
        throw std::runtime_error("Offline LoRa detection timed out");
    }
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0)
        throw std::runtime_error("Offline LoRa detection failed: "+process.readAllStandardError().toStdString());
    const auto output=process.readAllStandardOutput();
    return nlohmann::json::parse(output.begin(),output.end());
}
}
