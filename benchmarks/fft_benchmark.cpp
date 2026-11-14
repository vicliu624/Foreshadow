#include <fftw3.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
int main() {
    for(int n:{512,1024,2048,4096,8192,16384,32768,65536}) for(unsigned flags:{unsigned(FFTW_ESTIMATE),unsigned(FFTW_MEASURE)}) {
        auto* in=fftwf_alloc_complex(n);auto* out=fftwf_alloc_complex(n);
        if(!in||!out)return 1;
        fftwf_set_timelimit(.2);
        auto plan=fftwf_plan_dft_1d(n,in,out,FFTW_FORWARD,flags);
        if(!plan)return 1;
        for(int i=0;i<n;++i){in[i][0]=float(i%17)/17;in[i][1]=float(i%11)/11;}
        const int loops=16777216/n;
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<loops;++i)fftwf_execute(plan);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<n<<' '<<(flags==FFTW_ESTIMATE?"estimate":"measure")<<" us="<<elapsed*1e6/loops<<'\n';
        fftwf_destroy_plan(plan);fftwf_free(in);fftwf_free(out);
    }
}
