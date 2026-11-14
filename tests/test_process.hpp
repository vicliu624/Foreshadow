#pragma once

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <iostream>
#include <stdexcept>

namespace amc_test {
inline void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

inline QByteArray read(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly),"Could not read test file");
    return file.readAll();
}

inline void write(const QString& path,const QByteArray& data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data)==data.size(),"Could not write test file");
}

class ChildRunner {
public:
    explicit ChildRunner(const QString& root):root_(root),environment_(QProcessEnvironment::systemEnvironment()) {
        if(environment_.value("QT_QPA_PLATFORM").isEmpty())environment_.insert("QT_QPA_PLATFORM","offscreen");
        environment_.insert("XDG_CONFIG_HOME",root+"/config");
    }

    void setEnvironment(const QString& key,const QString& value) {environment_.insert(key,value);}

    QByteArray run(const QString& executable,const QStringList& arguments,int expected=0) const {
        QProcess process;
        process.setWorkingDirectory(root_);
        process.setProcessEnvironment(environment_);
        process.start(QFileInfo(executable).absoluteFilePath(),arguments);
        require(process.waitForStarted(5000),"Could not start test process");
        if(!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(5000);
            std::cerr<<QFileInfo(executable).absoluteFilePath().toUtf8().constData()<<' '
                     <<arguments.join(' ').toUtf8().constData()<<" timed out\n"
                     <<process.readAllStandardError().constData();
            throw std::runtime_error("Test process timed out");
        }
        if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=expected) {
            std::cerr<<arguments.join(' ').toUtf8().constData()<<" exited "<<process.exitCode()<<'\n'
                     <<process.readAllStandardError().constData();
            throw std::runtime_error("Unexpected test process exit");
        }
        return process.readAllStandardOutput();
    }
private:
    QString root_;
    QProcessEnvironment environment_;
};
}
