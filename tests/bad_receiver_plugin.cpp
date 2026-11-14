#include "signalscope/adapters/receivers/plugin_api.h"
extern "C" SIGNALSCOPE_RECEIVER_EXPORT const AmcReceiverPluginV1* signalscope_receiver_plugin_v1() {
    static const AmcReceiverPluginV1 incompatible{99,sizeof(AmcReceiverPluginV1),"{}",nullptr,nullptr};return &incompatible;
}
