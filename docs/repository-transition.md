# Foreshadow repository transition

English · [简体中文](repository-transition.zh-CN.md)

The maintainer requested a new source repository named `vicliu624/Foreshadow`
with an independent Git history, replacing `vicliu624/AMC`. The initial source
snapshot derives from the former Foreshadow branch at
`b4d672727536a94e52d3697b1440e251a0037803`, with current repository/download URLs
updated. The initial publication also includes explicit `QLocale` headers in the
waveform trace views for the self-hosted Linux runner's Qt version, an independent
CI build directory, and removal of an accidental Qt installer log. The localized
measurement test normalizes locale symbols before checking numerical round trips,
because the runner's Qt cannot parse its own Arabic scientific notation; the
application's display format is unchanged. Binary releases
are a separate operation and are not uploaded as part
of this source publication.

Original third-party copyright notices, license texts, first-party GPL grant and
source provenance remain. A new Git history does not change licensing obligations
or establish new codec patent, vendor API or firmware permission. Complete
functionality is retained. Compatibility commands, package IDs, settings roots,
formats and trust public key retain their existing identities; the active catalog
URL and official asset URL allowlist use the new repository. No signed catalog is
claimed available before a later qualified binary release.

The former shared Git metadata, all local refs/worktrees, remote mirror,
repository discussions/metadata and published release assets are archived outside
the new repository. Ignored build outputs, local recordings, credentials and
backup Git databases are not imported into the new history. The former working
directories remain local source snapshots after their old Git metadata is retired.

Dated reports keep their original source hashes, filenames and measurements.
Old AMC Actions/commit/release links describe historical evidence and may no
longer be reachable after retirement. They are not rewritten as tests of this
fresh history; archived evidence is held by the maintainer. Re-run the relevant
build, architecture, contract and package qualification before publishing binaries
from the new repository.
