#pragma once
// Derived from dtmb-sdr IqResampler, commit 2cc94c012128d6fa3e8d6de456b3bb57e51990a9.
// Copyright (c) 2026 Ning. MIT terms are distributed in third_party/dtmb-sampler-LICENSE.
// Extensions: fractional decision phase, bounded origins and persistent workers.
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace signalscope::television::native {
class Workers {
public:
    explicit Workers(unsigned count):count_(std::clamp(count,1u,64u)) {
        try{for(unsigned i=1;i<count_;++i)threads_.emplace_back([this,i](std::stop_token stop){
            std::uint64_t previous=0;
            while(true) {
                std::unique_lock lock(mutex_);ready_.wait(lock,[&]{return stop.stop_requested() || generation_!=previous;});
                if(stop.stop_requested())return;previous=generation_;lock.unlock();
                execute(i);lock.lock();--pending_;finished_.notify_one();
            }
        });}catch(...){for(auto& thread:threads_)thread.request_stop();ready_.notify_all();threads_.clear();throw;}
    }
    ~Workers(){for(auto& thread:threads_)thread.request_stop();ready_.notify_all();for(auto& thread:threads_)thread.join();}
    void run(std::size_t count,const std::function<void(std::size_t,std::size_t)>& function,std::size_t threshold=16384) {
        if(count_==1 || count<threshold){function(0,count);return;}
        {std::lock_guard lock(mutex_);work_=function;items_=count;error_={};pending_=count_-1;++generation_;}
        ready_.notify_all();execute(0);
        std::unique_lock lock(mutex_);finished_.wait(lock,[this]{return pending_==0;});work_={};if(error_)std::rethrow_exception(error_);
    }
private:
    void execute(unsigned worker) noexcept {
        try{work_(items_*worker/count_,items_*(worker+1)/count_);}catch(...){std::lock_guard lock(mutex_);if(!error_)error_=std::current_exception();}
    }
    unsigned count_;std::vector<std::jthread> threads_;std::mutex mutex_;std::condition_variable ready_,finished_;
    std::uint64_t generation_=0;unsigned pending_=0;std::size_t items_=0;
    std::function<void(std::size_t,std::size_t)> work_;std::exception_ptr error_;
};

class SymbolSampler {
public:
    static constexpr unsigned phaseCount=16;
    SymbolSampler(std::uint32_t inputRate,std::uint32_t outputRate,unsigned phase=0,Workers* workers=nullptr,
                  std::uint64_t inputOrigin=0,std::uint64_t outputOrigin=0,std::uint64_t clockMilliHz=0)
        :inputRate_(clockMilliHz?clockMilliHz:std::uint64_t(inputRate)*1000),outputRate_(std::uint64_t(outputRate)*1000),base_(inputOrigin),received_(inputOrigin),produced_(outputOrigin),workers_(workers) {
        if(inputRate<1 || inputRate>100000000 || outputRate<1 || outputRate>100000000 || phase>=phaseCount)
            throw std::invalid_argument("resample.invalid_rate_or_phase");
        const auto nominal=std::uint64_t(inputRate)*1000;
        if(inputRate_<nominal-nominal/1000 || inputRate_>nominal+nominal/1000)throw std::invalid_argument("resample.clock_range");
        const auto ticks=inputRate_*phase/phaseCount;
        if(outputOrigin>(UINT64_MAX-ticks)/inputRate_)throw std::overflow_error("resample.origin_overflow");
        const auto time=outputOrigin*inputRate_+ticks;center_=time/outputRate_;fraction_=time%outputRate_;
        const double ratio=std::min(1.,double(outputRate_)/inputRate_);radius_=static_cast<std::size_t>(std::ceil(8/ratio));
        if(radius_>8192)throw std::invalid_argument("resample.filter_budget");
        const auto width=2*radius_+1;filters_.resize((bankPhases+1)*width);
        for(std::size_t p=0;p<=bankPhases;++p) {
            double sum=0;
            for(std::size_t k=0;k<width;++k){const auto value=srrc((double(k)-radius_-double(p)/bankPhases)*ratio);filters_[p*width+k]=float(value);sum+=value;}
            for(std::size_t k=0;k<width;++k)filters_[p*width+k]/=float(sum);
        }
    }
    void process(std::span<const float> input,std::vector<float>& output) {
        if(finished_ || input.size()%2)throw std::invalid_argument("resample.invalid_input_state");
        if(input.size()/2>UINT64_MAX-received_)throw std::overflow_error("resample.input_overflow");
        history_.insert(history_.end(),input.begin(),input.end());received_+=input.size()/2;emit(false,output);
    }
    void finish(std::vector<float>& output){if(!finished_){emit(true,output);history_.clear();finished_=true;}}
private:
    static double srrc(double t) {
        constexpr double a=static_cast<double>(.05F);const auto pi=std::numbers::pi_v<double>;
        if(std::abs(t)<1e-12)return 1-a+4*a/pi;
        if(std::abs(std::abs(4*a*t)-1)<1e-9)return a/std::sqrt(2.)*((1+2/pi)*std::sin(pi/(4*a))+(1-2/pi)*std::cos(pi/(4*a)));
        return (std::sin(pi*t*(1-a))+4*a*t*std::cos(pi*t*(1+a)))/(pi*t*(1-16*a*a*t*t));
    }
    void emit(bool final,std::vector<float>& output) {
        const auto limit=final?received_:received_>radius_?received_-radius_:0;
        if(center_>=limit)return;
        if(limit-center_>UINT64_MAX/outputRate_)throw std::overflow_error("resample.block_overflow");
        const auto remaining=(limit-center_)*outputRate_-fraction_;
        const auto count=(remaining-1)/inputRate_+1;const auto offset=output.size();
        if(count>(output.max_size()-offset)/2)throw std::overflow_error("resample.output_budget");
        output.resize(offset+std::size_t(count)*2);
        const auto range=[&](std::size_t first,std::size_t last){emitRange(first,last,std::span(output).subspan(offset));};
        if(workers_)workers_->run(count,range);else range(0,count);
        if(count>(UINT64_MAX-fraction_)/inputRate_)throw std::overflow_error("resample.block_overflow");
        const auto time=fraction_+count*inputRate_;center_+=time/outputRate_;fraction_=time%outputRate_;produced_+=count;
        const auto keep=std::max(base_,std::min(received_,center_>radius_?center_-radius_:0));
        history_.erase(history_.begin(),history_.begin()+std::ptrdiff_t((keep-base_)*2));base_=keep;
    }
    void emitRange(std::size_t first,std::size_t last,std::span<float> output) const {
        const auto tick=fraction_+std::uint64_t(first)*inputRate_;auto center=center_+tick/outputRate_,fraction=tick%outputRate_;
        const auto width=2*radius_+1;
        for(auto index=first;index<last;++index) {
            const double phase=double(fraction)*bankPhases/outputRate_;const auto p=std::size_t(phase);const auto blend=float(phase-p);
            float i=0,q=0;
            for(std::size_t k=0;k<width;++k) {
                if(center+k<radius_)continue;const auto source=center+k-radius_;
                if(source<base_ || source>=received_)continue;
                const auto h0=filters_[p*width+k],h1=filters_[(p+1)*width+k],h=h0+(h1-h0)*blend;
                const auto at=std::size_t(source-base_)*2;i+=history_[at]*h;q+=history_[at+1]*h;
            }
            output[index*2]=i;output[index*2+1]=q;
            fraction+=inputRate_;center+=fraction/outputRate_;fraction%=outputRate_;
        }
    }
    static constexpr std::size_t bankPhases=1024;
    std::uint64_t inputRate_,outputRate_;std::size_t radius_=0;
    std::vector<float> filters_,history_;std::uint64_t base_,received_,produced_,center_=0,fraction_=0;
    Workers* workers_;bool finished_=false;
};
}
