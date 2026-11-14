# Desktop workspaces and module interfaces

English · [简体中文](18-桌面工作区与模块界面规格.md)

## Responsibility

The main window hosts generic receiver/session/recording/event/workspace behavior, not modulation-specific advanced settings. LoRa bandwidth/SF research parameters belong to its provider and affect that research job only. Live detection uses detector-declared default search plans.

Display providers create stable-ID/localized widgets. Workspace descriptors drive show/hide/float/dock/tab/split/resize. `Workspace::add` automatically registers View entries, with no provider-ID branches. `ResearchProvider::controls` / `ResearchOptions::parameters` carry provider settings; the host does not parse tool-specific keys.

## Live research lifecycle

`LiveResearchProvider` v2 is a compiled C++ contract with independent reception leases and summaries; v1 retains monitor-bound semantics. Assembly is `plugins/desktop/research/live_registry.cpp`. External snapshot IPC is separately versioned and does not acquire live/native capabilities automatically. Static built-ins update with the application, not independent ABI installation.

v3 retains earlier field order/semantics and adds extra panels, native-path recording contexts and panel references for actions (specification 22). Extra panels are not extra I/Q consumers. Tool-to-main-panel mapping routes errors/status/diagnostics. Recording ranges are file-relative sample coordinates; opaque event fields pass intact. Action references must belong to the provider's declared widgets. Closing panels does not stop work; View and persistence remain generic.

Providers may declare localized explicit workspace presets. `Workspace::addLayout` registers them in View/Layout, validates all referenced panels and preserves receiver/status/events while placing primary/extra research panels. Narrow layouts use tabs. Apply only when explicitly selected; ordinary show/resize must not reapply a preset or infer layout from tool IDs.

Providers own widgets, parameter validation, preferences/resources, source context, nonblocking I/Q, event actions, stop/busy/error behavior. Except I/Q callbacks, callbacks run on UI thread. I/Q views are borrowed: copy retained payload before returning and own the bounded queues/workers. Session generations prevent stale callbacks reviving stopped tools. Optional-consumer failures isolate that consumer, not acquisition/other tools. Diagnostics are provider-owned opaque JSON; host only wraps/exports.

## FM and independent reception

FM owns tuning/favorites/search/output/volume/mute/squelch/de-emphasis/deviation normalization. Provider event actions open its registered panel. Hidden panels can continue playback. Stopping audio releases its lease; stopping monitoring releases monitoring; disconnect stops all live users. Temporary playback configuration does not overwrite saved wideband preferences. Manual full configuration/validation stays available. Current audio is mono, not stereo/RDS/universal decode.

SNR is a post-filter constant-envelope estimate affected by interference/fading/clipping; squelch does not alter physical SNR. States distinguish buffering/playing/recovering/failed. Recoverable underruns do not terminate playback; real errors persist with diagnostics and actionable configuration/device repair.

Audio device/prebuffer/write timer run on a separate output thread, not a GUI timer. UI reads published snapshots and sends controls. Test actual output progress while UI events stop. Spectrum rendering aggregates by pixel while retaining peaks/noise range rather than handing huge invisible polylines to raster painting.

## Persistent layout behavior

View centrally manages panels, monitoring/compact/default/save layouts and locking. Drag, close or double-click titles to dock/float; combine tabs and resize splitters. Hide is not unload or module-disable; menu and panel states synchronize.

Large-screen startup uses available space for spectrum/waterfall/events/status/receiver/evidence. Analysis/recording/evidence/displays are independent panels; selecting one must not forcibly hide others. Small screens may tab content while receiver/start/stop remain reachable. Ordinary resizing preserves user arrangement and visibility.

Persist geometry, floating/docking, tabs, sizes, visibility and locks in versioned workspace state. Off-screen floating panels redock when displays change. Damaged/incompatible state falls back safely; restore-default can recover every closed panel.

Restore a valid saved layout once, without a preceding default or repeated event-loop restoration. Delayed default sizing applies only to explicit presets. Release checks use a complex existing layout across actual desktop close/restart, not only offscreen or empty profiles.

Restore dock state while hidden in normal window state, then restore maximized/fullscreen. This avoids Qt 6.8 native resize leaving stale dock items when a new preset deletes them. Preserve user geometry/layout; do not hide faults by clearing preferences or fixed sleeps.

## Acceptance

An independently added panel must enter View without menu edits. Test close/reopen/float, cross-restart persistence/default restore, multiple visible large-screen areas, 640×480 and 1.25 scaling, and provider-owned parameters reaching/rejecting actual algorithms. Existing monitoring/events/replay/receiver configuration remain regression requirements. New v3/native research evidence is in the waveform and Mac reports; older “developing v3” wording is not a current feature exclusion.
