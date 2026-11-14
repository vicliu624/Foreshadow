#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Json=nlohmann::json;
using Sample=std::complex<double>;
constexpr int rate=3'000'000, blockSamples=16'384, offsetHz=75'000, peak=8192;
constexpr int sampleCount=((rate/5+blockSamples-1)/blockSamples)*blockSamples;
constexpr std::array burstStarts{60'000,240'000,420'000};
constexpr double tau=2*std::numbers::pi;

std::vector<Sample> makeCss(Json& metadata) {
    constexpr int bandwidth=500'000, sf=7, symbols=1<<sf, period=rate*symbols/bandwidth;
    metadata.update({{"bandwidth_hz",bandwidth},{"sf_equivalent",sf},{"format","generic_css"}});
    std::vector<Sample> burst;
    burst.reserve(9*period+period/4);
    for(int n=0;n<9*period+period/4;++n) {
        const auto position=double(n%period)/period;
        const auto phase=std::numbers::pi*symbols*(position*position-position);
        burst.push_back(std::polar(1.,n<8*period?phase:-phase));
    }
    return burst;
}

std::vector<Sample> makeKeyedBurst(bool ook,std::uint32_t seed,Json& metadata) {
    constexpr int symbolRate=2400, deviation=4800, samplesPerSymbol=rate/symbolRate;
    metadata["symbol_rate"]=symbolRate;
    metadata["random_sequence"]="mt19937_low_bit";
    if(!ook)metadata["deviation_hz"]=deviation;
    std::mt19937 random(seed);
    std::vector<Sample> burst;
    burst.reserve(80*samplesPerSymbol);
    double phase=0;
    for(int symbol=0;symbol<80;++symbol) {
        const bool high=symbol<16?symbol%2==0:(random()&1u)!=0;
        for(int n=0;n<samplesPerSymbol;++n) {
            if(ook)burst.emplace_back(high?1.:0.,0.);
            else {
                phase+=tau*(high?deviation:-deviation)/rate;
                burst.push_back(std::polar(1.,phase));
            }
        }
    }
    return burst;
}

void writeFile(const QString& path,const QByteArray& contents) {
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
       file.write(contents)!=contents.size() || !file.flush())
        throw std::runtime_error(file.errorString().toStdString());
}

void generate(const QString& output,const QString& mode,std::uint32_t seed) {
    const QFileInfo target(QDir::cleanPath(output));
    QDir parent(target.absolutePath());
    if(target.exists() || target.isSymLink())throw std::runtime_error("Output directory already exists");
    if(!QDir().mkpath(parent.absolutePath()))throw std::runtime_error("Could not create the output parent directory");
    QTemporaryDir temporary(parent.filePath(".amc-pluto-XXXXXX"));
    if(!temporary.isValid())throw std::runtime_error("Could not create a temporary output directory");

    Json metadata{{"mode",mode.toStdString()},{"seed",seed},{"sample_rate_hz",rate},{"datatype","ci16_le"},
        {"peak_digital_count",peak},{"offset_hz",offsetHz},{"sample_count",sampleCount},
        {"burst_start_samples",burstStarts},{"iio_buffer_samples",blockSamples}};
    const auto burst=mode=="css"?makeCss(metadata):makeKeyedBurst(mode=="ook",seed,metadata);
    metadata["burst_samples"]=burst.size();
    QByteArray waveform(4*sampleCount,'\0');
    for(int n=0;n<static_cast<int>(burst.size());++n) {
        const auto value=double(peak)*(burst[n]*std::polar(1.,tau*offsetHz*n/rate));
        const auto i=static_cast<std::uint16_t>(std::lround(value.real()));
        const auto q=static_cast<std::uint16_t>(std::lround(value.imag()));
        for(const auto start:burstStarts) {
            auto* bytes=waveform.data()+4*(start+n);
            bytes[0]=static_cast<char>(i&255);
            bytes[1]=static_cast<char>(i>>8);
            bytes[2]=static_cast<char>(q&255);
            bytes[3]=static_cast<char>(q>>8);
        }
    }
    writeFile(temporary.filePath("waveform.iq"),waveform);
    writeFile(temporary.filePath("waveform.json"),QByteArray::fromStdString(metadata.dump(2)+'\n'));
    if(!parent.rename(temporary.path(),target.absoluteFilePath()))
        throw std::runtime_error("Could not publish the waveform directory");
    temporary.setAutoRemove(false);
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Generate a finite Pluto CI16 waveform for offline RF tests.");
    const auto help=parser.addHelpOption();
    parser.addOptions({{"mode","Waveform mode: css, ook or fsk.","mode"},
                       {"output","New waveform directory.","path"},
                       {"seed","Unsigned 32-bit test seed.","seed","478"}});
    if(!parser.parse(app.arguments())) {
        std::cerr<<parser.errorText().toStdString()<<'\n';
        return 2;
    }
    if(parser.isSet(help) || parser.isSet("help-all")) {
        std::cout<<parser.helpText().toStdString();
        return 0;
    }
    try {
        const auto mode=parser.value("mode");
        const auto output=parser.value("output");
        bool validSeed=false;
        const auto seed=parser.value("seed").toUInt(&validSeed);
        if((mode!="css" && mode!="ook" && mode!="fsk") || output.isEmpty() || !validSeed ||
           !parser.positionalArguments().isEmpty())
            throw std::invalid_argument("Specify --mode css|ook|fsk, --output NEW_DIR and an optional unsigned --seed");
        generate(output,mode,seed);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
