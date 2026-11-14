#include "signalscope/adapters/receivers/receiver.hpp"
#include <rtl-sdr.h>
#include <iostream>
#include <cstring>
struct rtlsdr_dev {std::uint32_t rate=2000000,frequency=470500000;int gain=197,direct=0;bool manual=false;};
namespace {
rtlsdr_dev device;
bool open=false;
bool aio=false,ignoreDirect=false;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
extern "C" {
uint32_t rtlsdr_get_device_count(){return 1;}
const char* rtlsdr_get_device_name(uint32_t){return "Test R820T";}
int rtlsdr_get_device_usb_strings(uint32_t,char* maker,char* product,char* serial){
    std::strcpy(maker,aio?"HackerGadgets":"Test");std::strcpy(product,aio?"AIO_V2 Ext":"R820T");std::strcpy(serial,"RTL_TEST");return 0;
}
int rtlsdr_open(rtlsdr_dev_t** result,uint32_t index){if(open || index)return -1;open=true;*result=&device;return 0;}
int rtlsdr_get_usb_strings(rtlsdr_dev_t*,char* maker,char* product,char* serial){return rtlsdr_get_device_usb_strings(0,maker,product,serial);}
int rtlsdr_set_direct_sampling(rtlsdr_dev_t*,int value){if(!ignoreDirect)device.direct=value;return 0;}
int rtlsdr_get_direct_sampling(rtlsdr_dev_t*){return device.direct;}
int rtlsdr_close(rtlsdr_dev_t*){open=false;return 0;}
enum rtlsdr_tuner rtlsdr_get_tuner_type(rtlsdr_dev_t*){return RTLSDR_TUNER_R820T;}
int rtlsdr_get_tuner_gains(rtlsdr_dev_t*,int* gains){if(gains){gains[0]=0;gains[1]=197;gains[2]=280;}return 3;}
int rtlsdr_set_center_freq(rtlsdr_dev_t*,uint32_t value){if(value!=473000000)device.frequency=value;return 0;}
uint32_t rtlsdr_get_center_freq(rtlsdr_dev_t*){return device.frequency;}
int rtlsdr_set_sample_rate(rtlsdr_dev_t*,uint32_t value){device.rate=value;return 0;}
uint32_t rtlsdr_get_sample_rate(rtlsdr_dev_t*){return device.rate;}
int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t*,int value){device.manual=value!=0;return 0;}
int rtlsdr_set_tuner_gain(rtlsdr_dev_t*,int value){if(!device.manual)return -1;device.gain=value;return 0;}
int rtlsdr_get_tuner_gain(rtlsdr_dev_t*){return device.gain;}
int rtlsdr_reset_buffer(rtlsdr_dev_t*){return 0;}
int rtlsdr_cancel_async(rtlsdr_dev_t*){return 0;}
int rtlsdr_read_async(rtlsdr_dev_t*,rtlsdr_read_async_cb_t callback,void* context,uint32_t,uint32_t){
    unsigned char bytes[4096];std::memset(bytes,128,sizeof(bytes));callback(bytes,sizeof(bytes),context);return 0;
}
}
namespace signalscope {const ReceiverBackend& rtlsdr_receiver();}
int main() {
    using namespace signalscope;
    try {
        const auto& receiver=rtlsdr_receiver();
        const auto probe=receiver.probe(0,{});const ReceiverCapabilities caps=probe.at("capabilities");
        require(caps.model=="R820T" && caps.manual_gains_known && caps.frequency_source=="tuner_specification",
                "RTL driver query did not produce attributed tuner capabilities");
        Config c;c.rate=2400000;c.gain_tenths=280;
        auto applied=receiver.configure(c);
        require(!open && applied.at("actual").at("sample_rate_hz")==2400000 &&
                applied.at("actual").at("gain_tenths_db")==280 && applied.at("actual").at("auto_gain").is_null(),
                "RTL apply failed to read back parameters or fabricated gain-mode readback");
        c.auto_gain=true;c.gain_tenths=123;
        applied=receiver.configure(c);
        require(applied.at("actual").at("gain_tenths_db").is_null() &&
                applied.at("gain_mode_confirmation")=="driver_acknowledged","RTL AGC retained an active manual gain");
        c.frequency=473000000;
        bool rejected=false;
        try {receiver.configure(c);}catch(const OperationError&){rejected=true;}
        require(rejected && !open,"RTL driver ignored a setting without the adapter rejecting its readback");
        c.frequency=470500000;c.rate=2000000;c.seconds=.001;c.settle_seconds=0;
        bool ready=false;std::uint64_t pairs=0;
        const auto report=receiver.stream(c,[&](auto bytes,auto first){
            require(ready && first==pairs,"RTL samples arrived before confirmed configuration");pairs+=bytes.size()/2;
        },[]{return false;},[&](const ReceiverReadback& r){
            require(r.configuration.at("actual").at("sample_rate_hz")==2000000,"RTL stream readback missing");ready=true;
        });
        require(pairs==2000 && report.at("configuration").at("actual").at("frequency_hz")==470500000 && !open,
                "RTL stream and configure did not share applied configuration");
        aio=true;c.frequency=7100000;c.rate=2560000;c.auto_gain=false;c.gain_tenths=123;
        applied=receiver.configure(c);
        require(applied.at("actual").at("input_mode")=="direct_q" && applied.at("actual").at("gain_tenths_db").is_null(),"AIO2 HF path or inactive tuner gain was mishandled");
        ignoreDirect=true;c.input_mode="direct_i";rejected=false;
        try{receiver.configure(c);}catch(const OperationError&){rejected=true;}
        require(rejected && !open,"Direct sampling readback mismatch was ignored");
        std::cout<<"RTL query, hardware apply, mismatch rejection, AGC and stream readback passed.\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
