# Codec patent assessment for free, open-source distribution

English · [简体中文](codec-patent-assessment.zh-CN.md)

Assessment date: **14 November 2026**. This assesses the proposed free public
GitHub downloads of the GPL-3.0-or-later Windows x86-64 / Debian 13 x86-64 desktop
software, with local television decoding. It does not assess a paid appliance,
hardware bundle, encoder service or redistributed broadcast content. A global
download is not assumed to be confined to China. The publishing legal entity,
affiliates, territories and volume would need confirmation in an actual license.
Free downloads do not add a noncommercial restriction to the GPL grant.

**Result:** the available public material does not establish a blanket patent
exemption for this distribution. It also does not establish that Foreshadow must
pay every listed pool or infringes a particular live claim. Claim scope, legal
status, territory, activity and any applicable grant still matter. The complete
base is prepared with existing functionality retained; its codec patent decision
remains open. The separately delivered SDRplay client's runtime does not contain
or invoke these television implementations and has its own release decision.
Its delivered production-source archive is a source superset, rather than a claim
that every source file is linked into the client.

## Actual implementation being assessed

Native inspection of the newly packaged `avcodec` libraries confirms the following
registered decoders on both platforms:

`cavs`, `h264`, `hevc`, `mpeg2video`, `mpeg4`, `aac`, `aac_latm`, `ac3`, `eac3`,
`mp2`, `mp3`, `pcm_s16le`.

The libraries report LGPL 2.1-or-later and `--disable-gpl --disable-nonfree`.
The local AVS+ work accompanies the pinned FFmpeg build. The MIT DRA core is
separately compiled into the isolated media helper, not registered in FFmpeg.
No new decoder, encoder or runtime was added or removed in this documentation
repair. See [build configuration](../cmake/BuildFfmpeg.sh),
[DRA provenance](../third_party/dra/UPSTREAM.md) and
[new package evidence](reports/2026-11-14-public-release-preparation-proof.json).
Copyright licenses and process isolation do not decide patent applicability.

## Findings from primary material

