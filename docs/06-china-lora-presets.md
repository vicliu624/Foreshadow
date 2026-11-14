# Chinese LoRa applications and observation presets

English · [简体中文](06-中国LoRa应用与监听预设.md)

**Historical research dated 2026-11-03.** This records the basis for early receive-only observation, not current legal advice, a local occupancy claim or current full hardware limits. Later wideband recommendations supersede the initial 1 MHz preset.

## Basis and limits

Public metering/IoT products use LoRa in China, supporting investigation but not proving nearby deployment, reporting intervals, private parameters, reception or market share. The [2021 MIIT industrial/IoT frequency guide](https://wap.miit.gov.cn/cms_files/filemanager/1226211233/attach/20216/6f2d2602200448009b156a1e7863d8c2.pdf) lists 470–510 MHz civil metering. This is neither LoRa-exclusive spectrum nor arbitrary transmit authorization.

Product/application examples: [Semtech ecosystem](https://www.semtech.com/lora/ecosystem/sensor-features), [LoRa Alliance metering paper](https://lora-alliance.org/wp-content/uploads/2021/01/White-Paper_WHY-UTILITIES-ARE-CHOOSING-SMART-LoRaWAN%C2%AE-CONNECTIVITY.pdf), [Seckmeter CN470 module](https://www.seckmeter.com/product/nonmagnetic_smart_water_meter/splittype_nonmagnetic_communication_module/95.html). LoRa is PHY; LoRaWAN is a protocol. Private meters need not use LoRaWAN; NB-IoT/other standards also exist.

## Dated regulatory/version references

The [2019 announcement 52](https://www.miit.gov.cn/zwgk/zcwj/wjfb/gg/art/2020/art_4f2c890530cc4837b2113cf977e249c4.html) and [official attachment](https://wap.miit.gov.cn/cms_files/filemanager/oldfile/miit/n1146285/n1146352/n3054355/n3057735/n3057748/n8004149/c8006552/part/8006563.pdf) specify device-category scenarios/frequency/bandwidth/duration conditions. They do not authorize software transmission. Application replay is passive/software; separate test hardware must retain its applicable configuration.

The research consulted [RP002-1.0.5](https://resources.lora-alliance.org/technical-specifications/rp002-1-0-5-lorawan-regional-parameters), noting CN470 plans/version differences. The complete revised channel table was not extracted in that investigation, so no universal optimum/uplink-downlink plan was hardcoded from an older version. [WH-L101 manufacturer material](https://www.usr.cn/uploads/20250107/WH-L101-LoRaWAN%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E%E8%B5%84%E6%96%99-V1.0.1-20250107134910.pdf) is further product-range evidence, not local use.

## Initial receive presets

| Preset | Center-search range | Purpose |
|---|---|---|
| Controlled user device | Initial 1 MHz around actual test frequency | Fixed observation, random in-window transmission frequency for independent unknown-frequency checks |
| CN470 starting observation | 470–471 MHz | Explainable initial location, not measured as busier |
| CN470 exploration | 470–510 MHz | Long-dwell windows with actual observed duration and gaps |
| Active-window watch | 1 MHz around observed activity | Fixed watch for sparse recurrence |
| Custom known band | Device label/user evidence | No default assumption that EU868/US915 suits local devices |

Initial BW 125/SF7–12 search was a staged plan; wider modes required implementation/load checks. Record unsearched combinations. Nondetection does not mean no LoRa.

## Exploration proposal

Initial 1 MHz center windows stepping 0.8 MHz construct 50 overlapping windows over 470–510 MHz (edge-clipped last window). Each simultaneously samples/searches its own center range, adding target BW/guards within actual passband. It is not 200 kHz hardware channel hopping.

Proposed dwell 30–120 seconds, initially60, is an observation budget, not a meter reporting interval. One round ~50 minutes plus tuning; one window receives ~1/50 duty apart from overlap. One-shot bursts can be missed; periodic transmissions can remain phase-misaligned with polling. Bounded dwell jitter/coverage display can help but not guarantee capture. Prefer fixed watch to collect short bursts.

Candidates can request a bounded additional dwell and suggest fixed watching, never indefinite extension. Summarize by frequency/evidence/**actual observation duration**, not raw counts alone; retain zero-event window exposure.

## Field sequence

First controlled device: unknown-frequency search, varying strength, structure, original capture and offline reproduction. Then stop active testing and watch passively; explore only when needed and return to an active window.

Record coarse environment/antenna/gain/date/time/coverage/detector parameters/candidates/files. Ambiguous source stays unknown. Band/interval alone cannot label a particular meter/protocol or become known-label accuracy data. At this research date no local capture/survey/channel-distribution evidence existed; later dated reports must supply actual findings.
