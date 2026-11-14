#include "signalscope/backend/dsp/channelizer.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
using namespace signalscope;
namespace {
constexpr double tau=2*std::numbers::pi;
using Complex=std::complex<float>;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Bytes tones(std::uint32_t rate,std::size_t count,std::vector<std::pair<double,double>> signals) {
    Bytes bytes;bytes.reserve(count*4);
    for(std::size_t i=0;i<count;++i) {
        std::complex<double> value{};
        for(const auto [frequency,amplitude]:signals)value+=std::polar(amplitude,tau*frequency*i/rate);
        for(const auto part:{value.real(),value.imag()}) {
            const auto word=static_cast<std::uint16_t>(static_cast<std::int16_t>(std::llround(std::clamp(part,-.999,.999)*32768)));
            bytes.push_back(word&255);bytes.push_back(word>>8);
        }
    }
    return bytes;
}
std::complex<double> project(std::span<const Complex> data,double offset,double rate,double delay,double inputRate) {
    std::complex<double> sum{};const auto begin=std::min<std::size_t>(data.size(),256);
    for(std::size_t i=begin;i<data.size();++i)
        sum+=std::complex<double>(data[i])*std::polar(1.,-tau*offset*(i/rate-delay/inputRate));
    return sum/double(data.size()-begin);
}
std::vector<std::vector<Complex>> run(Channelizer& converter,const Bytes& bytes,bool split) {
    std::vector<std::vector<Complex>> outputs(converter.channels().size());
    const auto sink=[&](const ChannelizedBlock& block) {
        auto& target=outputs[block.channel];require(block.first_sample==target.size(),"Channel output index is discontinuous");
        target.insert(target.end(),block.samples.begin(),block.samples.end());
    };
    if(!split)converter.feed(bytes,sink);
    else for(std::size_t at=0;at<bytes.size();) {
        const auto count=std::min<std::size_t>(((at/4)%4093+1)*4,bytes.size()-at);
        converter.feed(std::span(bytes).subspan(at,count),sink);at+=count;
    }
    converter.finish(sink);return outputs;
}
}
int main() {
    try {
        Channelizer empty(10000000,2000000,SampleFormat::ci16_le,{{0,1000000}});
        empty.finish([&](const auto&){throw std::runtime_error("Empty input emitted invented channel samples");});
        const auto bytes=tones(10000000,100000,{{4200000,.18},{-3700000,.12}});
        Channelizer wide(10000000,2000000,SampleFormat::ci16_le,{{-4000000,1000000},{4000000,1000000}});
        const auto a=run(wide,bytes,false);
        require(std::abs(std::abs(project(a[0],300000,2000000,wide.delay_input_samples(),10000000))-.12)<.002,
            "Negative wideband channel was not retained");
        require(std::abs(std::abs(project(a[1],200000,2000000,wide.delay_input_samples(),10000000))-.18)<.002,
            "Positive wideband channel was not retained");
        wide.reset();const auto b=run(wide,bytes,true);require(a==b,"Input block splitting or reset changed channel samples");
        wide.reset();std::vector<std::vector<Complex>> normalized(a.size());std::vector<Complex> decoded;
        for(std::size_t i=0;i<bytes.size();i+=bytes_per_pair(SampleFormat::ci16_le))decoded.push_back(read_iq_pair(bytes.data()+i,SampleFormat::ci16_le));
        const auto normalizedSink=[&](const ChannelizedBlock& block){auto& output=normalized[block.channel];require(block.first_sample==output.size(),"Normalized input lost its output clock");output.insert(output.end(),block.samples.begin(),block.samples.end());};
        for(std::size_t at=0;at<decoded.size();at+=997)wide.feed(std::span(decoded).subspan(at,std::min<std::size_t>(997,decoded.size()-at)),normalizedSink);
        wide.finish(normalizedSink);require(a==normalized,"Normalized and encoded channelizer inputs produced different samples");
        Channelizer alias(10000000,2000000,SampleFormat::ci16_le,{{0,1000000}});
        const auto filtered=run(alias,tones(10000000,100000,{{100000,.2},{2300000,.5}}),true)[0];
        require(std::abs(project(filtered,100000,2000000,alias.delay_input_samples(),10000000))>.195,
            "Channel filtering attenuated the passband");
        require(std::abs(project(filtered,300000,2000000,alias.delay_input_samples(),10000000))<.0005,
            "Out-of-band energy aliased into the channel");
        Channelizer edge(10000000,2000000,SampleFormat::ci16_le,{{-4400000,1000000}});
        const auto boundary=run(edge,tones(10000000,100000,{{-4900000,.2},{4900000,.5}}),true)[0];
        const auto actual=edge.channels()[0].offset_hz;
        require(std::abs(project(boundary,-4900000-actual,2000000,edge.delay_input_samples(),10000000))>.19 &&
            std::abs(project(boundary,-5100000-actual,2000000,edge.delay_input_samples(),10000000))<.003,
            "Opposite RF edge wrapped into the local analysis channel");
        for(const auto rate:{2048000u,2400000u,2560000u,3200000u,10660000u}) {
            Channelizer converter(rate,2000000,SampleFormat::ci16_le,{{0,1000000}});
            const auto output=run(converter,tones(rate,rate/100,{{100000,.2}}),true)[0];
            const auto value=project(output,100000,2000000,converter.delay_input_samples(),rate);
            require(std::abs(std::abs(value)-.2)<.004 && std::abs(std::arg(value))<.03,
                "Fractional resampling changed signal frequency, level or time reference");
        }
        const auto plan=planAnalysisChannels(10000000,2000000,10000000,500000);
        require(plan.size()>1 && plan.front().offset_hz< -4000000 && plan.back().offset_hz>4000000,
            "Wideband plan only covers the center of the source");
        for(std::size_t i=1;i<plan.size();++i)
            require(plan[i].offset_hz-plan[i-1].offset_hz<=(plan[i].search_width_hz+plan[i-1].search_width_hz)/2,
                "Analysis channels leave a frequency coverage gap");
        std::cout<<"Wideband channels, fractional rates, alias rejection and stream continuity passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
