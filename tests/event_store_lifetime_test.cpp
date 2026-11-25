#include "signalscope/backend/recordings/event_store.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "resource_count.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#ifdef __linux__
#include <fcntl.h>
#endif
using namespace signalscope;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--child")) {
        std::cout<<"ready\n"<<std::flush;std::string line;std::getline(std::cin,line);return 0;
    }
    try {
        QTemporaryDir temporary;require(temporary.isValid(),"Cannot create lock lifetime fixture");
        const auto root=path_from_utf8(temporary.path().toStdString())/path_from_utf8("录制锁 📡");
        Config cfg;cfg.rate=250000;cfg.frequency=470000000;cfg.seconds=0;
        RetentionPolicy policy;
        auto store=std::make_unique<EventStore>(root,cfg,policy);
#ifdef __linux__
        bool found=false;
        for(const auto& entry:std::filesystem::directory_iterator("/proc/self/fd")) {
            std::error_code error;
            if(std::filesystem::read_symlink(entry.path(),error)==root/".store.lock") {
                found=true;const auto fd=std::stoi(entry.path().filename().string());
                require((fcntl(fd,F_GETFD)&FD_CLOEXEC)!=0,"Recording lock is inherited across exec");
            }
        }
        require(found,"Active recording store did not hold its lock");
#endif
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),{"--child"});
        require(child.waitForStarted(3000) && child.waitForReadyRead(3000) && child.readLine()=="ready\n","Child process did not become ready");
        store.reset();
        {EventStore reopened(root,cfg,policy);}
        require(child.state()==QProcess::Running,"The lock was only released by stopping the child");
        child.write("quit\n");require(child.waitForFinished(3000) && child.exitCode()==0,"Child process did not exit cleanly");
        const auto baseline=amc_test::openResourceCount();
        for(int attempt=0;attempt<64;++attempt) {
            {EventStore opened(root,cfg,policy);
                bool rejected=false;try{EventStore competing(root,cfg,policy);}catch(const OperationError&){rejected=true;}
                require(rejected,"Concurrent event store was not rejected");
                opened.finish();}
            require(amc_test::openResourceCount()==baseline,"Repeated event store success/failure leaked a resource");
            const auto directory=root.parent_path()/ ("capture-"+std::to_string(attempt));
            const auto format=attempt%2?SampleFormat::ci16_le:SampleFormat::cu8;
            const auto stride=bytes_per_pair(format);const Bytes bytes(128*stride,0);
            auto recordingConfig=cfg;recordingConfig.seconds=.001;
            Recorder recorder(directory,recordingConfig,"resource-lifetime-fixture",format);
            recorder.append(bytes);recorder.finish("complete");
            require(amc_test::openResourceCount()==baseline,"Finished recorder retained its data file");
            const auto info=inspect_recording(directory/"capture.sigmf-meta");
            std::size_t pairs=0;
            replay(info,0,info.pairs,0,32*stride,[&](auto block,auto){pairs+=block.size()/stride;},[]{return false;});
            require(pairs==128,"Resource fixture replay did not read the complete recording");
            bool failed=false;
            try{replay(info,0,info.pairs,0,32*stride,[](auto,auto){throw std::runtime_error("consumer-fault");},[]{return false;});}
            catch(const std::runtime_error& error){failed=std::string(error.what())=="consumer-fault";}
            require(failed,"Replay consumer failure was not exercised");
            replay(info,0,info.pairs,0,32*stride,[](auto,auto){throw std::runtime_error("Canceled replay delivered data");},[]{return true;});
            require(amc_test::openResourceCount()==baseline,"Replay success/failure/cancellation retained a recording file");
        }
        std::cout<<"{\"lock_cycles\":64,\"recording_replay_cycles\":64,\"baseline\":"<<baseline<<",\"final\":"<<amc_test::openResourceCount()<<"}\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
