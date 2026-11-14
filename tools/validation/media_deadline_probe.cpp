// Formal-release platform diagnostic, independent of codecs and Qt.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>
#include <cerrno>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>

using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
template<class Wait> void observe(const char* name,Wait wait) {
    for(unsigned trial=0;trial<3;++trial) {
        std::vector<long long> values;
        for(unsigned i=0;i<32;++i) {
            const auto start=Clock::now();wait(start+24ms);
            values.push_back(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
        }
        std::sort(values.begin(),values.end());
        std::cout<<"{\"wait\":\""<<name<<"\",\"trial\":"<<trial<<",\"requested_us\":24000,\"minimum_us\":"<<values.front()
                 <<",\"median_us\":"<<values[values.size()/2]<<",\"maximum_us\":"<<values.back()<<"}\n"<<std::flush;
    }
}
int main() {
    std::cout<<"interactive_qos_error="<<pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)<<'\n';
    std::mutex mutex;std::condition_variable changed;std::unique_lock lock(mutex);
    observe("std_condition_variable",[&](auto due){changed.wait_until(lock,due);});
    observe("pthread_relative",[&](auto due){
        const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(due-Clock::now()).count();
        const timespec delay{duration/1000000000,duration%1000000000};
        const auto error=pthread_cond_timedwait_relative_np(changed.native_handle(),mutex.native_handle(),&delay);
        if(error!=ETIMEDOUT && error!=0)throw std::runtime_error("relative wait failed");
    });
    semaphore_t semaphore;const auto created=semaphore_create(mach_task_self(),&semaphore,SYNC_POLICY_FIFO,0);
    if(created!=KERN_SUCCESS)return 2;
    observe("mach_semaphore",[&](auto due){
        const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(due-Clock::now()).count();
        const mach_timespec_t delay{static_cast<unsigned>(duration/1000000000),static_cast<clock_res_t>(duration%1000000000)};
        const auto error=semaphore_timedwait(semaphore,delay);
        if(error!=KERN_OPERATION_TIMED_OUT && error!=KERN_SUCCESS)throw std::runtime_error("semaphore wait failed");
    });
    semaphore_destroy(mach_task_self(),semaphore);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    observe("mach_absolute",[&](auto due){
        const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(due-Clock::now()).count();
        mach_wait_until(mach_absolute_time()+static_cast<std::uint64_t>(duration)*timebase.denom/timebase.numer);
    });
}
