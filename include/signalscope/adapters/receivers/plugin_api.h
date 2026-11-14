#ifndef SIGNALSCOPE_RECEIVER_PLUGIN_API_H
#define SIGNALSCOPE_RECEIVER_PLUGIN_API_H
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define SIGNALSCOPE_RECEIVER_EXPORT __declspec(dllexport)
#else
#define SIGNALSCOPE_RECEIVER_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* ABI 1: UTF-8 JSON for control, borrowed binary buffers for samples. No C++
 * objects, exceptions or allocated memory cross this boundary. All callbacks
 * are synchronous; their arguments remain valid only during the call. Samples,
 * ready and result are serialized on the calling stream thread. Cancellation
 * may also be queried by the plugin's watchdog; all threads join before return.
 * Return 0 for success, 1 for an error envelope delivered through result.
 * A nonzero samples/ready callback result requests an immediate clean stop.
 * The ready JSON is emitted once before samples. Its optional protocol_version
 * defaults to 1. Version 2 adds a required, separately versioned
 * acquisition_conditions object describing startup facts, not live telemetry.
 * The C function table and ABI stay at 1; existing hosts ignore added JSON keys. */
typedef void (*AmcReceiverResult)(void *context, const char *json, size_t length);
typedef int (*AmcReceiverSamples)(void *context, const uint8_t *data, size_t length, uint64_t first_pair);
typedef int (*AmcReceiverReady)(void *context, const char *json, size_t length);
typedef int (*AmcReceiverCancelled)(void *context);
typedef struct AmcReceiverPluginV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *metadata_json;
    int (*request)(const char *operation, const char *request_json, void *context, AmcReceiverResult result);
    int (*stream)(const char *config_json, void *context, AmcReceiverSamples samples,
                  AmcReceiverReady ready, AmcReceiverCancelled cancelled, AmcReceiverResult result);
} AmcReceiverPluginV1;
typedef const AmcReceiverPluginV1 *(*AmcReceiverEntryV1)(void);
/* Each native platform/architecture builds its own binary from this contract. */
SIGNALSCOPE_RECEIVER_EXPORT const AmcReceiverPluginV1 *signalscope_receiver_plugin_v1(void);
#ifdef __cplusplus
}
#endif
#endif
