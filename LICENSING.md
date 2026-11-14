# Licensing

English · [简体中文](LICENSING.zh-CN.md)

The Foreshadow copyright holders license the first-party desktop application,
command-line application, receiver plugins, bundled analysis/display/research
providers, plugin SDK, and associated first-party build, test, maintenance,
resource and documentation code under **GNU GPL version 3 or, at your option,
any later version** (`GPL-3.0-or-later`). The unmodified license is in [LICENSE](LICENSE).
Existing copyright notices remain authoritative; this grant does not imply a
copyright assignment to a new legal entity.

The grant applies to first-party work in `src/`, `include/`, `plugins/`, `cmake/`,
`tools/`, `resources/`, `translations/`, the first-party portions of `tests/`,
and the project's own documentation and build files. It also covers first-party
source preserved in historical project snapshots. It does not relicense imported
code, upstream source archives, third-party notices, external media or standards.
Third-party components retain their own terms and copyright notices.

## Separate scopes

- `third_party/dra` is imported MIT code; its original LICENSE and provenance apply.
- The dtmb-sdr-derived sampler retains the MIT notice in
  `third_party/dtmb-sampler-LICENSE`, in addition to the applicable terms for our
  modifications and the combined program.
- Generated television fixture media and their provenance retain their existing
  CC0-1.0 dedication. First-party generator code is GPL-3.0-or-later.
- RF captures, standard-derived numerical data, external interoperability vectors
  and third-party report assets retain their documented provenance. This grant
  does not claim rights held by other parties. Non-build test recordings and
  unreviewed external assets are omitted from distributed corresponding-source
  sets; the omission manifest identifies them. Production source builds use
  `BUILD_TESTING=OFF` and do not depend on those recordings.
- `firmware/pager_tx` has a separate embedded SDK and distribution boundary. Its
  final first-party license scope and combined firmware clearance remain under
  review; the desktop grant does not relicense the Arduino/ESP-IDF SDK or grant
  redistribution rights in vendor binary libraries.
- SDRplay's API is separately licensed vendor software. From 0.2.12 the optional
  GPL receiver client communicates with an independently installed SoapyRemote
  service. The application/client do not load or include that vendor API or the
  local hardware driver. The service's BSL code and user-installed vendor software
  retain their own terms; no vendor redistribution grant is inferred. See the
  [receiver-service boundary](docs/receiver-services.md).

GPL permits commercial use and charging for distribution, subject to its terms.
The project adds no noncommercial, no-competition or reverse-engineering ban.
User recordings and analysis output do not become GPL-covered merely because
Foreshadow reads or generates them. The license does not grant trademark rights
or establish third-party media or codec patent clearance.

## Binary and plugin distribution

Each application or independently distributed plugin package must contain its
applicable third-party notices, license texts, corresponding-source material,
build/patch information and replacement instructions. A file in the main
application package does not discharge an independent plugin download's own
requirements. Provider-owned declarations describe these materials; generic
packaging and release checks resolve and validate their closure.

Qt is used through the selected LGPLv3 route for the actual runtime modules.
Their complete applicable notices, exact source identity and replacement rights
must be preserved. A shared DLL alone is not a compliance conclusion. See
[third-party notices](THIRD_PARTY_NOTICES.md),
[source and replacement instructions](SOURCE_AND_REPLACEMENT.md), and the
[licensing assessment](docs/licensing-assessment.md).

Review-only packages may retain explicitly recorded external licensing issues
for private evaluation. They are not cleared for public publication. Release
checks reject missing materials, unknown payload ownership and unresolved
distribution conditions. Functional acceptance is separate from license clearance.
