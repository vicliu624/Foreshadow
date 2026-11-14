# LoRa real-time loss and shared scheduling acceptance

English · [简体中文](2026-11-10-LoRa实时丢弃与共享调度验收.md)

Historical0.2.7record. On31.42,0.2.6 GUI at100 MHz/10 MS/s/10 MHz with FM audio still disabled, stopped status reported146,423,808 input/recordingpairs,145,539,072mainprocessed and884,736mainrejected/about0.604%. Auxiliary processedall with0rejection. Mainqueuehit16 MiB,18gaps; 3.56equivalentcores/about15%whole CPU did not mean every serial task had capacity. No hardware counter existed.

## Ownership and correction

Change axes were detector scheduling and common DSPchannelization, owned by LoRa/DSP, without new receiver/session/UI/CLI-IDbranches. Previously one worker serially searched all BW/SF/polarities within a subband while idle workers elsewhere could not help; channelization was serial before that. Sixseconds of identical actual input took6.71s LoRa analysis, including2.08schannelization.

Channel plans precomputed contiguous FFT index spans, preserving RF-edge cropping/alias-sum order and removing repeated hot indexing/checks; channelization fell to1.57s. Each search model retained independent history/FFT/results, with a bounded persistent shared pool distributing work across subbands/models and deterministic merge order. Existing worker budgets were no longer clipped prematurely by subband count. A whole batch validates all clocks before preparing shared read-only input, parallel execution and collection; input chunks remain at most131,072 samples and task count is model-bounded.

At the same10-workerbudget,6sanalysis fell to5.05swith2134diagnostic events unchanged. This host/input comparison did not establish universal speed. Search coverage, chosen models, thresholds and defaultqueues stayed unchanged. FFTW measured planning was rejected because limited gains added startup cost.

## Sustained physical run

Same RSPdx-R2/100 MHz/10 MS/s/10 MHz/all realtime detectors,16recommended workers/default16 MiB queues,180 seconds:

| Metric | Result |
|---|---:|
|Input/recording/main/auxiliary pairs, each|1,800,000,000|
|Main rejected/gaps|0/0|
|Auxiliary rejected|0|
|Main payload high-water|2,686,976 bytes/about2.56 MiB|
|Auxiliary payload high-water|1,835,008 bytes/1.75 MiB|
|Driver short reads/timeouts|27,917/0|

Snapshot CPU was about8 cores. Short reads neither reduced bytebudget nor became false loss counts. Stop drained stages with conservation. Private RF/machine-state inputs remained ignored. This qualifies180 seconds of known software continuity, without infinite-duration/arbitrary-load/every-signal or FM output guarantees.10 MS/s readback/8 MHzIFdoes not establish flat10 MHz RF response.

Tests used two simultaneous off-center signals: 1vs12 workers yielded identical evidence/source times/search-workcounts. Invalid batches could not partly advance other subbands; shared/independent inputs retained clocks; silence remained empty. LocalWSL45/45 passed; target45 checks had0failures/3 SDRplay environment skips, separately supplemented by real hardware above.

## Formal release and GUI rerun

Frozen source `0a445dac48f6741159f2fd1b71aa5392a4abb2dc`; [six-platform validation](https://github.com/vicliu624/AMC/actions/runs/42784476445) passed actual toolbox/Unicode/extracted checks. Windows KissFFT 43/43; FFTW platforms corresponding suites.30uploadedassetsmatched size/GitHub SHA-256;24packageshad identical source. Locally signed catalog sequence1794301072merged56records. [v0.2.7](https://github.com/vicliu624/AMC/releases/tag/v0.2.7); signing keys never entered Actions/remote.

31.42imported four formalpackages through the same signed/hashvalidated catalog; helperactivated0.2.7. Actual GUI/stable CLI/two receivers/powerreportwere checked. GUI SHA-256 `c86e4ae29f0d03a136363331bb59bc67773fbb542d97666d2a316f1d6138d068`matched the release. Old launcher testargswere removed; ordinary startup respected savedreceiver/language/layout.

Actual GUI again received1,808,105,472pairsat100 MHz/10 MS/s/10 MHz/default16 MiB, with0mainrejects/gaps/auxiliaryrejects and3 MiB mainpeak. Approximately14.7msnormal in-flight backlog was not mislabelled loss. This did not qualify FM playback; its errors/controls were a separate task.

Systemproxyremained. Installed updatecheck returned structurednetwork/`Connection closed`; no permanent temporary tunnel was required. Initial self-hosted CI was interrupted byWSLexit and canceled. A login keepalive entry allowed [the actual rerun](https://github.com/vicliu624/AMC/actions/runs/42785856998) to pass; hosted Runner did not take over daily CI.
