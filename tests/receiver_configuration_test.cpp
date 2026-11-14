#include "plugins/receivers/rtl_sdr/profile.hpp"
#include "signalscope/backend/sessions/receiver_configuration.hpp"
#include <iostream>
using namespace signalscope;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool has(const ReceiverPlan& p,const char* code){return std::any_of(p.issues.begin(),p.issues.end(),[&](const auto& i){return i.code==code;});}
}
int main() {
    try {
        Config c;c.auto_gain=true;
        const auto rtl=rtlReceiverCapabilities(5,{0,197,280});
        require(planReceiverConfiguration(rtl,c).hardwareValid(),"Normal R820T configuration rejected");
        c.rate=500000;require(has(planReceiverConfiguration(rtl,c),"receiver_rate"),"librtlsdr sample-rate gap accepted");
        c.rate=2400000;require(planReceiverConfiguration(rtl,c).hardwareValid(),"A rate outside recommended presets was rejected");
        c.frequency=2e9;require(!planReceiverConfiguration(rtl,c).hardwareValid() &&
            planReceiverConfiguration(rtlReceiverCapabilities(1,{197}),c).hardwareValid(),"Tuner-specific frequency limits were ignored");
        c.frequency=1200e6;require(!planReceiverConfiguration(rtlReceiverCapabilities(1,{197}),c).hardwareValid(),"E4000 RF gap accepted");
        c=Config{};c.auto_gain=false;c.gain_tenths=123;
        require(has(planReceiverConfiguration(rtl,c),"receiver_gain"),"Unsupported manual gain accepted");
        c.auto_gain=true;require(planReceiverConfiguration(rtl,c).hardwareValid(),"AGC incorrectly depends on inactive manual gain");
        ReceiverCapabilities rsp;rsp.backend="sdrplay";rsp.model="RSPdx-R2";rsp.format=SampleFormat::ci16_le;
        rsp.sample_rate_ranges={{2000000,10660000}};rsp.sample_rates={2000000,2048000,3000000,10000000};
        rsp.frequency_ranges={{1000,2e9}};rsp.filter_bandwidths={200000,600000,1536000,5000000,6000000,8000000};
        rsp.antennas={"Antenna A","Antenna B","Antenna C"};rsp.limited_antenna="Antenna C";
        rsp.limited_antenna_max_hz=200e6;rsp.rate_source="driver";rsp.frequency_source="driver";
        rsp.recommended_rates={10000000,8000000,6000000,2000000};
        const ReceiverCapabilities roundtrip=Json(rsp).get<ReceiverCapabilities>();
        require(Json(roundtrip)==Json(rsp),"Capability serialization lost device constraints");
        c=Config{};c.auto_gain=true;LoRaConfig lc;lc.bandwidth_candidates={125000,250000,500000};
        auto p=planMonitorConfiguration(rsp,c,lc,true,true);
        const auto single=planMonitorConfiguration(rsp,c,LoRaConfig{},true,true);
        require(single.lora_channels==1 && single.lora_analysis_rate_hz==c.rate,"Settings preview disagrees with the direct analysis path");
        require(p.valid() && p.maximum_search_width_hz==2000000 && p.filter_bandwidth_hz==1536000 && p.bytes_per_second==8000000 && p.nominal_adc_bits==14,
                "Default RSP linked configuration is incorrect");
        auto bad=c;bad.rate=10000000;bad.antenna="Antenna C";bad.auto_gain=false;bad.gain_tenths=123;
        auto wide=lc;wide.search_width=5000000;
        const auto recommended=recommendMonitorConfiguration(rsp,bad,wide,true,true);
        require(recommended && recommended->receiver.rate==10000000 && recommended->receiver.frequency==bad.frequency &&
            recommended->receiver.antenna!="Antenna C" && recommended->receiver.auto_gain && recommended->search_width_hz==10000000,
            "Recommendation did not repair the linked rate, antenna, gain and width");
        auto fixed=wide;fixed.search_width=recommended->search_width_hz;
        require(planMonitorConfiguration(rsp,recommended->receiver,fixed,true,true).valid(),"Recommendation bypassed monitor validation");
        bad.frequency=3e9;
        require(!recommendMonitorConfiguration(rsp,bad,wide,true,true),"Recommendation silently retuned an invalid frequency");
        auto limited=rsp;limited.sample_rates={2048000};limited.sample_rate_ranges.clear();limited.recommended_rates={2048000};
        require(recommendMonitorConfiguration(limited,c,lc,true,true).has_value(),"Fractional acquisition clock was not converted for LoRa");
        const auto withoutLoRa=recommendMonitorConfiguration(limited,c,lc,false,true);
        require(withoutLoRa && withoutLoRa->receiver.rate==2048000,"Recommendation ignored detector-dependent compatibility");
        const auto rtlRecommended=recommendMonitorConfiguration(rtl,c,lc,true,true);
        require(rtlRecommended && planReceiverConfiguration(rtl,rtlRecommended->receiver).valid(),"RTL recommendation is invalid");
        lc.search_width=2100000;p=planMonitorConfiguration(rsp,c,lc,true,true);
        require(has(p,"search_passband") && p.hardwareValid(),"Search passband was confused with receiver capability");
        lc.search_width=2000000;
        require(planMonitorConfiguration(rsp,c,lc,true,true).valid() && has(planMonitorConfiguration(rsp,c,lc,true,true),"detector_edge_coverage"),"Full span was blocked instead of explaining edge coverage");
        lc.search_width=1000000;c.rate=2048000;
        require(planMonitorConfiguration(rsp,c,lc,true,true).valid() &&
                planMonitorConfiguration(rsp,c,lc,false,true).valid(),"A common acquisition rate was rejected by detector timing");
        for(const auto [rate,bits]:{std::pair{6000000u,14u},{8000000u,12u},{9000000u,10u},{10000000u,8u}}) {
            c.rate=rate;p=planMonitorConfiguration(rsp,c,lc,false,false);
            require(p.hardwareValid() && p.valid() && p.nominal_adc_bits==bits,
                    "Hardware capability, ADC profile and analysis boundary were conflated");
        }
        require(p.bytes_per_second==40000000,"CS16 storage size incorrectly followed nominal ADC bits");
        {
            auto advanced=rsp;
            advanced.extensions["settings"]=Json::array({{{"key","hdr_ctrl"},{"type","boolean"},{"default",false},{"enabled_frequency_max_hz",2000000}}});
            auto hdr=c;hdr.frequency=470500000;hdr.receiver_settings={{"hdr_ctrl",true}};
            require(!planReceiverConfiguration(advanced,hdr).valid(),"RSP plugin allowed HDR outside its RF range");
            const auto repair=recommendMonitorConfiguration(advanced,hdr,lc,true,true);
            require(repair && repair->receiver.receiver_settings.at("hdr_ctrl")==false,"Plugin recommendation did not repair an advanced parameter conflict");
            hdr.frequency=1000000;require(planReceiverConfiguration(advanced,hdr).valid(),"RSP plugin rejected HDR within its RF range");
        }
        lc.search_width=10000000;const auto full=planMonitorConfiguration(rsp,c,lc,true,true,true);
        require(full.valid() && full.lora_channels>1 && full.maximum_search_width_hz==10000000 && has(full,"filter_rolloff"),"Full RSP span lacks analysis coverage or is incorrectly limited by IF 3 dB width");
        c.if_bandwidth_hz=1536000;require(planReceiverConfiguration(rsp,c).hardwareValid(),"Independent IF bandwidth was treated as a sampling-rate limit");
        c.if_bandwidth_hz=1900000;require(!planReceiverConfiguration(rsp,c).hardwareValid(),"Unreported IF filter accepted");c.if_bandwidth_hz=0;
        c.antenna="Antenna C";require(has(planReceiverConfiguration(rsp,c),"antenna_frequency"),"Antenna-dependent tuning limit missing");
        c.frequency=200000000;require(planReceiverConfiguration(rsp,c).hardwareValid(),"Antenna C rejected at its limit");
        c.frequency=200000001;require(!planReceiverConfiguration(rsp,c).hardwareValid(),"Antenna C upper boundary not enforced");
        c=Config{};c.auto_gain=true;
        const auto aio=rtlReceiverCapabilities(5,{197},"HackerGadgets","AIO_V2 Ext");
        require(aio.direct_sampling_supported && aio.model.find("AIO V2")!=std::string::npos && aio.frequency_source=="board_specification" && aio.frequency_ranges.front().maximum==1.74e9,"AIO2 board identity did not expose its attributed RF/HF paths");
        c.frequency=7100000;c.auto_gain=false;c.gain_tenths=123;
        require(receiverInputMode(aio,c)=="direct_q" && planReceiverConfiguration(aio,c).valid(),"HF automatic input or inactive tuner gain validation failed");
        require(!planReceiverConfiguration(rtl,c).valid(),"Generic RTL tuner was assigned an unproven HF board path");
        c.input_mode="tuner";require(!planReceiverConfiguration(aio,c).valid(),"HF was accepted through the tuner");c.input_mode="auto";
        lc.search_width=2000000;
        const auto aioRecommended=recommendMonitorConfiguration(aio,c,lc,true,true);
        require(aioRecommended && aioRecommended->receiver.rate==2560000 && aioRecommended->search_width_hz==2560000,"AIO2 full-rate recommendation remained restricted");
        c.frequency=470500000;c.auto_gain=true;
        for(auto rate:{2048000u,2400000u,2560000u,3200000u}){c.rate=rate;lc.search_width=rate;require(planMonitorConfiguration(aio,c,lc,true,true,true).valid(),"AIO2 acquisition rate is blocked");}
        c=Config{};c.auto_gain=true;
        auto unknown=rtlReceiverCapabilities(0,{});
        require(has(planReceiverConfiguration(unknown,c),"frequency_unknown") && planReceiverConfiguration(unknown,c).hardwareValid(),
                "Unknown tuner ranges were fabricated or treated as measured limits");
        std::cout<<"Receiver capability provenance, parameter combinations and software limits passed.\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
