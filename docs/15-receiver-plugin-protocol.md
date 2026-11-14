# Receiver plugin protocol and extension

English · [简体中文](15-接收机插件协议与扩展.md)

Receivers are runtime native modules. The host depends on the protocol, not librtlsdr, SoapySDR or vendor SDKs. RTL/AIO2 and SDRplay ship as providers; an independently built third test module demonstrates extension and is not installed into production.

## Ownership

Providers own enumeration/open, identity/dependencies, application order/readback, parameter types/units/options/defaults, tuning holes, antennas, direct sampling, gain, IF, ADC facts and recommendation candidates. Generic desktop owns rendered controls, conflict/application state and persistence. Analysis owns selected-detector compatibility/channelization/edge margin; workspace presents backlog/losses/errors.

The host filters provider candidates using the same complete analysis preflight as monitoring. It must not change requested frequency, disable detectors or reinterpret host CPU as hardware limits. Device rules are not host ID branches.

## Discovery and ABI

Entry `signalscope_receiver_plugin_v1` is declared in `include/signalscope/adapters/receivers/plugin_api.h`. Modules use prefix `amc-receiver-` and platform `.so/.dll/.dylib`; each platform/architecture has its own binary.

Search roots: executable-adjacent `receiver-plugins/`; its parent for Debug/Release builds; Linux `../lib/signalscope/receivers/`; Mac `Foreshadow.app/Contents/MacOS/receiver-plugins/`; and `~/.local/lib/signalscope/receivers/` when HOME exists. `SIGNALSCOPE_RECEIVER_PLUGIN_PATH` **replaces** defaults, colon-delimited on Unix and semicolon-delimited on Windows. Startup discovers modules; restart activates changes, with no live unload.

`signalscope receiver-plugins` reports loaded/rejected paths, IDs, ABI and reasons. Unsupported ABI, missing entry, duplicate identity, format or dependency rejects that module without hiding others. Offline input remains available without receiver modules.

ABI 1 uses fixed integers, function/context pointers and borrowed buffers. Host validates version/struct size. C++ strings/containers/exceptions/ownership never cross the boundary; optional SDK is provider-internal.

- `metadata_json`: stable ID, display text, source identity, encoding, default gain and discovery priority.
- `request`: UTF-8 JSON request with synchronous response callback.
- `stream`: configuration plus binary I/Q, ready/readback, cancellation and final-result callbacks.

| Operation | Input | Result |
|---|---|---|
| `devices` | Empty (request v1); optional `settings` (v2) | Availability/device list |
| `probe` | Index/optional serial; optional `settings` (v2) | Facts, capabilities, attribution |
| `parameters` | Facts + whole pending config | Current parameter descriptions |
| `plan` | Facts + whole pending config | Hardware validity, issues, effective values, data volume, ADC/IF |
| `recommend` | Facts + request | Ordered provider candidate configs |
| `configure` | Complete config | Requested/actual values and confirmation source |

Return zero for success; nonzero plus structured errors for failure. SDK distinguishes normal/configuration/stream errors. Host callback exceptions are captured, provider cleanup/stop is requested, then rethrown on the host side after return.

## Parameters and dependencies

Generic fields include `sample_rate_hz`, 64-bit `frequency_hz`, `antenna`, `auto_gain`, `gain_tenths_db`, `input_mode`, and `if_bandwidth_hz`. Vendor fields live under stable `settings` keys. A 5.8 GHz value must not truncate to 32 bits.

`extension:true` creates extra controls. Supported types: boolean/integer/number/enum/string; attributes include `label_key`, `description_key`, `choice_labels`, unit/default/choices/minimum/maximum/step/visible/enabled/read_only/requires_stop. Text is resource-keyed, not paired C++ translations. The host does not identify vendor keys.

Every edit reevaluates descriptions and plan against the entire pending draft. Providers may change options/ranges/availability. SDK supports declarative `enabled_when`; complex rules remain provider code. Disabling controls does not bypass validation. Saved invalid values remain visible with conflicts rather than silently replaced.

