#include "signalscope/backend/recordings/event_store.hpp"
#include <iostream>
#include <cmath>
#include <numbers>
using namespace signalscope;
void require(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
std::size_t event_dirs(const std::filesystem::path& path) {
    std::size_t n=0;for(const auto& item:std::filesystem::directory_iterator(path)) if(item.is_directory())++n;return n;
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("signalscope-store-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Config c;c.rate=250000;c.frequency=470500000;c.seconds=0;
        RetentionPolicy p;p.ring_seconds=.01;p.pre_seconds=.004;p.post_seconds=.001;
        p.ring_bytes=1000;p.max_event_seconds=.008;p.max_event_bytes=4000;p.quota_bytes=1024*1024;p.max_files=2;p.reserve_bytes=0;
        {
            EventStore store(root,c,p);
            bool locked=false;try{EventStore other(root,c,p);}catch(const std::exception&){locked=true;}
            require(locked,"concurrent stores must be rejected");
            Bytes block(200,128);
            for(std::uint64_t first=0;first<5000;first+=100)
                store.feed(block,first,Json::array({{{"evidence_level","css_candidate"}}}));
            require(event_dirs(root)==0,"CSS-only input must never save raw I/Q");
            require(store.status()["ring_bytes"].get<std::uint64_t>()<=1000,"ring byte limit");
            std::uint64_t next=5000;
            for(int event=0;event<4;++event) {
                Json hit={{"evidence_level","lora_structure_match"},{"first_observed_sample",next-200},{"confirmed_at_sample",next+100}};
                store.feed(block,next,Json::array({hit}));next+=100;
                for(int i=0;i<30;++i){store.feed(block,next,Json::array());next+=100;}
            }
            bool reached_limit=false;
            for(int i=0;i<50;++i) {
                Json hit={{"evidence_level","lora_structure_match"},{"first_observed_sample",next-200},{"confirmed_at_sample",next+100}};
                store.feed(block,next,Json::array({hit}));next+=100;
                // 检查刚结束的片段，后续容量淘汰不保证保留此测试证据。
                if(!reached_limit)for(const auto& item:std::filesystem::directory_iterator(root)) if(item.is_directory()) {
                    const auto data=item.path()/"capture.sigmf-data";
                    if(std::filesystem::file_size(data)==p.max_event_bytes) {
                        const auto info=inspect_recording(item.path()/"capture.sigmf-meta");
                        reached_limit=info.metadata["global"]["signalscope:status"]=="size_or_time_limit";
                    }
                }
            }
            store.finish();
            require(event_dirs(root)<=2,"max event count retention");
            require(store.status()["retention_deleted"].get<std::uint64_t>()>=2,"old events must expire by count");
            for(const auto& item:std::filesystem::directory_iterator(root)) if(item.is_directory()) {
                auto info=inspect_recording(item.path()/"capture.sigmf-meta");
                require(info.pairs*2<=p.max_event_bytes,"per-file byte bound");
                require(info.metadata["global"]["signalscope:result"]["pretrigger_truncated"]==true,"missing pretrigger must be explicit");
            }
            require(reached_limit,"continuous triggers must stop at the hard byte limit");
        }
        for(const auto& item:std::filesystem::directory_iterator(root)) if(item.is_directory())
            std::filesystem::last_write_time(item.path(),std::filesystem::file_time_type::clock::now()-std::chrono::hours(200));
        p.max_age_hours=168;
        {EventStore reopened(root,c,p);require(event_dirs(root)==0,"age retention across restart");}
        p.diagnostic_candidates=true;
        {
            EventStore diagnostic(root,c,p);Bytes block(2000,128);
            Json probe={{"evidence_level","chirp_probe"},{"first_observed_sample",0},{"confirmed_at_sample",1000}};
            diagnostic.feed(block,0,Json::array({probe}));
            for(std::uint64_t first=1000;first<501000;first+=1000)diagnostic.feed(block,first,Json::array());
            const auto pending=root/"pending-chirp";
            require(std::filesystem::exists(pending/"capture.sigmf-data"),"diagnostic candidate must be bounded and labelled");
            require(std::filesystem::file_size(pending/"capture.sigmf-data")<=8*1024*1024,"diagnostic size bound");
            write_json(pending/"signalscope-provisional.json",{{"owner","signalscope-provisional-v1"},{"expires_unix_seconds",0}});
            diagnostic.feed(block,501000,Json::array());
            require(!std::filesystem::exists(pending),"expired unconfirmed candidate must be deleted");
        }
        p.diagnostic_candidates=false;p.ring_seconds=.1;p.ring_bytes=65536;
        p.max_event_seconds=1;p.max_event_bytes=500000;p.quota_bytes=2*1024*1024;
        const auto adaptive=root/"adaptive";
        {
            EventStore store(adaptive,c,p);std::uint32_t random=12345;
            for(std::uint64_t first=0;first<50000;first+=500){
                Bytes block(1000);
                for(std::size_t n=0;n<500;++n)for(int component=0;component<2;++component){
                    random^=random<<13;random^=random>>17;random^=random<<5;
                    double value=(double(random&255)/255-.5)*8;
                    if(first>=5000&&first<25000){const double phase=2*std::numbers::pi*double(first+n)/8;value+=32*(component?std::sin(phase):std::cos(phase));}
                    block[n*2+component]=static_cast<std::uint8_t>(std::lround(127.5+value));
                }
                Json events=Json::array();
                if(first==5000)events.push_back({{"evidence_level","lora_structure_match"},{"first_observed_sample",5000},{"confirmed_at_sample",5500},{"center_frequency_hz",c.frequency+31250.0},{"bandwidth_hz",31250.0}});
                store.feed(block,first,events);
            }
            store.finish();require(event_dirs(adaptive)==1,"one verified event should create one recording");
            for(const auto& entry:std::filesystem::directory_iterator(adaptive))if(entry.is_directory()){
                const auto info=inspect_recording(entry.path()/"capture.sigmf-meta");
                const auto& result=info.metadata["global"]["signalscope:result"];
                require(result["stream_last_sample_exclusive"].get<std::uint64_t>()>=37500,"recording must extend beyond fixed post-trigger while signal remains active");
                require(result["quiet_tail_observed"]==true&&result["possibly_truncated"]==false,"quiet tail observation must be explicit");
                require(result["frame_crc_verified"]==false,"raw burst capture is not byte-level packet validation");
            }
        }
        {
            Config late_config;late_config.rate=1000000;
            RetentionPolicy late_policy;late_policy.reserve_bytes=0;
            const auto late_root=root/"late-result";EventStore store(late_root,late_config,late_policy);
            Bytes raw(12000000);std::uint32_t random=18371;
            for(auto& byte:raw){random=random*1664525u+1013904223u;byte=static_cast<std::uint8_t>(random>>24);}
            store.feed(raw,0,Json::array());
            store.feed({},6000000,Json::array({{{"evidence_level","lora_structure_match"},{"first_observed_sample",100000},{"confirmed_at_sample",200000}}}));
            store.finish();require(event_dirs(late_root)==1,"late result was not recorded");
            for(const auto& item:std::filesystem::directory_iterator(late_root))if(item.is_directory()){
                const auto saved=inspect_recording(item.path()/"capture.sigmf-meta");
                require(saved.pairs>=200000,"late recording does not cover the confirmed signal interval");
                require(saved.metadata["global"]["signalscope:result"]["stream_first_sample"]==0,"late result lost buffered packet head");
                require(saved.metadata["global"]["signalscope:result"]["pretrigger_truncated"]==false,"late packet head was truncated");
                Bytes actual(static_cast<std::size_t>(saved.pairs*2));std::ifstream input(saved.data_path,std::ios::binary);input.read(reinterpret_cast<char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
                require(actual.size()<=raw.size()&&std::equal(actual.begin(),actual.end(),raw.begin()),"late recording changed raw I/Q");
            }
            require(store.status()["ring_bytes"].get<std::uint64_t>()<=late_policy.ring_bytes,"late result ring exceeded memory cap");
        }
        {
            const auto tie_root=root/"retention-tie";
            auto tie_policy=p;
            tie_policy.max_files=1;
            tie_policy.max_age_hours=0;
            {EventStore initialized(tie_root,c,tie_policy);}
            const auto older=tie_root/"event-100-0";
            const auto newer=tie_root/"event-101-1";
            const auto same_time=std::filesystem::file_time_type::clock::now();
            for(const auto& path:{newer,older}) {
                std::filesystem::create_directory(path);
                write_json(path/"signalscope-event.json",{{"owner","signalscope-event-v1"}});
                std::filesystem::last_write_time(path,same_time);
            }
            EventStore reopened(tie_root,c,tie_policy);
            require(!std::filesystem::exists(older)&&std::filesystem::exists(newer),
                    "equal timestamps must retain the newest event by creation name");
        }
        std::filesystem::remove_all(root);
        std::cout<<"环形缓冲、仅 LoRa 保存、文件上限、配额/期限淘汰与互斥检查通过\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';std::filesystem::remove_all(root);return 1;}
}
