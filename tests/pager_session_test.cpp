#include "pager/session.hpp"
#include "test_process.hpp"

#include <QCoreApplication>

#include <array>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using namespace signalscope::pager;
    const std::array<Command,5> invalid{
        AmplitudeTest{"OOK\nTX 42 8",478725000},
        AmplitudeTest{"ASK",0},
        Transmission{{42,7},478625000,{}},
        Transmission{{42,32},478875000,LoRaSettings{400000,11,5,-9,8,18,false}},
        Transmission{{42,32},478875000,LoRaSettings{250000,11,5,1,8,18,false}}
    };
    try {
        for(const auto& command:invalid) {
            bool rejected=false;
            try {
                runCommand(QStringLiteral("amc-test-nonexistent-port"),command);
            } catch(const std::invalid_argument&) {
                rejected=true;
            }
            amc_test::require(rejected,"Invalid Pager request reached the serial transport");
        }
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