Issues carry `code`, `field`, `scope`, `blocking`, `values`; `values.message_key` supplies a resource key, default `issue.CODE`, with named dynamic parameters. `settings.KEY` identifies extension controls. Blocking issues prevent apply without host error-ID branches.

## Resources

Metadata `catalogs` lists relative resource roots in overlay order. Language-tag files (`en.json`, `zh-CN.json`, `fr.json`) map stable keys to strings. Shared `amc-receiver-common.i18n` can be overlaid by provider catalogs. Choice labels and error placeholders use the same mechanism; providers do not concatenate translated sentences.

Fallback is full tag → progressively shorter tag → English → raw driver text/key, per key. `fr-CA` can use `fr.json`; one missing French key still falls back to English. Resource-only additions need restart but no host/module recompilation. Desktop Qt catalogs remain separate. Missing/malformed/out-of-package declared resources reject the affected module.

RTL owns tuner/USB-AIO2 HF profiles. SDRplay queries driver controls and owns RSP ADC/antenna rules. RSPduo topology-changing modes exceed the current single-stream contract. Recommendations/configure/stream share hardware validation. Partial hardware application is not automatically transactionally rolled back; do not report failed settings as saved success.

## Samples and continuity

ABI 1 supports interleaved CU8, little-endian CI16 and normalized CF32. Shared codecs own names, byte count, normalization and full scale; CF32 component full scale is one and nonfinite values fail. Other native encodings require provider conversion or protocol extension. Storage format, nominal ADC bits and ENOB are distinct.

Borrowed data lasts only during the synchronous callback; each block is nonempty, pair-aligned and at most requested `block_bytes`. Source indices begin at zero and are strictly continuous; advertised encoding must match. Cancel/callback failure releases hardware and returns. A gap explicitly terminates this stream contract with structured counts/partial state; do not fabricate continuous indices. RTL lacks a hardware sequence, so zero software losses is not hardware-no-loss proof. Binary I/Q is not JSON per sample.

Modules run in-process. C exception boundaries are not vendor-crash isolation. Coherent channels, hardware triggering or new lifecycle capabilities require negotiated protocol changes.

## Independent extension evidence

`plugins/receivers/test_receiver` uses only the C ABI header/JSON, linking neither host SDK nor drivers:

```sh
cmake -S plugins/receivers/test_receiver -B build-example \
  -DAMC_RECEIVER_SDK_INCLUDE=/path/to/installed/include \
  -DAMC_JSON_INCLUDE_DIR=/path/to/nlohmann_json/include
cmake --build build-example
```

Install it/resources into an isolated root after building the host. Verify discovery, controls/dependencies, recommendations, readback, raw capture and monitoring with unchanged host SHA-256. Its eco/wide profiles, preamp and clock correction differ from built-ins: eco limits rate/disables preamp and recommendation intentionally differs from listed presets. English/Chinese UI checks cover generic controls, 5.8 GHz, blocked conflicts and repair/application.

Add French resources after binary installation; check new language/per-key fallback and unchanged host/module hashes. Test incompatible ABI, duplicates, damaged/removed modules. The independent provider encodes CF32 itself without linking the host codec library, exercising the same generic capture/record/analyze path. Fake devices never enter production installation.

## Request protocol 2: connection context before enumeration

Foreshadow 0.2.12 retains C ABI 1 and adds `request_protocol:2` metadata with an
array of provider-owned `connection_parameters`. These use the existing resource
and parameter descriptors, with `connection:true` and `extension:true`, so the
desktop can render them before discovery. `devices` and `probe` accept optional
`settings` objects. CLI, receiver sessions and the desktop forward the complete
connection context; a connection edit invalidates the preceding probe until
refresh. Unsupported/fractional request versions or missing protocol-2 descriptor
arrays reject the provider. Missing version retains protocol-1 payloads/defaults.
An independently installed provider verifies settings transfer and legacy fallback
without changing the host binary. The optional SDRplay client requires host
0.2.12 and uses an independent service; native modules still run in-process, but
that client never loads the hardware driver/API there. See
[receiver services](receiver-services.md).
