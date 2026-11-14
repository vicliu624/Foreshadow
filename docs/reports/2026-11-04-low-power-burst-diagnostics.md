# Low-power short-burst diagnostics

English · [简体中文](2026-11-04-低功率短包漏检诊断.md)

This historical diagnostic chronology records the transition from threshold experiments through the independent recorder, v2/v3/v3.1 detection and bounded burst buffering. Intermediate “pending” statements belong to their stage. Later results do not erase earlier misses, false positives, dropped samples or failed observations. Payload/CRC recovery was outside these experiments.

## Initial controlled baseline

The COM11 SX1262 used `amc-pager-tx-0.2`, nominal −9 dBm, idle at boot and one packet per TX, with verified original-firmware backup. The receiver searched independently without transmitter settings. The first 25 transmissions produced 19 parameter/time-associated structures and six misses; all 19 recordings covered the estimated frame. This was association, without received-byte/CRC proof (`matrix-minus9-source-ring-v2.json`).

Four known positive recordings were replayed at 18/16/13/11 dB; all sixteen runs produced only the expected parameter matches. Such selected positives cannot measure environmental false alarms or justify a default threshold alone. Higher-power controls never replace lower-power failures. Noise, CW, continuous CSS and extended/truncated SFDs remained negative in the finite low-threshold regression.

## Throughput experiments, including rejected changes

All times below concern the same three-second recording on the uConsole unless stated otherwise. Concurrent measurements are not isolated throughput.

| Candidate | Observation | Decision/scope |
|---|---|---|
| First concurrent threshold comparison |18/16/13/11 dB: 5.16/8.97/13.02/13.76 s |Old live process also running; online analysis loss observed |
| Strong 13 dB seed, low structural threshold |Isolated18/11 dB: 2.650/6.519 s |Four real positives retained, insufficient throughput |
| Adjacent same-frequency peaks activate search |2.220/3.435 s |About47% faster at11 dB, still slower than input |
| Dense window11→5 symbols |2.233/3.359 s |Limited improvement; short-preamble controls passed |
| Two versus three workers |3.821/3.352 s total |Three retained; initialization separated from processing |
| Single-bin neighbor projection |4.023 s total,3.933 s processing |Slower than FFT reuse; withdrawn |
| Budget-aware result construction |3.300 s total/3.210 processing |Still insufficient |
| Dynamic model scheduling |3.193/3.101 s |Deterministic1/2/3-worker evidence retained |
| Four workers |3.038/2.944 s |Barely faster excluding startup, without sustained margin |
| Bounded256 KiB analysis batching |2.774 s processing |No waiting to fill a batch; gaps remain boundaries |
| Additional NEON power sum |2.763 s |Small benefit; not the central correction |

FFTW's limited measured-planning trial did not establish a stable advantage. At1.5 GHz/47.2°C/throttled0, major FFT plans used NEON; lack of SIMD was not an established cause. Low-threshold throughput was not repaired by narrowing the required simultaneous1 MHz/BW 500 coverage.

A30s/2 MS/s dynamic-scheduler run associated a nominal9.024ms SF7 packet at478.474609375 MHz versus478.475172 MHz transmitted. Estimated frame coverage had0.499shead/7.492stail, but `possibly_truncated=true` retained the uncertain quiet ending. Input60,000,000 versus analysis59,016,960 showed983,040 rejected pairs and48result-group losses. An empty queue at stop did not mean no earlier backlog.

Ordinary diagnostic probes were then excluded from the recording trigger queue, retaining sampled display hints and explicit counters. A30s run had no result losses but2,064,384 analysis rejects; the transmitter correctly refused to send after reception had already stopped. Probe publication was bounded to approximately once per0.25s/model without reducing detector input. A120s run still rejected5,472,256 of240million pairs at about95.6%whole CPU. An attempted six-case matrix stopped before its first TX because readiness never held; empty cases were not counted as executed.

## Source blocking and independent recording

At1.75 MS/s, a planned120s run stopped around70.33s with `source_error`:117,265,512 pairs reached analysis, but source rejection made the capture incomplete. A later300s source-diagnostic run accepted525million with no source rejection,64 blocks/4 MiB ingress and maximum sink0.02334s, yet analysis lost1,409,024 pairs. Nonreproduction did not close the source fault.

An eight-case partial matrix later exposed maximum sink time3.7046s,64/64 ingress high water and32,768 source rejects. Its4 MiB budget held only about1.2s. Main/result losses were zero, so this particular failure could not be assigned to detector rejection. Writeback activity was observed, without identifying a particular filesystem call/card defect. Missing `/proc/pressure/io` had also interrupted optional metrics; independent metric reads corrected the observer, without fabricating old I/O evidence.

`independent_capture_writer_v3` moved event-store writes/eviction/status persistence off acquisition into a bounded32 MiB recorder. Late/early results queue by confirmation sample; shutdown drains analysis then recorder. Storage snapshots use nonblocking reads. Full queues/file errors stop explicitly. Tests injected750ms recording and250ms analysis stalls, exact-byte retention/quiet tail and original-error propagation.

ARM five-test regression passed. The300s physical run accounted525million in acquisition/analysis/recording, zero known rejection/result losses, ingress high-water1/64/maxsink0.013s; recorder reached80 blocks and drained. This qualified that software run, without USB/24h guarantees. The27-transmission matrix associated23 and missed four; 15successful captures still had conservative truncation flags. No per-case software loss explained those misses. Actual GUI deployment and bounded localized research coexistence were checked separately; diagnostic plots did not count as blind detections.

