# Distribution compliance material protocol

English · [简体中文](distribution-compliance.zh-CN.md)

This is version 1 of the legal-material protocol used by Foreshadow release
assembly. It is separate from the signed toolbox manifest/installation protocol
in [specification 17](17-toolboxes-and-updates.md). It does not
make the installer interpret license IDs, resolve copyright disputes or grant
permission absent from upstream terms. The delivery inventory is a project JSON
format, not a claimed SPDX/CycloneDX certification; original upstream SPDX
documents are retained as evidence where supplied.
The packaging tools require Python 3.12+ (qualified here with Python 3.13);
the native application does not require a Python runtime.

## Owners and assembly

Receiver owners maintain `plugins/receivers/<provider>/licensing.json`; external
research owners maintain their own `licensing.json`. Application roots belong
to `resources/licensing/application.json`. Each schema-1 declaration has
`packages`, a mapping from package ID to component roots, and `application`, the
roots needed when this provider ships inside the application. CMake installs
these resources in `share/signalscope/compliance/providers/`.

CMake writes `build-facts.json` in the parent directory, with the distribution
platform, Qt, OpenSSL and application versions. It is a build fact rather than a
provider declaration. New providers add their resource, registration and reviewed
component evidence; the collector, GUI, session and installer do not gain a
provider-ID branch.

## Reviewed profiles and materials

`resources/licensing/profiles/*.json` records the exact supported platform and
build facts, the declared package roots, runtime ownership patterns, and component
definitions. A component has a version, license expression/selected alternatives,
upstream origin, dependencies, explicit `issues`, and whether source is required.
Optional `display_notice` is provider-owned text rendered prominently in that
package's index. An independent receiver's index does not invent Qt/codec usage.

Artifacts identify SHA-256-pinned real source archives, original notices or build
metadata. Repository materials pin their file path, role and SHA-256. The reviewed
profiles use LF text to keep those bytes stable across build platforms. First-party
source comes from a frozen Git commit and a `SOURCE-DELIVERY.json` manifest; the
production subset excludes research history, old patches, external test fixtures
and separately reviewed firmware. All production code, provider resources,
vendored source and build/install scripts remain.

`collect_materials.py` checks cached artifact and repository hashes, copies real
files, extracts original notices and records each notice's original archive and
member path. It gives extracted notices short hash-prefixed filenames, preserving
their original bytes and provenance without creating an excessively deep Windows
installation tree. Missing source, build or notice roles fail collection.

`compliance.py` checks the actual build facts and provider roots, attaches verified
material to a fresh stage, fingerprints native binaries and generates
`share/foreshadow/compliance/inventory.json` and `index.html`. Native PE/ELF/Mach-O
files must have exactly one reviewed owner. An unexpected executable or changed
post-attachment binary fails. Exact filename ownership in a profile is evidence
about one build configuration; it does not authorize new Qt modules implicitly.

## Independent packages and release gates

`package_release.py` resolves the component dependency closure for every
application, receiver and research archive, embeds that closure and writes
`verification.json`. Material paths stay relative and inside the package; source
archives are delivered, not replaced with source-host homepage links. The generic
installer preserves these files with the installed package.

Normal composition rejects unresolved `issues`. Private evaluation can explicitly
use `--review-only`, which preserves issue details and marks `review-only` in the
report; it cannot bypass source, notices, hashes, build versions or ownership.
Each selected package is evaluated independently. From 0.2.12 the base excludes
the optional SDRplay client; that client contains only the network adapter/core,
not the separately installed service/vendor runtime. Provider-owned
`bundle_with_application:false` omits an optional receiver's declared code,
resources and private runtimes from the base while retaining shared files. An
unresolved optional package still fails its own gate without blocking an unrelated
base. The independent-provider contract verifies both outcomes.

Before official catalog signing, `sign_catalog.py` reopens the actual archives.
It verifies material hashes, source identity, package closure, runtime rules and
component identities/conditions against a repository-reviewed profile. It checks
that pinned source/notice artifacts and repository materials are present. Removing
an issue from an inventory does not change the reviewed profile. Retained catalog
entries must be resupplied with their archives for revalidation; no implicit
grandfathering of older unqualified packages occurs. Keep approved historical
profiles while offering their versions.

On Windows the generic installation transaction retries native access/sharing/
lock errors at the final directory rename for at most five seconds. Other errors
fail immediately; persistent locks fail without committing the registry. Native
tests use an independent package type holding a real directory handle, covering
both a briefly held reader and a persistent reader in a Chinese/non-BMP path.
No provider-specific installer branch or disabled system protection is involved.

The formal workflow requires the reviewed material bundle's HTTPS URL and SHA-256,
then checks source commit and actual build facts. It has no automatic clearance
for rolling dependencies or an unsupported platform. Daily contract/regression
checks continue on the self-hosted Runner. Release keys stay outside the checkout.

## Verification and limits

The Python contract suite supplies a separately invented provider and package,
checks a deep upstream notice, and exercises missing source, tampered materials,
changed runtime, unknown ownership, build mismatch, path traversal, source commit
mismatch and issue erasure. Native acceptance checks real archives in Chinese/
non-BMP directories, independent installation and application/module startup.
Source rebuild and library substitution evidence are reported separately.

Mechanical evidence completeness is not a legal opinion. SDRplay API permissions,
codec patent rights, external media rights and firmware scope require their own
evidence. A functional test, a shared DLL or a GPL root text cannot waive upstream
conditions. See [licensing scope](../LICENSING.md),
[notices](../THIRD_PARTY_NOTICES.md) and
[source/replacement instructions](../SOURCE_AND_REPLACEMENT.md).
