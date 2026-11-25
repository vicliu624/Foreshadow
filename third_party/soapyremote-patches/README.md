# SoapyRemote client resource lifetime changes

English · [简体中文](README.zh-CN.md)

These narrowly scoped changes apply to upstream commit
`bd10a2e4a916bf66f0faa36dca7f043d4268138c` (SoapyRemote 0.5.4).
The original source archive and its BSL-1.0 notices remain unchanged. The local
changes described by `Settings.json` and `LogAcceptor.json` are also provided
under BSL-1.0, matching upstream. Original source copyright and license headers
are preserved in the generated compilation units.

`cmake/RemoteReceiver.cmake` applies the manifests to private build-tree copies
using `cmake/PatchPinnedSource.cmake`. The shared fetched source is never edited.
Each expected context must appear exactly once; a missing or ambiguous context
fails configuration. The unmodified upstream service remains the independent
test server and is not distributed with the client.

- `Settings.json` retains the log subscription in a local `unique_ptr` until
  remote device construction fully succeeds. A rejected MAKE request releases
  that subscription, its thread and its socket while unwinding.
- `LogAcceptor.json` joins the completed logging thread and closes its old socket
  before reconnecting. It avoids reconnecting an unsubscribed handler, clears the
  deleted thread pointer, and uses an atomic completion flag across threads.

`remote_resource_lifetime` uses a real independent upstream server, an independent
test receiver and a TCP relay that deliberately disconnects only the logging
channel. It checks native descriptor/handle counts during rejected and successful
construction, repeated log recovery, and final unsubscribe. The resource
test confirms log delivery with a real forwarded marker before sampling. A fully
closed warmup excludes process-wide WinSock initialization from the baseline;
the resource comparisons still require exact equality without an allowance.
The test links through the receiver SDK interface target so imported dependency
scope is preserved on Windows as well as Linux. The receiver backend
contract also repeats successful, canceled, consumer-failed and activation-failed
streams in one process. These checks do not establish an overnight hardware
qualification or identify the cause of an already restarted process.
