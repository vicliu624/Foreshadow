#include "signalscope/adapters/receivers/receiver.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/adapters/receivers/plugin_api.h"
#include "signalscope/adapters/receivers/plugin_support.hpp"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <set>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <dlfcn.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace signalscope {
namespace {
struct Module {
#ifdef _WIN32
    HMODULE handle=nullptr;
    explicit Module(const std::filesystem::path& p) {
        // A malformed optional module must report rejection, not block startup
        // on the loader's modal error dialog. Preserve the calling thread's mode.
        DWORD previous=0;
        if(!SetThreadErrorMode(GetThreadErrorMode()|SEM_FAILCRITICALERRORS,&previous))
            throw std::system_error(GetLastError(),std::system_category(),"Could not configure receiver module loading");
        handle=LoadLibraryExW(p.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        SetThreadErrorMode(previous,nullptr);
    }
    void* symbol(){return reinterpret_cast<void*>(GetProcAddress(handle,"signalscope_receiver_plugin_v1"));}
    ~Module(){if(handle)FreeLibrary(handle);}
#else
    void* handle=nullptr;
    explicit Module(const std::filesystem::path& p):handle(dlopen(p.c_str(),RTLD_NOW|RTLD_LOCAL)) {}
    void* symbol(){return dlsym(handle,"signalscope_receiver_plugin_v1");}
    ~Module(){if(handle)dlclose(handle);}
#endif
};
struct Reply {
    Json value;std::exception_ptr error;bool received=false;
    static void accept(void* p,const char* data,std::size_t size) noexcept {
        auto& r=*static_cast<Reply*>(p);
        try {if(r.received || size>16*1024*1024)throw std::runtime_error("Invalid receiver plugin response size/count");
            r.received=true;r.value=Json::parse(data,data+size);
        }catch(...){r.error=std::current_exception();}
    }
    Json finish(int status) {
        if(error)std::rethrow_exception(error);
        if(!received)throw std::runtime_error("Receiver plugin did not return a response");
        if(status) {
            const auto message=value.value("error",std::string("Receiver plugin failed"));
            const auto kind=value.value("kind",std::string());
            if(kind=="source")throw SourceError(message,value.at("report"));
            if(kind=="operation")throw OperationError(static_cast<ErrorCode>(value.at("code").get<int>()),message);
            throw std::invalid_argument(message);
        }
        return std::move(value);
    }
};
struct Loaded {
    bool managed=false;
    std::unique_ptr<Module> module;
    const AmcReceiverPluginV1* api=nullptr;
    Json metadata;
    TextCatalog catalog;
    std::array<std::string,5> strings;
    std::vector<int> gains;std::vector<std::uint32_t> rates;
    ReceiverBackend backend;
    int priority=1000;
    void localize(Json& node) const {catalog.resolve(node);}
    Json call(const char* op,const Json& payload) const {
        const auto text=payload.dump();Reply reply;
        const int status=api->request(op,text.c_str(),&reply,Reply::accept);return reply.finish(status);
    }
    Json stream(const Config& cfg,const BlockSink& sink,const std::function<bool()>& cancelled,const ReceiverReady& ready) const {
        struct Context {
            const Config& config;const BlockSink& sink;const std::function<bool()>& cancelled;const ReceiverReady& ready;
            Reply reply;std::exception_ptr error;std::uint64_t next=0;std::size_t stride;bool announced=false;
            std::atomic<bool> failed=false;std::mutex error_mutex;
            Context(const Config& c,const BlockSink& s,const std::function<bool()>& cancel,const ReceiverReady& r,std::size_t bytes)
                :config(c),sink(s),cancelled(cancel),ready(r),stride(bytes) {}
            void fail() {
                std::lock_guard lock(error_mutex);if(!error)error=std::current_exception();failed=true;
            }
        } context{cfg,sink,cancelled,ready,bytes_per_pair(backend.format)};
        const auto text=receiver_plugin::encodeConfig(cfg).dump();
        const int status=api->stream(text.c_str(),&context,
            [](void* p,const std::uint8_t* bytes,std::size_t count,std::uint64_t first)->int {
                auto& c=*static_cast<Context*>(p);if(c.failed)return 1;
                try {
                    if(!bytes || !count || count%c.stride || count>c.config.block_bytes || first!=c.next)
                        throw OperationError(ErrorCode::receiver_stream,"Plugin returned discontinuous or invalid I/Q");
                    c.sink({bytes,count},first);c.next+=count/c.stride;return 0;
                }catch(...){c.fail();return 1;}
            },[](void* p,const char* json,std::size_t length)->int {
                auto& c=*static_cast<Context*>(p);if(c.failed)return 1;
                try {
                    if(!json || length>256*1024 || c.announced || c.next)throw std::invalid_argument("Invalid receiver startup report size or ordering");
                    const auto j=Json::parse(json,json+length);const auto version=j.value("protocol_version",Json(1));
                    if(!version.is_number_integer() || (version!=1 && version!=receiverReadbackProtocolVersion) || (version==2 && !j.contains("acquisition_conditions")))throw std::invalid_argument("Unsupported receiver readback protocol");
                    const auto conditions=j.contains("acquisition_conditions")?j.at("acquisition_conditions").get<AcquisitionConditions>():AcquisitionConditions{};
                    c.announced=true;
                    if(c.ready)c.ready({j.at("sample_rate_hz"),j.at("frequency_hz"),j.value("filter_bandwidth_hz",0.),j.at("configuration"),conditions});
                    return 0;
                }catch(...){c.fail();return 1;}
            },[](void* p)->int {
                auto& c=*static_cast<Context*>(p);
                try{return c.failed || c.cancelled();}catch(...){c.fail();return 1;}
            },[](void* p,const char* json,std::size_t length){Reply::accept(&static_cast<Context*>(p)->reply,json,length);});
        if(context.error)std::rethrow_exception(context.error);
        return context.reply.finish(status);
    }
};
struct Registry {
    std::vector<std::unique_ptr<Loaded>> loaded;
    std::vector<const ReceiverBackend*> backends;
    Json diagnostics=Json::array();
    Registry() {
        std::vector<std::filesystem::path> paths;
        std::map<std::filesystem::path,distribution::ActiveComponent> managed;
        std::set<std::filesystem::path> managedDirectories;
        std::set<std::string> disabled;
        if(const auto env=environment_utf8("SIGNALSCOPE_RECEIVER_PLUGIN_PATH")) {
            std::string list(*env);
#ifdef _WIN32
            constexpr char separator=';';
#else
            constexpr char separator=':';
#endif
            std::size_t start=0;
            while(start<list.size()) {const auto end=list.find(separator,start);const auto item=list.substr(start,end-start);
                if(!item.empty())paths.push_back(path_from_utf8(item));
                if(end==std::string::npos)break;
                start=end+1;
            }
        } else {
            try {
                disabled=distribution::runtimeDisabledComponents("receiver");
                for(const auto& component:distribution::runtimeComponents("receiver")) {
                    const auto path=std::filesystem::weakly_canonical(component.path);managed[path]=component;
                    if(managedDirectories.insert(path.parent_path()).second)paths.push_back(path.parent_path());
                }
            }catch(const std::exception& error){diagnostics.push_back({{"status","rejected"},{"origin","toolbox_registry"},{"error",error.what()}});}
            const auto exe=application_directory();
            for(const auto& path:{exe/"receiver-plugins",exe/"../receiver-plugins",exe/"../lib/signalscope/receivers"})paths.push_back(path);
            if(const auto home=environment_utf8("HOME"))paths.push_back(path_from_utf8(*home)/".local/lib/signalscope/receivers");
        }
        std::set<std::filesystem::path> visited;
        for(const auto& directory:paths) {
            std::error_code ec;if(!std::filesystem::is_directory(directory,ec))continue;
            std::vector<std::filesystem::path> files;
            for(const auto& entry:std::filesystem::directory_iterator(directory)) {
                const auto path=entry.path();const auto name=path_to_utf8(path.filename());
                if(managedDirectories.contains(directory) && !managed.contains(std::filesystem::weakly_canonical(path)))continue;
                if(name.starts_with("amc-receiver-") && (path.extension()==".so" || path.extension()==".dll" || path.extension()==".dylib"))files.push_back(path);
            }
            std::sort(files.begin(),files.end());
            for(const auto& path:files) {
                const auto resolved=std::filesystem::weakly_canonical(path);if(!visited.insert(resolved).second)continue;
                try {
                    auto item=std::make_unique<Loaded>();item->module=std::make_unique<Module>(resolved);
                    if(!item->module->handle)throw std::runtime_error("Could not load receiver module or its driver dependencies");
                    const auto entry=reinterpret_cast<AmcReceiverEntryV1>(item->module->symbol());
                    if(!entry)throw std::runtime_error("Missing receiver plugin entry point");
                    item->api=entry();const auto* api=item->api;
                    if(!api || api->abi_version!=1 || api->struct_size<sizeof(AmcReceiverPluginV1) || !api->metadata_json || !api->request || !api->stream)
                        throw std::runtime_error("Incompatible receiver plugin ABI");
                    item->metadata=Json::parse(api->metadata_json);auto& m=item->metadata;
                    const auto requestProtocol=m.value("request_protocol",Json(1));
                    if(!requestProtocol.is_number_integer() || (requestProtocol!=1 && requestProtocol!=2) ||
                       (requestProtocol==2 && (!m.contains("connection_parameters") || !m.at("connection_parameters").is_array())))
                        throw std::runtime_error("Unsupported receiver request protocol");
                    item->strings={m.at("source"),m.at("recording_source"),m.at("default_gain"),m.at("id"),m.at("name")};
                    if(item->strings[3].empty() || item->strings[3].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")!=std::string::npos)
                        throw std::runtime_error("Invalid receiver plugin ID");
                    const auto owner=managed.find(resolved);
                    if(owner!=managed.end() && owner->second.id!=item->strings[3])throw std::runtime_error("Toolbox component identity does not match receiver module");
                    if(disabled.contains(item->strings[3])){diagnostics.push_back({{"id",item->strings[3]},{"path",path_to_utf8(resolved)},{"status","disabled"}});continue;}
                    bool superseded=false;
                    for(const auto& previous:loaded)if(previous->strings[3]==item->strings[3]) {
                        if(previous->managed)superseded=true;else throw std::runtime_error("Duplicate receiver plugin ID");
                    }
                    if(superseded){diagnostics.push_back({{"id",item->strings[3]},{"path",path_to_utf8(resolved)},{"status","superseded"}});continue;}
                    item->managed=owner!=managed.end();
                    auto boundary=owner!=managed.end()?owner->second.root:resolved.parent_path();
                    if(owner==managed.end())for(auto folder=resolved.parent_path();folder!=folder.parent_path();folder=folder.parent_path()) {
                        if(std::filesystem::is_regular_file(folder/"toolbox.json") ||
                           (folder.extension()==".app" && std::filesystem::is_regular_file(folder/"Contents/Info.plist"))){boundary=folder;break;}
                    }
                    boundary=std::filesystem::weakly_canonical(boundary);
                    const auto catalogs=m.value("catalogs",std::vector<std::string>{});
                    bool catalogLoaded=catalogs.empty();
                    for(const auto& location:m.value("catalog_roots",std::vector<std::string>{"."})) {
                        const auto relative=path_from_utf8(location);
                        if(relative.empty() || relative.is_absolute())throw std::runtime_error("Invalid provider catalog root");
                        const auto base=std::filesystem::weakly_canonical(resolved.parent_path()/relative);
                        const auto within=base.lexically_relative(boundary);
                        if(within.empty() || *within.begin()=="..")continue;
                        if(std::all_of(catalogs.begin(),catalogs.end(),[&](const auto& name){return std::filesystem::is_directory(base/path_from_utf8(name));})) {
                            item->catalog.load(base,catalogs,boundary);catalogLoaded=true;break;
                        }
                    }
                    if(!catalogLoaded)throw std::runtime_error("Provider catalog directories missing inside plugin package");
                    const auto format=m.at("format").get<std::string>();
                    const auto encoding=sample_format_from_name(format);
                    auto* l=item.get();auto& b=l->backend;
                    b.source=l->strings[0].c_str();b.recording_source=l->strings[1].c_str();b.default_gain=l->strings[2].c_str();
                    b.id=l->strings[3].c_str();b.display_name=l->strings[4].c_str();b.format=encoding;
                    l->gains=m.value("gain_presets",std::vector<int>{});l->rates=m.value("monitor_rates",std::vector<std::uint32_t>{});
                    b.gain_presets_tenths=l->gains;b.monitor_rates=l->rates;
                    b.gain_kind=m.value("gain_kind",std::string())=="if_reduction"?ReceiverGainKind::if_reduction:ReceiverGainKind::tuner;
                    l->priority=m.value("priority",1000);
                    const bool connectionContext=requestProtocol==2;
                    b.connected_devices=[l,connectionContext](const Json& settings){auto result=l->call("devices",connectionContext?Json{{"settings",settings}}:Json::object());l->localize(result);return result;};
                    b.devices=[l]{return l->backend.connected_devices(Json::object());};
                    b.connection_parameters=m.value("connection_parameters",Json::array());l->localize(b.connection_parameters);
                    b.connected_probe=[l,connectionContext](auto index,auto serial,const Json& settings){
                        Json request={{"device",index},{"serial",serial}};if(connectionContext)request["settings"]=settings;
                        auto result=l->call("probe",request);
                        const auto caps=result.at("capabilities").template get<ReceiverCapabilities>();
                        if(caps.backend!=l->backend.id || caps.format!=l->backend.format)
                            throw std::runtime_error("Receiver capabilities disagree with plugin identity/stream format");
                        l->localize(result);return result;
                    };
                    b.probe=[l](auto index,auto serial){return l->backend.connected_probe(index,serial,Json::object());};
                    b.configure=[l](const Config& c){return l->call("configure",{{"configuration",receiver_plugin::encodeConfig(c)}});};
                    b.plan=[l](const ReceiverCapabilities& caps,const Config& c){
                        auto plan=l->call("plan",{{"capabilities",caps},{"configuration",receiver_plugin::encodeConfig(c)}});
                        for(auto& issue:plan.at("issues"))if(!issue["values"].contains("message_key"))issue["values"]["message_key"]="issue."+issue.at("code").get<std::string>();
                        l->localize(plan);return plan.get<ReceiverPlan>();
                    };
                    b.parameters=[l](const ReceiverCapabilities& caps,const Config& c){auto fields=l->call("parameters",{{"capabilities",caps},{"configuration",receiver_plugin::encodeConfig(c)}});l->localize(fields);return fields;};
                    b.recommend=[l](const ReceiverCapabilities& caps,const Config& c){std::vector<Config> result;
                        for(const auto& j:l->call("recommend",{{"capabilities",caps},{"configuration",receiver_plugin::encodeConfig(c)}}))result.push_back(receiver_plugin::decodeConfig(j));
                        return result;
                    };
                    b.stream=[l](const auto& c,const auto& sink,const auto& cancelled,const auto& ready){return l->stream(c,sink,cancelled,ready);};
                    diagnostics.push_back({{"path",path_to_utf8(resolved)},{"id",b.id},{"status","loaded"},{"abi",1},
                        {"toolbox",owner==managed.end()?Json(nullptr):Json{{"id",owner->second.package_id},{"version",owner->second.version}}},
                        {"settings_prefix",m.value("settings_prefix",std::string("receiver/")+b.id+'/')}});
                    loaded.push_back(std::move(item));
                }catch(const std::exception& e){diagnostics.push_back({{"path",path_to_utf8(resolved)},{"status","rejected"},{"error",e.what()}});}
            }
        }
        std::stable_sort(loaded.begin(),loaded.end(),[](const auto& a,const auto& b){return a->priority<b->priority;});
        for(const auto& item:loaded)backends.push_back(&item->backend);
    }
};
Registry& registry(){static Registry value;return value;}
}
std::span<const ReceiverBackend* const> receiver_backends(){return registry().backends;}
Json receiver_plugin_diagnostics(){return registry().diagnostics;}
const ReceiverBackend& default_receiver() {
    if(receiver_backends().empty()) {
        // Offline commands and replay remain usable without installed drivers.
        static const ReceiverBackend offline{"unavailable","unavailable",
            []{return Json{{"backend","unavailable"},{"devices",Json::array()},{"reason_code","plugin_missing"}};},
            [](std::uint32_t,std::string_view)->Json{throw OperationError(ErrorCode::receiver_unavailable,"No compatible receiver plugins installed");},
            [](const Config&,const BlockSink&,const std::function<bool()>&,const ReceiverReady&)->Json{throw OperationError(ErrorCode::receiver_unavailable,"No compatible receiver plugins installed");},
            SampleFormat::cu8,"auto","unavailable","No receiver plugins",{},ReceiverGainKind::tuner,
            100000,1740000000,default_monitor_rates,{},{},{},{}};
        return offline;
    }
    return *receiver_backends().front();
}
const ReceiverBackend* find_receiver(std::string_view name) {
    for(const auto* backend:receiver_backends())if(name==backend->id)return backend;
    if(receiver_backends().empty() && name==default_receiver().id)return &default_receiver();
    return nullptr;
}
const ReceiverBackend& receiver_by_name(std::string_view name) {
    if(const auto* backend=find_receiver(name))return *backend;
    throw OperationError(ErrorCode::receiver_unavailable,"Receiver plugin is not installed: "+std::string(name));
}
ReceiverPlan planReceiverConfiguration(const ReceiverCapabilities& caps,const Config& cfg) {
    // A recorded file has no receiver settings or driver policy to apply.
    if(caps.backend=="recorded") {
        ReceiverPlan plan;plan.maximum_search_width_hz=cfg.rate;
        plan.bytes_per_second=double(cfg.rate)*bytes_per_pair(caps.format);return plan;
    }
    return receiver_by_name(caps.backend).plan(caps,cfg);
}
std::vector<std::uint32_t> receiverRateChoices(const ReceiverCapabilities& caps) {
    for(const auto& d:receiverParameters(caps,Config{}))
        if(d.value("key",std::string())=="sample_rate_hz" && d.contains("choices"))return d.at("choices").get<std::vector<std::uint32_t>>();
    return caps.sample_rates;
}
std::string receiverInputMode(const ReceiverCapabilities& caps,const Config& cfg){return planReceiverConfiguration(caps,cfg).effective_configuration.value("input_mode",cfg.input_mode);}
Json receiverParameters(const ReceiverCapabilities& caps,const Config& cfg){return receiver_by_name(caps.backend).parameters(caps,cfg);}
std::vector<Config> receiverRecommendations(const ReceiverCapabilities& caps,const Config& cfg){return receiver_by_name(caps.backend).recommend(caps,cfg);}
Json capture_receiver(const ReceiverBackend& receiver,const Config& cfg,Recorder& recorder,
                      const std::function<bool()>& cancelled) {
    if(cfg.seconds<=0) throw std::invalid_argument("Manual capture requires a finite duration. Use monitor for continuous operation");
    if(recorder.format()!=receiver.format)throw std::invalid_argument("Receiver and recording sample formats do not match");
    const auto stride=bytes_per_pair(receiver.format);
    Json startup={{"protocol_version",receiverReadbackProtocolVersion},{"acquisition_conditions",AcquisitionConditions{}}};
    try {
        auto result=receiver.stream(cfg,[&](auto bytes,auto first){
            if(first!=recorder.pairs() || bytes.empty() || bytes.size()%stride || bytes.size()>cfg.block_bytes)
                throw OperationError(ErrorCode::receiver_stream,"Input I/Q is discontinuous or has an invalid block length");
            recorder.append(bytes);
        },cancelled,[&](const ReceiverReadback& actual){startup={{"protocol_version",receiverReadbackProtocolVersion},{"sample_rate_hz",actual.sample_rate_hz},
            {"frequency_hz",actual.center_frequency_hz},{"filter_bandwidth_hz",actual.filter_bandwidth_hz},{"configuration",actual.configuration},{"acquisition_conditions",actual.conditions}};});
        result["startup_readback"]=startup;
        recorder.finish(result["status"].get<std::string>(),result);
        return result;
    } catch(const SourceError& e) {
        auto report=e.details;report["startup_readback"]=startup;recorder.finish("error",report);
        throw;
    } catch(const std::exception& e) {
        recorder.finish("error",{{"error",e.what()},{"startup_readback",startup}});
        throw;
    }
}
}
