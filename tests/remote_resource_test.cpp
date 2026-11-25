#include "resource_count.hpp"
#include "LogAcceptor.hpp"
#include "SoapyRPCSocket.hpp"
#include "SoapyRemoteDefs.hpp"
#include "SoapyRPCPacker.hpp"
#include "SoapyRPCUnpacker.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QCoreApplication>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Logger.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <cstring>
namespace {
std::atomic<unsigned> logFailures=0;
std::atomic<unsigned> logMessages=0;
void logger(const SoapySDR::LogLevel,const char* message){
    if(std::strstr(message,"SoapyLogAcceptor::handlerLoop"))++logFailures;
    if(std::strstr(message,"FORESHADOW_RESOURCE_LOG_READY"))++logMessages;
}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void sameResources(std::size_t expected,const char* message) {
    const auto actual=amc_test::openResourceCount();
    if(actual!=expected)throw std::runtime_error(std::string(message)+" (expected "+std::to_string(expected)+", actual "+std::to_string(actual)+")");
}
template<class Predicate>void until(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate()) {
        require(std::chrono::steady_clock::now()<deadline,"Fault/recovery handshake timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
int readCount(const std::filesystem::path& path){int n=0;std::ifstream input(path);input>>n;return n;}
void confirmLogReady(SoapyRPCSocket& command,int generation) {
    const auto previous=logMessages.load();
    SoapyRPCPacker request(command);request & SOAPY_REMOTE_FIND;
    request & SoapySDR::Kwargs{{"driver","sdrplay"},{"amc_test_log_marker",std::to_string(generation)}};request();
    SoapyRPCUnpacker response(command,true,1000000);SoapySDR::KwargsList devices;response & devices;
    // A real forwarded message proves the logger reached its receive loop.
    // In particular, WinSock's lazy receive resources must exist at both counts.
    until([&]{return logMessages.load()>previous;});
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        const auto arguments=app.arguments();
        require(arguments.size()>=3,"Expected construction/reconnect mode and service endpoint");
        SoapySDR::registerLogHandler(logger);
        SoapySocketSession session;
        const auto endpoint=arguments.at(2).toStdString();
        if(arguments.at(1)=="construction") {
            const auto make=SoapySDR::Registry::listMakeFunctions().at("remote");
            SoapySDR::Kwargs args{{"remote",endpoint},{"driver","remote"},{"remote:driver","sdrplay"},{"remote:timeout","1000000"}};
            {std::unique_ptr<SoapySDR::Device> opened(make(args));}
            const auto warmed=amc_test::openResourceCount();
            for(int cycle=0;cycle<32;++cycle) {
                auto failing=args;failing["remote:amc_test_reject_make"]="true";
                bool rejected=false;try{std::unique_ptr<SoapySDR::Device> opened(make(failing));}catch(const std::exception& error){rejected=std::strstr(error.what(),"Deliberately rejected remote construction")!=nullptr;}
                require(rejected,"Remote MAKE fault was not exercised");
                sameResources(warmed,"Rejected remote construction retained a socket/thread");
                {std::unique_ptr<SoapySDR::Device> opened(make(args));}
                sameResources(warmed,"Successful remote device release retained resources");
            }
            std::cout<<"{\"construction_cycles\":32,\"baseline\":"<<warmed<<",\"final\":"<<amc_test::openResourceCount()<<"}\n";
        }else {
            require(arguments.size()==4,"Expected reconnect control directory");
            const auto control=signalscope::path_from_utf8(arguments.at(3).toStdString());
            {
                SoapyRPCSocket command;require(command.connect(endpoint,1000000)==0,"Cannot warm command socket");
                SoapyLogAcceptor warm(endpoint,command,1000000);confirmLogReady(command,-1);
            }
            // A fully closed warmup excludes process-wide WinSock initialization.
            const auto baseline=amc_test::openResourceCount();
            {
                SoapyRPCSocket command;require(command.connect(endpoint,1000000)==0,"Cannot connect command socket");
                SoapyLogAcceptor persistent(endpoint,command,1000000);
                confirmLogReady(command,0);
                const auto active=amc_test::openResourceCount();
                for(int cycle=1;cycle<=16;++cycle) {
                    const auto failures=logFailures.load();
                    const auto acceptedConnections=readCount(control/"accepted");
                    {std::ofstream drop(control/"drop");drop<<cycle;}
                    until([&]{return logFailures.load()>failures;});
                    until([&]{SoapyLogAcceptor transient(endpoint,command,1000000);return readCount(control/"accepted")>acceptedConnections;});
                    confirmLogReady(command,cycle);
                    sameResources(active,"Log reconnect retained the previous socket/thread");
                }
            }
            sameResources(baseline,"Log unsubscribe retained resources");
            std::cout<<"{\"reconnect_cycles\":16,\"baseline\":"<<baseline<<",\"final\":"<<amc_test::openResourceCount()<<"}\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