## Conditional SFD and fine synchronization

A0.131s/458752-byte actual SF7 fixture localized a9.93 dB downchirp rejection at11 dB. v2 retained preamble/sync gates and allowed only established-preamble downchirps2 dB lower, with prominence, consistency, grid, quarter and ending checks intact. Windows 4/ARM5 tests passed. A240s run accounted420million with zero software losses; repeated nominal−9 dBm center/short-preamble cases were3/3, low edge2/3, high edge0/3. High-edge0 dBm was3/3, without replacing−9 dBm failures or excluding attenuation.695sstatus was not24hqualification.

Extending a negative exposed incorrect-sync cross-model promotion at0.67 symbol: actual BW 125/SF7 was interpreted as BW 250/SF8/reversed I/Q through a coarse path. All final structures were therefore required to pass refined carrier/timing and six-symbol coherence. ARM5 tests and240s/420million/zero-loss checks passed; bounded STATUS retries never blindly repeated TX.

A new high-edge recording had strong preamble/sync/downchirps but only1.11 dB ending contrast, failing the old5 dB ending rule. The2.25 symbol transition fit beat2.125/2.375 in diagnostics. Joint transition experiments initially falsely accepted1.875length and missed long SF signals. Reserving a64Mi-point attempt budget, at most48Mi for refinement, repaired budget exhaustion without relaxing final structure.

| Joint-transition stage | Sustained/matrix observation |
|---|---|
| Unoptimized |300s/525million accepted; 2,195,456 analysis rejects; 25/26associated, one BW 250/SF11miss, final high-edge case not sent |
| Cached cyclic references |480s/840million; 1,081,344rejects; 26/27matrix and10/10focused repeats, with losses in longSFcases; not deployed |
| Early rejection of noncompetitive candidates |300s/525million through all paths, zero known losses,59.19%whole CPU; 14/15focused, high edge2/3, six additional controls passed |

The pruned binary and GUI were deployed with matching hashes/rollback. Ordinary/longSF installed checks passed; high-edge and long-observation limits remained.

## Weak v4 research and independent v3.1 correction

The actual coherent-prefix fixture had mean11.42 dB/minimum 7.17 dB/coherence0.872/quarter5.36 dB. v4 joint weak-preamble work recovered it but failed new noisy arrivals0.07, then0.31 and0.67. Direction-change verification rejected periodic FSK by requiring the downchirp match to exceed reverse preamble projection by5 dB. A surviving0.67 case had only about0.178%transition advantage versus the empirical0.5%gate. The gate was not lowered merely to pass it.

On240 independent hashed inputs, v3 positives29/30,4/30,0/30 compared with v4 30/30,17/30,1/30. Periodic-FSK false structures were2/30 versus0/30; other selected negatives stayed zero. These were finite synthetic counts, without field sensitivity/false-alarm qualification. A separate minimal v3.1 patch added direction verification while retaining v3positive counts; two fixed FSK fixtures became negatives.

An800-input predeclared margin comparison found both0.5%/0.1% variants at98/100,80/100,3/100 positives, with the same wrong-SFD seed914001false positive. The0.1%variant passed local regressions but showed no independent overall gain and was not ARM/RF qualified/deployed. The complete v4 snapshot and failures remain under `research/v4`.

## Failed long observations and bounded-buffer conclusion

The installed v3observation failed near minute33, eventually accumulating10,551,296 rejected pairs/302gaps. No throttling was reported. A concurrent independent build was intentionally stopped; compilation interference remained a hypothesis. The original failure was not cleared. GUI repeated-table/repaint work was deduplicated; static ten-second main-thread CPU 0.70→0.16s under the same quota did not prove live continuity.

A25%-of-one-core/idle build scope was verified. Its600.019sobservation had no additional loss, without erasing cumulative failure. Deployment gates require same-binary SHA/algorithm, normally completed≥300shardware accounting and accurate notes. v3.1 CLI passed fixed negatives,300s/525million/zero-loss/62.91%CPU and16/16associated RF controls; later actual GUI failed in15.04s with720,896new rejects and high edge0/2. CLI/offscreen success did not qualify GUI stability.

The final bounded-buffer candidate retained64×64 KiB source ingress, expanded analysis to at most16 MiB/5s and original history to32 MiB/8s, kept32 MiB recording/1 GiB diskquota and continuous raw-disk recording disabled. It absorbs peaks without increasing sustained compute. Tests used deterministic stalls, retained actual recordings and covered late six-second events.

ARM5 tests passed; 300s CLI accounted525million/zero known loss but a longestSF12miss remained. A five-minute installed GUI observation completed with zero added loss yet only5/8associated transmissions, including high edge0/2. Two forced-stall attempts failed and remain. Final−9 dBm SF9/BW 125thenSF10/BW 250/reversed/public-synccontrols placed the second transmission safely inside a2500msanalysis pause, accounting78,750,000 pairs and both full-frame associations with high-water135>old64/zero known loss. This proves that bounded peak case, without payload/CRC/sensitivity claims.

A new86460sobservation was started with a recorded expected end, **not accepted as passed in this report**. Old33minute and GUI failures remain. Private `build/rf-tests/` records named throughout the Chinese counterpart preserve individual commands, source/binary hashes, matrix receipts and observer snapshots. Actual radio recordings were not published.