| Implementation | Public evidence | Decision for this free download |
|---|---|---|
| AVS / AVS+ | The [2021 AVS IPR policy](https://www.avs.org.cn/public/storage/upload/files/download/IPRPolicy.pdf), articles 12/14, distinguishes RAND-RF, pool and RAND commitments. Article 30 concerns reference-software copyright. | No general free/open-source patent exemption established. Identify the applicable standard/claims and available grant. |
| DRA | A [2008 statement supplied by its then CMMB licensing representative](https://www.cnr.cn/2008tf/CMMB/js/200808/t20080807_505059772.html) described written permission and a royalty holiday ending June 2009. | Historical, field-limited evidence; not a current grant for desktop GitHub binaries. Current free-distribution permission remains unconfirmed. |
| AVC / H.264 | The [current Via LA program](https://www.via-la.com/licensing-programs/avc-h-264/) describes software codecs and an initial zero-royalty unit tier; the agreement determines coverage. | A $0 tier is not proof that no agreement is needed. Do not assume software download counts or Foreshadow eligibility. |
| HEVC / H.265 | [Access Advance's current FAQ](https://accessadvance.com/faq/) expressly addresses free software downloads and requires case-specific determination. The [former Via program](https://www.via-la.com/licensing-programs/hevc-vvc/) is now administered by VCL Advance. | Free price alone does not establish exemption; confirm relevant portfolios and overlapping grants. |
| AAC | [Via LA's AAC FAQ](https://www.via-la.com/licensing-programs/aac/) distinguishes encoder/decoder products from encoded-stream distribution, and makes approved PC-software pricing conditional. | Foreshadow distributes a decoder, not just a bitstream. No applicable blanket exemption found. |
| MPEG-2 video / systems | The [current pool page](https://www.via-la.com/licensing-programs/mpeg-2/) describes remaining coverage in Malaysia. | Do not convert US expiry into worldwide clearance; territory remains material. This does not assert that our implementation practices a particular claim. |
| MP3 | [Fraunhofer's explanation](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses) states that its core-patent program ended after expiry, while distinguishing implementation-specific rights. | That ended program is not itself a new license blocker. Do not make an all-patents/worldwide claim. |
| AC-3, E-AC-3, MPEG-4 Part 2, MP2 | These are present. [Dolby's licensing entry](https://professional.dolby.com/licensing/) is not a free-software waiver or a claim analysis of our independent implementation. | No new license obligation or exemption is asserted; remaining implementation/territory analysis is explicitly incomplete. PCM has no identified separate codec-pool issue in this review. |

The old [2006 AVS pool advisory](https://www.avs.org.cn/index/list?catid=48)
describes initial, time-limited guidance. Its often quoted one-yuan figure is not
a current Foreshadow tariff or permission. Likewise, old DRA hardware/chip terms,
a single patent invalidation, or the age of a standard cannot clear an entire
software implementation. The [separate AVS2 advisory](https://www.avs.org.cn/index/list?catid=49)
allows for royalty-free profiles. This is a useful possible route for that
standard, not permission for our AVS/AVS+ `cavs` implementation; AVS2/AVS3 must not
be conflated with AVS+. Ask explicitly whether a software exemption also covers
our standard and distribution. No current official claim register was obtained: the
[CNIPA query entry](https://cpquery.cponline.cnipa.gov.cn/chinesepatent/index)
returned HTTP 412 in this session; the AVS licensing portal timed out. Those are
research limitations, not evidence that a patent is active, invalid or expired.

## Why free research software is not automatically exempt

China's [Patent Law](https://www.cnipa.gov.cn/art/2020/11/23/art_97_155167.html)
distinguishes activities for production/business purposes (article 11) and use
specifically for scientific research/experiments (article 75(4)). The Supreme
People's Court's [published reasoning](https://ipc.court.gov.cn/zh-cn/news/view-1330.html)
explains that business purpose is not decided solely by profit or entity type.
Neither source adjudicates Foreshadow. Our inference is that private research
use and public distribution of a usable decoder require separate analysis;
calling an application a laboratory or making downloads free is insufficient to
declare that every distribution/use is exempt.

This is an evidence-based engineering assessment, not a freedom-to-operate legal
opinion. No payable license, admission of infringement or worldwide safety
guarantee is inferred. No permission request was sent, fee paid, contract signed
or vendor terms accepted in this work.

## Preserve functionality and resolve the open decision

The prepared base retains television capture/TS handling, AVS+ video, DRA audio
and the existing media helper. No menu-only hiding, download disclaimer or new
noncommercial EULA is used as a substitute for permission. The SDRplay client can
be qualified independently, but requires host 0.2.12 or later and a separately
installed service. Vendor API and pager firmware stay outside both downloads.

The remaining concrete question is whether the identified free desktop binary
distribution and its GPL downstream rights are outside the relevant live claims,
already covered, or eligible for a compatible royalty-free grant. Prioritize an
answer for AVS+/DRA, then the actually included HEVC/AVC/AAC implementations.
If an agreement is needed, evaluate the real agreement, including territory,
reporting, downstream redistribution and GPL compatibility, before accepting it.
Paid fees or restrictive per-copy terms are not assumed necessary or acceptable.

### Draft scope for a rights-holder or patent-counsel inquiry

This is prepared text, **not a sent message or an admission**:

> Foreshadow is a GPL-3.0-or-later signal-research desktop application. We plan
> free Windows x86-64 and Debian 13 x86-64 source/binary downloads on GitHub, with
> local decoding of user-received or user-supplied television streams. The current
> implementation includes the codec family relevant to this inquiry; exact source
> revisions, build flags and decoder list are available. We do not distribute
> television programs or operate an encoding/streaming service in this scope.
>
> Please identify the current licensing administrator and any live patent claims
> and territories you consider relevant to this particular distribution. Is a
> patent agreement required for free open-source downloads? Is a no-fee or
> royalty-free route available, and what conditions/reporting apply? Does any
> grant permit GPL-compliant source/binary redistribution, modified versions and
> downstream recipients, without a noncommercial or per-recipient restriction?
> Please provide the current applicable terms and the basis for any exemption or
> covered use. We are seeking clarification and have not accepted an agreement.

Do not add unverified statements about downloads, corporate identity, revenue,
advertising or hardware bundles when using this draft. The source/material gates
continue to check their actual scope; they are not labelled patent clearance.
