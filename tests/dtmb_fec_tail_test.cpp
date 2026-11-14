#include "dtmb_fixture_fec.hpp"
#include "test_process.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <bit>
#include <cmath>
#include <iostream>

namespace {
using amc_test::require;
QByteArray llrs(std::span<const std::uint8_t> bits) {
    QByteArray bytes;bytes.reserve(static_cast<qsizetype>(bits.size()*4));
    for(const auto bit:bits) {
        const auto value=std::bit_cast<std::uint32_t>(bit?-16.f:16.f);
        for(unsigned byte=0;byte<4;++byte)bytes.append(static_cast<char>(value>>(byte*8)));
    }
    return bytes;
}
struct Result {QByteArray output,diagnostics;int exit;};
Result run(const QString& helper,unsigned rate,const QString& qam,unsigned codewords,const QByteArray& input) {
    QProcess process;process.setWorkingDirectory(QFileInfo(helper).absolutePath());
    process.start(helper,{"--fec-rate",QString::number(rate),"--alist","data/dtmb_ldpc_rate"+QString::number(rate)+".alist",
        "--qam",qam,"--codewords-per-frame",QString::number(codewords),"--workers","2","--max-iterations","10",
        "--decode-batch-frames","8","--clean-frames-only","--require-output","-","-"});
    require(process.waitForStarted(5000),"FEC helper failed to start");
    require(process.write(input)==input.size(),"FEC helper rejected the independent encoded input");process.closeWriteChannel();
    Result result;QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning) {
        if(timer.elapsed()>15000){process.kill();process.waitForFinished(1000);throw std::runtime_error("FEC tail validation timed out");}
        process.waitForReadyRead(5);result.output+=process.readAllStandardOutput();result.diagnostics+=process.readAllStandardError();
        require(result.output.size()<65536 && result.diagnostics.size()<65536,"FEC test exceeded its output budgets");
    }
    result.output+=process.readAllStandardOutput();result.diagnostics+=process.readAllStandardError();
    result.diagnostics.replace("\r\n","\n");
    result.exit=process.exitStatus()==QProcess::NormalExit?process.exitCode():-1;return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(app.arguments().size()==4,"Expected FEC helper, independent standard tables and generated media fixture");
        const auto helper=QFileInfo(app.arguments()[1]).absoluteFilePath();
        const auto tables=nlohmann::json::parse(amc_test::read(app.arguments()[2]).toStdString());
        const auto fixture=amc_test::read(app.arguments()[3]);
        for(unsigned rate=1;rate<=3;++rate) {
            dtmb_fixture::FecEncoder encoder(tables,rate);encoder.verifyGenerator();
            const auto expected=fixture.first(encoder.payloadBytes()*2);
            const auto bits=encoder.encode({reinterpret_cast<const std::uint8_t*>(expected.constData()),static_cast<std::size_t>(expected.size())},encoder.payloadBytes());
            auto input=llrs(bits);input+=llrs(std::span(bits).first(137));
            const auto result=run(helper,rate,"4qam",1,input);
            if(result.exit)std::cerr<<result.diagnostics.constData();
            require(result.exit==0 && result.output==expected && result.diagnostics.contains("trailing_fec_llrs=137\n"),
                "An incomplete tail changed earlier independently encoded transport payloads");
        }
        for(const auto nr:{true,false}) {
            dtmb_fixture::FecEncoder encoder(tables,3);const unsigned words=nr?3:10,perGroup=nr?1:5,half=perGroup*7488/2;
            const auto expected=fixture.first(encoder.payloadBytes()*words);
            const auto bits=encoder.encode({reinterpret_cast<const std::uint8_t*>(expected.constData()),static_cast<std::size_t>(expected.size())},encoder.payloadBytes()*perGroup/2);
            auto input=llrs(bits);input+=llrs(std::span(bits).first(half));
            const auto qam=nr?QString("4qam-nr"):QString("32qam");const auto result=run(helper,3,qam,perGroup,input);
            if(result.exit)std::cerr<<result.diagnostics.constData();
            require(result.exit==0 && result.output==expected && result.diagnostics.contains("trailing_fec_llrs="+QByteArray::number(half)+"\n"),
                "A final paired-frame half-group invalidated previously verified NR/32QAM payloads");
            auto corrupt=input;corrupt.append('x');require(run(helper,3,qam,perGroup,corrupt).exit!=0,"A partial private float was accepted as a normal FEC tail");
            corrupt=input;const std::uint32_t nan=0x7fc00000;for(unsigned i=0;i<4;++i)corrupt[corrupt.size()-4+i]=static_cast<char>(nan>>(8*i));
            require(run(helper,3,qam,perGroup,corrupt).exit!=0,"A nonfinite LLR was hidden in a discarded tail");
        }
        require(run(helper,1,"4qam",1,llrs(std::vector<std::uint8_t>(100))).exit!=0,"A stream with no complete valid FEC group was reported as successful");
        std::cout<<"Independent standard G/H, all three FEC rates, paired-frame tails and corrupt-tail rejection passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
