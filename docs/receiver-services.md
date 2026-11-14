# Receiver services and the independent base package

English · [简体中文](receiver-services.zh-CN.md)

Foreshadow 0.2.12 separates the base application from the optional SDRplay
receiver client. The base includes RTL-SDR/AIO2 support and the existing offline,
monitoring, recording and research workflows. Its application archive does not
contain the SDRplay client or its private SoapySDR runtime. Install the separately
versioned SDRplay toolbox to use an independent receiver service. The main
application and first-party client remain `GPL-3.0-or-later`.

## Why SoapyRemote was selected

SDRconnect was evaluated first against the current capture/control contract.
The official [WebSocket specification 1.0.3](https://sdrplay.com/wp-content/uploads/2026/08/SDRconnect_WebSocket_API.pdf)
defines device frequency/sample rate, LNA state, antenna selection, overload,
and signed 16-bit interleaved I/Q. It does not document all the settings already
exposed by our provider. Hardware sample rate alone does not establish the
bandwidth, scaling and continuity of the delivered I/Q stream.

| Existing requirement | Published SDRconnect WebSocket contract | Decision |
|---|---|---|
| IF gain reduction and hardware gain mode/readback | No complete IFGR/hardware-AGC contract; documented audio AGC is a different control | Does not establish parity |
| Hardware IF filter selection/readback | The documented filter is VFO bandwidth | Does not establish parity |
| RF/DAB notch, Bias-T, I/Q correction, HDR, AGC setpoint | No complete corresponding property set | Does not establish parity |
| Native full-scale semantics, full acquisition bandwidth, discontinuity evidence | Signed I/Q is defined, but not all required acquisition facts | Requires additional evidence |

There was no installed SDRconnect on the receiver workstation. The official
Linux installation-script URL returned HTTP 403 during acquisition. No installer
was run and no vendor terms were accepted automatically. This is **a published
protocol assessment plus an external native-validation blockage**, not a claim
that a native SDRconnect trial failed. The documented control gaps already
prevent selecting it as a functionally equivalent replacement.

The selected fallback is [SoapyRemote](https://github.com/pothosware/SoapyRemote),
upstream revision `bd10a2e4a916bf66f0faa36dca7f043d4268138c` (0.5.4), under
BSL-1.0. It forwards the existing SoapySDR device capabilities/settings and native
CS16 stream through an independently usable `SoapySDRServer`. The client calls
only the assembled remote registry functions; it does not call the global
SoapySDR device factory or scan/load local hardware modules. The Windows vendor
DLL discovery/preload implementation was removed.

## Installation and operation

1. Install the base application. Files, synthetic review cases and RTL-SDR/AIO2
   remain usable without a receiver service or SDRplay vendor software.
2. Install the matching **SDRplay Support** receiver toolbox. Version 0.2.12 or
   later requires a host supporting receiver request protocol 2. Its package
   includes client source, original BSL notices and build instructions; it does
   not include the vendor API, local SDRplay driver or server executable.
3. On the receiver computer, install the vendor API from
   [SDRplay](https://sdrplay.com/hardware-api/) under its terms, the compatible
   MIT SoapySDRPlay3 driver, and SoapySDRServer. Existing vendor installations are
   used as installed; Foreshadow does not accept vendor terms on the user's behalf.
4. Start the server. For a local receiver use
   `SoapySDRServer --bind=127.0.0.1:55132`. For a receiver on a trusted private
   network, explicitly bind that computer's interface address. SoapyRemote does
   not add authentication or encryption; do not expose this service publicly.
   TCP streaming also opens negotiated stream/status ports, so forwarding only
   control port 55132 is insufficient for an SSH port-forward configuration.
5. Open **Receiver settings**, select **SDRplay**, set **Receiver service** to
   `tcp://host:55132`, and choose **Refresh devices**. Select the device/serial,
   antenna, rate, gain and advanced settings, then **Apply**. The address is a
   provider-owned connection parameter saved with that receiver's settings.
   Apply still confirms deferred hardware activation and reads values back.
6. Monitoring, manual recording, waveform capture, FM and television continue
   through the same shared receiver sessions. Stop/release permits another client
   to acquire the device; the service does not imply concurrent hardware ownership.

For unattended use, configure the external server with the operating system's
service manager. The review receiver workstation has a user service named
`foreshadow-receiver.service`; `systemctl --user status|start|stop|restart
foreshadow-receiver.service` manages it. The review launcher/profile already
contains its address. The base application neither installs nor starts vendor
software silently.

The optional `FORESHADOW_SDRPLAY_SERVICE` environment variable supplies an initial
default. Explicit `service_endpoint` settings take precedence. CLI examples:

```sh
signalscope devices --receiver sdrplay --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
signalscope probe --receiver sdrplay --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
signalscope configure --receiver sdrplay --rate 2000000 --frequency 478500000 \
  --gain 32 --antenna 'Antenna B' --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
```

## Build and licensing boundaries

The application build fetches the SHA-pinned upstream SoapyRemote source and
compiles its BSL client sources into the receiver module. Provide
`FETCHCONTENT_SOURCE_DIR_SOAPYREMOTE` for an offline build. An unmodified upstream
server is built only as a test fixture when `BUILD_TESTING=ON`; it is not installed
into the application/client package. To install the independent upstream service:

```sh
cmake -S SoapyRemote -B service-build -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_AVAHI=OFF -DCMAKE_INSTALL_PREFIX=/your/service/prefix
cmake --build service-build --config Release --parallel 4
cmake --install service-build --config Release
```

Keep that service's original BSL license and dependency terms. Its vendor API and
hardware driver remain separate user-installed components. Using an open-source
adapter does not grant vendor redistribution rights. The earlier API 3.15
installer/EULA evidence remains historical evidence; no new permission in that
API or the embedded firmware is asserted. The base and remote client are checked
on their actual dependency closures, rather than inheriting an unrelated local
API distribution condition. A process boundary is supported by this independent
service and network protocol, not treated as a universal license exemption.

## Protocol, resources and continuity

The receiver C ABI remains 1. Request protocol 2 adds optional `settings` objects
to `devices`/`probe` and provider-owned `connection_parameters` metadata, available
before hardware enumeration. Legacy callbacks retain their defaults. The host
renders these generic descriptors and passes settings through CLI, sessions and
the wire protocol; it contains no endpoint or vendor-ID routing. New clients
declare minimum application version 0.2.12. Existing command names, receiver and
package IDs, settings roots and recordings remain compatible. Update old managed
direct-link SDRplay toolboxes before using the new service deployment; arbitrary
third-party native modules are not claimed safe merely because the base is clean.

I/Q remains native CS16 in little-endian recording storage; the codec owns byte
counts and normalization. TCP transport uses at most 16 KiB I/Q payload per frame
and requests a sample-rate-based socket window of 200 ms, clamped to
256 KiB–8 MiB. Actual OS socket limits can be smaller and are reported by the
upstream service. These are buffer budgets, not losslessness guarantees.

`read_stats.scope=client_transport_reads` describes client reads/timeouts, not
hidden driver reads on the server. Known overflow/abrupt flags stop the stream
and preserve the accepted prefix. Hardware sample-loss counts and internal server
driver-read counts remain unknown when the protocol/driver does not expose them.
Calibration and ADC overload facts retain their existing explicit unknown states.
Connection failure, malformed endpoint, cancellation and service restart must
produce bounded failures/recovery, without fabricating continuous samples.

## Release qualification

The application composer uses provider-declared `bundle_with_application` values
and receiver resource/runtime closures. An optional provider is omitted from the
base archive and its bundled inventory; shared files needed by retained providers
remain. Each independent archive carries its own applicable legal-material closure.
An unresolved optional package condition still blocks that package while an
unaffected base can pass. The generic installer and signer do not branch by vendor.

Native contracts use an actual independent upstream server and a separate mock
hardware provider, including continuous input, short reads, overflow, activation
failure, recovery, settings, resource fallback and a local-driver autoload trap.
Real archives must pass Unicode-path installation, independent-toolbox discovery,
native UI/workflow checks and mechanical source/material revalidation. Physical
receiver runs and loaded-module observations are recorded separately from those
contracts. macOS is outside this Windows/Linux review qualification.

The [14 November r4 report](reports/2026-11-14-receiver-service-review.md) records
the final Windows/Linux archives, passing package gates, source rebuilds, physical
service tests, current review entries and screenshots from the receiver computer.
