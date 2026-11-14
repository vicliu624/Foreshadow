#include "signalscope/adapters/modulation/descriptor.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "snapshot_registry.hpp"
#include <mutex>

namespace signalscope {
namespace {
std::mutex descriptorMutex;
Json text(const std::string& key){return {{"message_key",key}};}
Json presentation(const std::string& id,const std::string& profile,const DetectorContext& context) {
    Json result={{"label_key","mod."+id+".label"},{"covered",true},
        {"parsing",text("mod."+profile+".parsing")},{"effect",text("mod."+profile+".effect")},
        {"parsing_summary",text("mod."+profile+".summary")},{"impact_summary",text("mod."+profile+".impact")}};
    const auto arguments=Json{{"channels",context.analysis_channels},{"analysis_rate",context.analysis_rate/1e6}};
    for(auto& value:result)if(value.is_object())value["arguments"]=arguments;
    return result;
}
Json translate(Json value) {
    static const auto catalog=TextCatalog::resource("modulation");catalog.resolve(value);return value;
}
std::vector<ModulationDescriptor> builtins() {
    std::vector<ModulationDescriptor> result;
    result.push_back({"lora","lora",DetectorStage::lora_stream,[](DetectorSelection& s)->bool&{return s.lora;},
        [](const DetectorContext& c){
            auto data=presentation("lora","lora",c);std::string bws;
            const auto choices=c.lora.bandwidth_candidates.empty()?std::vector<double>{c.lora.bandwidth}:c.lora.bandwidth_candidates;
            for(auto bw:choices){if(!bws.empty())bws+='/';bws+=std::to_string(static_cast<int>(bw/1000));}
            data["effect"]["arguments"]={{"bandwidths",bws},{"sf_min",c.lora.sf_min},{"sf_max",c.lora.sf_max}};
            return translate(std::move(data));
        }});
    for(const auto& option:narrowband_options) {
        const auto id=std::string(option.id);const auto profile=option.enabled==&NarrowbandSelection::broadcast_fm?"fm":"narrow";
        auto describe=[id,profile](const DetectorContext& c){return translate(presentation(id,profile,c));};
        ModulationDescriptor descriptor{id,id,DetectorStage::narrowband_stream,
            [flag=option.enabled](DetectorSelection& s)->bool&{return s.narrowband.*flag;},describe};
        if(option.enabled==&NarrowbandSelection::broadcast_fm)descriptor.describe=[id](const DetectorContext& c) {
            auto data=presentation(id,"fm",c);
            const auto radius=std::min(c.lora.search_width/2,std::max(0.,c.maximum_span_hz/2-100000));
            data["covered"]=c.receiver.frequency+radius>=65e6 && c.receiver.frequency-radius<=110e6;
            data["coverage_reason"]=text("mod.fm.outside");return translate(std::move(data));
        };
        result.push_back(std::move(descriptor));
    }
    for(const auto& option:legacySnapshotOptions()) {
        const auto profile=option.id=="ofdm"?"ofdm":option.id=="css"?"css":option.id=="4-fsk"?"four_fsk":"psk";
        result.push_back({option.id,option.id=="4-fsk"?"four-fsk":option.id,DetectorStage::snapshot,
            [enabled=option.enabled](DetectorSelection& s)->bool&{return enabled(s.snapshot);},
            [id=option.id,profile](const DetectorContext& c){return translate(presentation(id,profile,c));}});
    }
    for(const auto& extension:builtinSnapshotExtensions())result.push_back(extension.descriptor);
    return result;
}
std::vector<ModulationDescriptor>& registry(){static auto value=builtins();return value;}
}
std::vector<ModulationDescriptor> modulationDescriptors(){std::lock_guard lock(descriptorMutex);return registry();}
void registerModulationDescriptor(ModulationDescriptor descriptor) {
    if(descriptor.id.empty() || !descriptor.enabled || !descriptor.describe)throw std::invalid_argument("Incomplete modulation descriptor");
    std::lock_guard lock(descriptorMutex);
    for(const auto& existing:registry())if(existing.id==descriptor.id)throw std::invalid_argument("Duplicate modulation descriptor: "+descriptor.id);
    registry().push_back(std::move(descriptor));
}
}
