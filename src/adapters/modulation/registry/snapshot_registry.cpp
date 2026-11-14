#include "snapshot_registry.hpp"
#include "../dtmb/detector.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <cmath>

namespace signalscope {
namespace {
std::mutex extensionMutex;
std::vector<SnapshotDetectorOption> extensionOptions;
std::vector<snapshot::WidebandDetector> extensionDetectors;
}
bool SnapshotSelection::any() const {
    const auto enabled=[this](const auto& detector){return detector.enabled(*this);};
    return std::ranges::any_of(snapshot::widebandDetectors(),enabled) ||
           std::ranges::any_of(snapshot::burstDetectors(),enabled);
}

std::span<const SnapshotExtension> builtinSnapshotExtensions() {
    static const std::array providers{dtmb::extension()};return providers;
}
std::span<const SnapshotDetectorOption> legacySnapshotOptions() {
    static const std::array options{
        SnapshotDetectorOption{"bpsk","BPSK",[](SnapshotSelection& s)->bool&{return s.psk.bpsk;}},
        SnapshotDetectorOption{"qpsk","QPSK",[](SnapshotSelection& s)->bool&{return s.psk.qpsk;}},
        SnapshotDetectorOption{"4-fsk","4-FSK",[](SnapshotSelection& s)->bool&{return s.four_fsk;}},
        SnapshotDetectorOption{"ofdm","OFDM",[](SnapshotSelection& s)->bool&{return s.ofdm;}},
        SnapshotDetectorOption{"css","CSS",[](SnapshotSelection& s)->bool&{return s.css;}},
    };
    return options;
}
std::vector<SnapshotDetectorOption> snapshotDetectorOptions() {
    const auto options=legacySnapshotOptions();
    std::vector<SnapshotDetectorOption> result(options.begin(),options.end());
    for(const auto& extension:builtinSnapshotExtensions()) {
        const auto id=extension.descriptor.id;
        result.push_back({id,id,[id](SnapshotSelection& s)->bool&{return s.extensions[id];}});
    }
    std::lock_guard lock(extensionMutex);result.insert(result.end(),extensionOptions.begin(),extensionOptions.end());return result;
}
void registerSnapshotExtension(SnapshotExtension extension) {
    if(!extension.analyze || extension.descriptor.id.empty())throw std::invalid_argument("Invalid snapshot extension");
    const auto& observation=extension.observation;
    if(extension.protocol_version!=snapshotExtensionProtocolVersion ||
       (observation.trigger!=SnapshotTrigger::energy && observation.trigger!=SnapshotTrigger::periodic) ||
       !std::isfinite(observation.duration_seconds) || observation.duration_seconds<=0 || observation.duration_seconds>1./3 ||
       !std::isfinite(observation.interval_seconds) || observation.interval_seconds<observation.duration_seconds || observation.interval_seconds>3600 ||
       observation.minimum_input_rate>100000000)throw std::invalid_argument("Unsupported snapshot observation contract");
    const auto id=extension.descriptor.id;
    extension.descriptor.stage=DetectorStage::snapshot;
    extension.descriptor.enabled=[id](DetectorSelection& s)->bool&{return s.snapshot.extensions[id];};
    if(extension.descriptor.preference_key.empty())extension.descriptor.preference_key=id;
    registerModulationDescriptor(extension.descriptor);
    std::lock_guard lock(extensionMutex);
    extensionOptions.push_back({id,id,[id](SnapshotSelection& s)->bool&{return s.extensions[id];}});
    extensionDetectors.push_back({[id](const SnapshotSelection& s){const auto found=s.extensions.find(id);return found!=s.extensions.end() && found->second;},std::move(extension.analyze),observation});
}

std::vector<SnapshotObservation> snapshotObservations(const SnapshotSelection& selection,std::uint32_t rate) {
    std::vector<SnapshotObservation> result;
    const auto add=[&](const SnapshotObservation& observation){
        if(rate<observation.minimum_input_rate)return;
        const auto found=std::find_if(result.begin(),result.end(),[&](const auto& entry){return entry.trigger==observation.trigger &&
            entry.duration_seconds==observation.duration_seconds && entry.interval_seconds==observation.interval_seconds;});
        if(found==result.end())result.push_back(observation);
    };
    for(const auto& detector:snapshot::widebandDetectors())if(detector.enabled(selection))add(detector.observation);
    for(const auto& detector:snapshot::burstDetectors())if(detector.enabled(selection)){add({});break;}
    return result;
}
SnapshotSelection snapshotObservationSelection(const SnapshotSelection& selected,const SnapshotObservation& observation,std::uint32_t rate) {
    auto result=selected;
    for(const auto& option:snapshotDetectorOptions()) {
        SnapshotSelection probe{{false,false},false,false,false,{}};option.enabled(probe)=true;
        const auto policies=snapshotObservations(probe,rate);
        if(std::none_of(policies.begin(),policies.end(),[&](const auto& policy){return policy.trigger==observation.trigger &&
            policy.duration_seconds==observation.duration_seconds && policy.interval_seconds==observation.interval_seconds;}))option.enabled(result)=false;
    }
    return result;
}

namespace snapshot {
std::vector<WidebandDetector> widebandDetectors() {
    static const std::array detectors{
        WidebandDetector{[](const SnapshotSelection& s){return s.ofdm;},
            [](const WidebandInput& input,Json& events){detectOfdm(input.samples,input.rate,input.first,events);}},
        WidebandDetector{[](const SnapshotSelection& s){return s.css;},
            [](const WidebandInput& input,Json& events){
                detectCss(input.samples,input.rate,input.center,input.searchWidth,input.first,events);
            }},
    };
    std::vector<WidebandDetector> result(detectors.begin(),detectors.end());
    for(const auto& extension:builtinSnapshotExtensions()) {
        const auto id=extension.descriptor.id;
        result.push_back({[id](const SnapshotSelection& s){const auto found=s.extensions.find(id);return found!=s.extensions.end() && found->second;},extension.analyze,extension.observation});
    }
    std::lock_guard lock(extensionMutex);result.insert(result.end(),extensionDetectors.begin(),extensionDetectors.end());return result;
}

std::span<const BurstDetector> burstDetectors() {
    // The first matching classifier owns the burst.
    static constexpr std::array detectors{
        BurstDetector{[](const SnapshotSelection& s){return s.psk.any();},
            [](std::span<const Complex> samples,double rate,const SnapshotSelection& s){
                return identifyPsk(samples,rate,s.psk);
            }},
        BurstDetector{[](const SnapshotSelection& s){return s.four_fsk;},
            [](std::span<const Complex> samples,double rate,const SnapshotSelection&){
                return identifyFourFsk(samples,rate);
            }},
    };
    return detectors;
}
}
}
