#include "signalscope/adapters/receivers/plugin_support.hpp"
#ifndef AMC_RECEIVER_CATALOG_ROOT
#define AMC_RECEIVER_CATALOG_ROOT "."
#endif
namespace signalscope { const ReceiverBackend& AMC_RECEIVER_FACTORY(); }
namespace {
int request(const char* op,const char* text,void* ctx,AmcReceiverResult result) {
    return signalscope::receiver_plugin::request(signalscope::AMC_RECEIVER_FACTORY(),op,text,ctx,result);
}
int stream(const char* text,void* ctx,AmcReceiverSamples samples,AmcReceiverReady ready,
           AmcReceiverCancelled cancelled,AmcReceiverResult result) {
    return signalscope::receiver_plugin::stream(signalscope::AMC_RECEIVER_FACTORY(),text,ctx,samples,ready,cancelled,result);
}
}
extern "C" SIGNALSCOPE_RECEIVER_EXPORT const AmcReceiverPluginV1* signalscope_receiver_plugin_v1() try {
    static const auto metadata=[] {
        const auto& b=signalscope::AMC_RECEIVER_FACTORY();
        auto metadata=signalscope::Json({{"id",b.id},{"name",b.display_name},{"source",b.source},{"recording_source",b.recording_source},
            {"catalogs",{"amc-receiver-common.i18n",AMC_RECEIVER_CATALOG}},
            {"catalog_roots",{".",AMC_RECEIVER_CATALOG_ROOT}},
            {"format",signalscope::sample_datatype(b.format)},{"default_gain",b.default_gain},{"priority",AMC_RECEIVER_PRIORITY},
            {"gain_kind",b.gain_kind==signalscope::ReceiverGainKind::if_reduction?"if_reduction":"tuner"},
            {"gain_presets",std::vector<int>(b.gain_presets_tenths.begin(),b.gain_presets_tenths.end())},
            {"monitor_rates",std::vector<std::uint32_t>(b.monitor_rates.begin(),b.monitor_rates.end())}});
        metadata["request_protocol"]=2;
        metadata["connection_parameters"]=b.connection_parameters;
#ifdef AMC_RECEIVER_SETTINGS_PREFIX
        metadata["settings_prefix"]=AMC_RECEIVER_SETTINGS_PREFIX;
#endif
        return metadata.dump();
    }();
    static const AmcReceiverPluginV1 api{1,sizeof(AmcReceiverPluginV1),metadata.c_str(),request,stream};return &api;
}catch(...){return nullptr;}
