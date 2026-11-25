#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Version.h>
#include <SoapySDR/Errors.hpp>
#include <SoapySDR/Logger.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <thread>
namespace {
const bool autoloadMarker=[] {
    if(const auto* name=std::getenv("AMC_TEST_LOCAL_DRIVER_MARKER"))
        if(auto* file=std::fopen(name,"ab")){std::fputs("local hardware module loaded\n",file);std::fclose(file);}
    return true;
}();
bool deviceClaimed=false;
bool releasedCleanly=true;
class Fake : public SoapySDR::Device {
    using Clock=std::chrono::steady_clock;
    double rate_=2000000, frequency_=470000000, bandwidth_=200000;
    std::size_t seen_=0;
    std::string antenna_="Antenna A";
    bool autoGain_=true;
    double ifReduction_=0;
    double phase_=0;
    bool streamOpen_=false;
    std::atomic_bool active_=false;
    bool injectedTimeout_=false;
    const bool paced_=std::getenv("AMC_TEST_SOAPY_REALTIME")!=nullptr;
    Clock::time_point started_;
public:
    Fake(){deviceClaimed=true;}
    ~Fake() override {
        releasedCleanly=!streamOpen_ && !active_;
        deviceClaimed=false;
    }
    std::string getDriverKey() const override{return "sdrplay";}
    std::string getHardwareKey() const override{return "RSPdx-R2";}
    SoapySDR::Kwargs getHardwareInfo() const override{return {{"sdrplay_api_api_version","3.150000"}};}
    std::size_t getNumChannels(int) const override{return 1;}
    std::string getNativeStreamFormat(int,std::size_t,double& scale) const override{scale=32767;return "CS16";}
    std::vector<std::string> listAntennas(int,std::size_t) const override{return {"Antenna A","Antenna B","Antenna C"};}
    void setAntenna(int,std::size_t,const std::string& name) override{antenna_=name;}
    std::string getAntenna(int,std::size_t) const override{return antenna_;}
    std::vector<double> listSampleRates(int,std::size_t) const override{return {2000000,2048000,3000000,6000000,10000000};}
    SoapySDR::RangeList getSampleRateRange(int,std::size_t) const override{return {{2000000,10660000}};}
    SoapySDR::RangeList getFrequencyRange(int,std::size_t) const override{return {{1000,2000000000}};}
    std::vector<double> listBandwidths(int,std::size_t) const override{return {200000,300000,600000,1536000,5000000,6000000,7000000,8000000};}
    void setBandwidth(int,std::size_t,double bw) override{if(rate_!=3100000)bandwidth_=bw;}
    double getBandwidth(int,std::size_t) const override{return bandwidth_;}
    SoapySDR::Range getGainRange(int,std::size_t,const std::string&) const override{return {20,59};}
    void setSampleRate(int,std::size_t,double rate) override{rate_=rate;}
    double getSampleRate(int,std::size_t) const override{return rate_;}
    void setFrequency(int,std::size_t,double f,const SoapySDR::Kwargs&) override{frequency_=f;}
    double getFrequency(int,std::size_t) const override{return frequency_;}
    void setGainMode(int,std::size_t,bool enabled) override{autoGain_=enabled;}
    bool getGainMode(int,std::size_t) const override{return autoGain_;}
    double getGain(int,std::size_t,const std::string&) const override{return ifReduction_;}
    void setGain(int,std::size_t,const std::string& name,double value) override{
        if(autoGain_ || name!="IFGR")throw std::runtime_error("Unexpected manual IF gain configuration");
        ifReduction_=value;
    }
    SoapySDR::Stream* setupStream(int,const std::string&,const std::vector<std::size_t>&,const SoapySDR::Kwargs&) override{
        if(streamOpen_)throw std::runtime_error("Stream already open");
        streamOpen_=true;
        return reinterpret_cast<SoapySDR::Stream*>(this);
    }
    void closeStream(SoapySDR::Stream*) override{if(!active_)streamOpen_=false;}
    std::size_t getStreamMTU(SoapySDR::Stream*) const override{return 4096;}
    int activateStream(SoapySDR::Stream*,int,long long,std::size_t) override{
        const auto widths=listBandwidths(0,0);
        if(!streamOpen_ || frequency_==473000000 || (!autoGain_ && ifReduction_<=0) ||
           std::find(widths.begin(),widths.end(),bandwidth_)==widths.end())
            return SOAPY_SDR_STREAM_ERROR;
        started_=Clock::now();
        active_=true;
        return 0;
    }
    int deactivateStream(SoapySDR::Stream*,int,long long) override{active_=false;return 0;}
    int readStream(SoapySDR::Stream*,void* const* buffers,std::size_t count,int& flags,long long&,long) override{
        // SoapyRemote owns a forwarding worker before activateStream. An idle
        // hardware reader waits for activation instead of emitting fake data.
        if(!active_){std::this_thread::sleep_for(std::chrono::milliseconds(1));return SOAPY_SDR_TIMEOUT;}
        if(frequency_==476000000 && !injectedTimeout_){
            injectedTimeout_=true;
            return SOAPY_SDR_TIMEOUT;
        }
        if(frequency_==471000000 && seen_>0)return SOAPY_SDR_OVERFLOW;
        if(frequency_==472000000 && seen_>0)flags=SOAPY_SDR_END_ABRUPT;
        count=std::min<std::size_t>(count,4096);
        if(paced_) {
            const auto elapsed=std::chrono::duration<double>((seen_+count)/rate_);
            std::this_thread::sleep_until(started_+std::chrono::duration_cast<Clock::duration>(elapsed));
        }
        auto* data=static_cast<std::int16_t*>(buffers[0]);
        const std::int16_t inPhase=autoGain_ || ifReduction_==32?-2345:0;
        for(std::size_t i=0;i<count;++i){
            if(frequency_==475000000) {
                constexpr std::size_t start=2*16384+512,symbolPairs=833,symbols=18;
                // The second burst also exercises desktop capture after its settling interval.
                constexpr std::size_t later=start+200000;
                const auto sample=seen_+i;
                const auto burst=sample>=later?later:start;
                if(sample>=burst && sample<burst+symbols*symbolPairs) {
                    const auto symbol=(sample-burst)/symbolPairs;
                    const auto offset=125000+(symbol%2?4800:-4800);
                    phase_+=2*std::numbers::pi*offset/rate_;
                    data[2*i]=static_cast<std::int16_t>(6554*std::cos(phase_));
                    data[2*i+1]=static_cast<std::int16_t>(6554*std::sin(phase_));
                } else data[2*i]=data[2*i+1]=0;
                continue;
            }
            data[2*i]=frequency_==474000000?static_cast<std::int16_t>((seen_+i)%16384):inPhase;
            data[2*i+1]=6789;
        }
        seen_+=count;
        return static_cast<int>(count);
    }
};
SoapySDR::KwargsList find(const SoapySDR::Kwargs& args){
    if(args.count("amc_test_log_marker"))SoapySDR::log(SOAPY_SDR_INFO,"FORESHADOW_RESOURCE_LOG_READY");
    return {{{"driver","sdrplay"},{"serial","AMC_TEST"}}};
}
SoapySDR::Device* make(const SoapySDR::Kwargs& args){
    if(args.count("amc_test_reject_make"))throw std::runtime_error("Deliberately rejected remote construction");
    if(deviceClaimed || !releasedCleanly)throw std::runtime_error("The previous SDRplay stream did not release cleanly");
    return new Fake;
}
static SoapySDR::Registry registration("sdrplay",find,make,SOAPY_SDR_ABI_VERSION);
}
