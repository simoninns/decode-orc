# Plugin SDK / ABI Review and Improvement Plan

Review of the Decode-Orc stage-plugin SDK, its ABI versioning model, and the
plugin distribution story, with recommendations and a phased implementation
plan. File references are to the tree as of ABI v8
([orc_plugin_abi.h:91](../orc/sdk/include/orc/plugin/orc_plugin_abi.h)).

---

## 1. Current state

### 1.1 SDK surface

The SDK is a single `INTERFACE` target `orc-plugin-sdk` (exported as
`orc::plugin-sdk`, [orc/sdk/CMakeLists.txt](../orc/sdk/CMakeLists.txt))
exposing **55 public headers**:

- `orc/plugin/` (10): ABI descriptor and entrypoints, registration macros,
  service tables, and five umbrella/aggregator headers.
- `orc/stage/` (45, incl. `observers/`): stage interfaces, data-contract
  types, and utility helpers.

**What is already good:**

- Header hygiene is genuinely clean: no SDK header includes anything from
  `orc/core`, `orc/common`, `orc/presenters`, `orc/view-types`, or Qt.
  The only third-party includes are spdlog/fmt
  (`orc/stage/logging.h`, `orc/stage/node_id.h`,
  `orc/plugin/orc_plugin_services_helpers.h`).
- The enforcement gates (`cmake/check_plugin_private_includes.sh`,
  `cmake/check_plugin_private_links.sh`, `ctest -L sdk`) are hard-fail and
  run in CI.
- `orc_add_stage_plugin()`
  ([DecodeOrcPluginSDKHelpers.cmake:57](../cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKHelpers.cmake))
  gives plugins a single link target, injects the plugin version define,
  handles `instructions.md` copying, output naming, and install rules.
- An out-of-tree build story exists: `find_package(decode-orc-plugin-sdk)`
  plus the CI-exercised fixture
  [orc-tests/fixtures/external-stage-plugin/](../orc-tests/fixtures/external-stage-plugin/).

**What makes it chaotic:**

1. **Everything is one flat, undifferentiated ABI tier.** All 55 headers
   carry the same implicit stability promise. The boundary is a C++ ABI —
   vtables, `std::shared_ptr`, `std::string`, `std::variant` cross it
   (stated outright at
   [orc_plugin_abi.h:119-121](../orc/sdk/include/orc/plugin/orc_plugin_abi.h))
   — so every type in every header is layout-binding.
2. **`orc/stage/` is a flat directory with no domain grouping.** Preview
   (~9 headers: `orc_preview_*`, `preview_*`, `colour_preview_*`,
   `orc_rendering.h`, `orc_vectorscope.h`,
   `stage_custom_preview_renderer.h`), observation (`observation_*` +
   `observers/`), dropout (`dropout_run.h`, `dropout_decision.h`,
   `dropout_util.h`), audio, parameters, and identifier types all sit side
   by side, so the directory reads as an undifferentiated pile and it is
   hard to see which headers belong to which capability. Note the
   converse trap: the domain that drives ABI churn — audio — is barely
   visible as headers at all, because its contract is embedded as virtual
   methods on `VideoFrameRepresentation` (see §1.2).
3. **Utility code is accidentally ABI surface.** `lru_cache.h`,
   `frame_line_util.h`, `logging.h` are header-only conveniences;
   `eia608_decoder.h`, `dropout_util.h`, `preview_helpers.h`,
   `colour_preview_conversion.h` are declarations whose implementations are
   compiled into the **`orc-core` shared library**
   ([orc/core/CMakeLists.txt](../orc/core/CMakeLists.txt) lines 4, 10, 11,
   30). None of these are plugin *contract*; all are currently
   indistinguishable from it.
4. **Plugins still link the whole host.** `orc-plugin-sdk` carries
   `$<LINK_ONLY:orc-core>` ([orc/sdk/CMakeLists.txt:40](../orc/sdk/CMakeLists.txt))
   solely to resolve the four helper implementations above. Consequence:
   the installed `decode-orc-plugin-sdkConfig.cmake` must
   `find_dependency` **spdlog, fmt, SQLite3, yaml-cpp, PNG, and FFmpeg**
   ([DecodeOrcPluginSDKConfig.cmake:43-64](../cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKConfig.cmake)).
   A third party building a trivial pass-through stage must install the
   entire host dependency stack to satisfy configure — the "SDK-only"
   promise holds at the header level but not at the package level.
5. **The allowlist is hand-maintained in three places:** the actual
   directory tree, `SDK_ALLOWLIST` in
   [check_plugin_private_includes.sh:39-101](../cmake/check_plugin_private_includes.sh)
   (50 entries vs 55 files on disk), and the header tables in
   [docs/technical/plugin-sdk.md](../docs/technical/plugin-sdk.md). Adding a
   header requires three manual edits; nothing detects drift.
6. **Platform I/O lives in a contract header.**
   `orc/plugin/orc_stage_tooling.h` includes `<dlfcn.h>` / `<windows.h>` /
   `<fstream>` for the `instructions.md` loader — file I/O and OS headers
   inside the stability-critical SDK contract.

### 1.2 ABI versioning

Three exact-match gates are checked at load
([stage_plugin_loader.cpp:347-378](../orc/core/stage_plugin_loader.cpp)):
`host_abi_version` (currently **8**), `plugin_api_version` (currently
**2**), and `ORC_SDK_TOOLCHAIN_TAG` (exact string equality, e.g.
`gcc14/libstdc++`). Mismatch produces a clear diagnostic and the plugin is
rejected. Descriptor fields are injected by
`ORC_DEFINE_STAGE_PLUGIN` / `ORC_STAGE_PLUGIN_DESCRIPTOR`
([orc_plugin_registration.h:137-161](../orc/sdk/include/orc/plugin/orc_plugin_registration.h))
— plugin authors never hand-set version numbers. This exact-match policy is
**correct** for a C++ ABI and should be kept.

Bump history (from the header history block and git):

| ABI | Cause |
|-----|-------|
| 2 | Descriptor gained `plugin_api_version` |
| 3 | Entrypoint signature change (`OrcPluginServices*` param) |
| 4 | Data-contract swap (`VideoFieldRepresentation` → `VideoFrameRepresentation`) |
| 5 | Descriptor gained `toolchain_tag` |
| 6 | `VideoFrameRepresentation` vtable change (track-indexed audio) |
| 7 | `VideoFrameRepresentation` vtable change (channel-pair audio) |
| 8 | `VideoFrameRepresentation` vtable change (`prime_audio_decode()`) |

**Observations:**

- Five of eight bumps were vtable-layout changes on
  `VideoFrameRepresentation`; v6→v7→v8 landed within weeks. A change to one
  virtual method invalidates *every* plugin, including ones that never
  touch audio.
- The codebase already has the right mitigation pattern —
  `OrcPluginServices::services_size`
  ([orc_plugin_services.h:76-79](../orc/sdk/include/orc/plugin/orc_plugin_services.h))
  allows append-only growth of the service table without a bump — but it is
  applied only to the service table, not to where the churn actually is.
- Bumping requires manual edits in several places, and it drifts:
  the docs version table is **stale** (stops at v7,
  [plugin-sdk.md:601](../docs/technical/plugin-sdk.md)); the registry
  example still says `required_host_abi: 7`
  ([plugin-sdk.md:538](../docs/technical/plugin-sdk.md));
  [plugin-architecture.md:141](../docs/technical/plugin-architecture.md)
  still says the current ABI is 7. The CMake-side duplicate was fixed
  (commit `2deede30` — CMake now regex-parses the header), and the in-header
  `ORC_SDK_ABI_VERSION` duplicate is guarded by a `static_assert`, but the
  prose history block and both doc files have no enforcement at all.

### 1.3 Distribution and registry

The registry is a **local YAML file only**
(`~/.config/decode-orc/stage-plugins.yaml`,
[stage_plugin_registry.cpp:224-226](../orc/core/stage_plugin_registry.cpp))
listing entries the user added by hand or via the GUI. Remote artifacts are
fetched from GitHub release assets, sha256-verified when a digest is
present, and quarantined on mismatch
([plugin_remote_loader.cpp:631-658](../orc/core/plugin_remote_loader.cpp)).

Gaps:

1. **No discovery.** There is no central index, catalog, search, or
   recommendation mechanism anywhere. A user must already know a GitHub URL
   (the only built-in hint is the prefilled skeleton URL,
   [pluginmanagerdialog.cpp:579](../orc/gui/pluginmanagerdialog.cpp)).
2. **Compatibility is discovered too late.** `required_host_abi` is parsed
   and serialized ([stage_plugin_registry.h:45](../orc/core/include/stage_plugin_registry.h))
   but **never consumed by any gate**; artifact names
   (`orc-plugin_<stage>_<platform>.<ext>`) carry no ABI/toolchain token.
   An incompatible plugin downloads, caches, and only fails at `dlopen`.
   There is also no way for one plugin release to serve two host ABIs
   side by side.
3. **Integrity is soft.** `sha256` is optional (missing digest → load with
   warning), the GUI remote-add flow has **no field to enter a digest**,
   and neither artifacts nor the registry are signed (self-documented at
   [plugin-architecture.md:235-239](../docs/technical/plugin-architecture.md)).
4. **Trust is conflated with enable** in the GUI — ticking the Enabled
   checkbox silently grants trust
   ([pluginmanagerdialog.cpp:504-514](../orc/gui/pluginmanagerdialog.cpp)).
5. **Implementation duplication/brittleness:** the artifact-name regex
   exists twice ([stage_plugin_registry.cpp:43](../orc/core/stage_plugin_registry.cpp),
   [plugin_remote_loader.cpp:437](../orc/core/plugin_remote_loader.cpp));
   the GitHub API JSON is parsed with regexes
   ([plugin_remote_loader.cpp:294-321](../orc/core/plugin_remote_loader.cpp));
   remote transport is hardcoded to GitHub; the CLI has no remote-URL add.

### 1.4 Documentation

[plugin-sdk.md](../docs/technical/plugin-sdk.md) and
[plugin-architecture.md](../docs/technical/plugin-architecture.md) are
substantial and their header/CMake content matches the tree, but: the
version table and ABI references are stale (§1.2); `README.md:58` and
`plugin-architecture.md:355` reference a `3rd-party-plugins/` directory
that does not exist; and there is no tutorial-style walkthrough — the
closest things are the SDK reference sections, the CI fixture, and the
external skeleton repo (`https://github.com/simoninns/orc-plugin_skeleton`).

---

## 2. Recommendations

**R1 — Tier the SDK surface explicitly.** Split headers into three tiers
with different stability promises:

- `orc/abi/` — the frozen binary contract: descriptor, entrypoints,
  registration, service tables. Changes here always bump the ABI.
- `orc/stage/` — the stage contract: interfaces and data types that cross
  the boundary (`DAGStage`, `VideoFrameRepresentation`, parameter types,
  ids, metadata). Changes here bump the ABI when layout changes.
  Within this tier, group headers by **domain** as subdirectories —
  `orc/stage/preview/`, `orc/stage/observation/`, `orc/stage/dropout/`,
  `orc/stage/audio/`, `orc/stage/params/` — with the small set of
  foundation types (ids, `stage.h`, `common_types.h`, frame
  representation) remaining at the tier root. The header manifest (Phase
  1) carries the domain as a field, so the docs reference, the allowlist,
  and the ABI history can all be grouped and reasoned about per domain.
- `orc/support/` — compiled-into-plugin utilities (`lru_cache.h`,
  `frame_line_util.h`, `logging.h`, tooling/instructions loader, observer
  helpers, and the four algorithm helpers below). Explicitly **not** ABI:
  changes never require a bump, only a plugin recompile at the author's
  leisure.

Keep compile-compatibility shims at the old include paths for one release
so existing plugin source keeps building.

**R2 — Sever the plugin→orc-core link so plugins compile against the SDK
alone.** Move `eia608_decoder`, `dropout_util`, `preview_helpers`, and
`colour_preview_conversion` implementations out of `orc-core` into a small
position-independent **static** library (`orc-sdk-support`) shipped with
the SDK and linked into each plugin (`RTLD_LOCAL` loading already isolates
per-plugin copies). Then delete `$<LINK_ONLY:orc-core>`, drop
`orc-core`/`orc-common` from the export set, and slim the installed CMake
config's dependencies to spdlog + fmt. Publish the installed SDK as a
standalone versioned release artifact so plugin authors need neither the
host source tree nor its dependency stack. This directly answers "can
plugins compile against something specific": yes — the mechanism half
exists today; only the orc-core link tether prevents it.

**R3 — Keep the exact-match ABI policy, but make bumps rarer, cheaper, and
self-consistent.**

- *Rarer:* shrink what counts as ABI (R1/R2); adopt the
  `services_size` append-only pattern as the default way to add host
  capabilities; batch planned vtable changes on `VideoFrameRepresentation`
  into scheduled contract revisions instead of one bump per method; and,
  as the structural fix for the audio churn specifically, factor
  churn-prone domains **out of the `VideoFrameRepresentation` vtable**
  into their own interfaces obtained via a capability accessor (e.g.
  `frame.audio()` returning an `IFrameAudio*`, header
  `orc/stage/audio/`). Domain grouping of headers (R1) makes the surface
  navigable, but only this factoring lets a domain's contract grow by
  *adding a new interface* — additive, no layout change to existing
  vtables — instead of editing the one vtable every plugin depends on.
  Publish
  an "ABI impact" decision table so contributors know what does and does
  not force a bump.
- *Cheaper:* a single machine-readable ABI history file as the source of
  truth, from which the docs table is generated; a CI guard that fails when
  `kStagePluginHostAbiVersion` changes without matching history/docs
  updates; generate the enforcement-script allowlist from a manifest so a
  new header is one edit, not three.
- *Failing early:* enforce `required_host_abi` in the registry before
  download, and add an ABI token to artifact naming
  (`orc-plugin_<stage>_<platform>_abi<N>.<ext>`, with legacy-name
  fallback) so one plugin release can carry builds for multiple host ABIs
  and users get "this plugin needs a rebuild for Orc ABI 8" instead of a
  post-download load failure.

**R4 — Add a curated plugin registry index alongside manual URL entry.**
A versioned YAML index in **its own dedicated repository**
(e.g. `orc-plugin-registry`), fetched by the host directly from the
repository's default branch over plain HTTPS (raw file, no GitHub API).
Because the host reads the branch head, index updates are fully decoupled
from releases on either side: the moment a registry PR merges, every
Decode-Orc installation sees the new list — no decode-orc release, no
registry tag. The host refreshes the index when the plugin manager (or a
CLI discovery command) is opened, asynchronously, falling back to the
last-good cached copy when offline.

The index lists per plugin: id, name, description, tags, maintainer,
license, source repo, and per (platform × ABI) artifact URL +
**mandatory** sha256. The host gains "Browse plugins" in the GUI and
`orc-cli plugins search/info/install`.

Contribution model: plugin authors PR their entry to the registry repo;
repo CI validates every PR (schema, artifact URL reachable, digest
matches the artifact, naming convention, SPDX license); the maintainer's
merge **is** the curation/trust decision and publishes immediately. Two
consequences follow: (a) the index evolves faster than installed hosts,
so old hosts must tolerate newer indexes — unknown fields ignored, minor
schema additions non-breaking; (b) the per-(platform × ABI) artifact
keying is what lets an older host still select a compatible build from a
freshly updated list (or report "no build for this host" before
downloading anything).

Manual URL entry remains for unlisted/private plugins. Index-driven
resolution also removes the regex-parsed GitHub API dependency for listed
plugins. Signing (minisign/sigstore of the index) is a documented
follow-on, not a blocker.

**R5 — Ship an author-facing documentation set** (detailed in the plan):
a tutorial walkthrough, a publishing guide, a versioning/ABI policy page
with the generated history table, and a tiered header reference — plus
fixes for the stale/broken references found in §1.2 and §1.4.

---

## 3. Implementation plan

### Phase 1 — SDK tiering and hygiene

1. **Introduce the `orc/support/` tier.** Move `lru_cache.h`,
   `frame_line_util.h`, `logging.h`, and the `instructions.md`/tooling
   loader parts of `orc_stage_tooling.h` (the `<dlfcn.h>`/`<windows.h>`/
   `<fstream>` code) into `orc/sdk/include/orc/support/`; leave
   deprecated shim headers at the old paths that `#include` the new ones.
   *Acceptance:* tree builds; `ctest -L sdk` and full unit suite pass;
   every moved header carries a banner stating its tier and stability
   promise; shims emit no errors for in-tree plugins.
2. **Introduce the `orc/abi/` tier.** Move `orc_plugin_abi.h`,
   `orc_plugin_registration.h`, `orc_plugin_services.h` (and the umbrella
   `orc_plugin_sdk.h`) under `orc/sdk/include/orc/abi/` with shims as
   above; contract headers must include nothing from `orc/support/`.
   *Acceptance:* a new hygiene scan (task 6) proves `orc/abi/` and
   `orc/stage/` headers include only SDK-tier, stdlib, or spdlog/fmt
   headers; build and `-L sdk` green.
3. **Group `orc/stage/` headers by domain.** Create
   `orc/stage/preview/`, `orc/stage/observation/` (absorbing
   `observers/`), `orc/stage/dropout/`, `orc/stage/audio/`, and
   `orc/stage/params/`; move the matching headers there with deprecated
   shims at the old paths; foundation types (`stage.h`, `common_types.h`,
   ids, `video_frame_representation.h`, metadata) stay at the tier root.
   *Acceptance:* tree builds; shims cover the old paths; `ctest -L sdk`
   passes; each domain directory maps one-to-one to a domain value in the
   manifest (task 5).
4. **Migrate all bundled stage plugins to the tiered paths.** Update
   every plugin under [orc/plugins/stages/](../orc/plugins/stages/)
   (including `common/` and `sinks/common/`), the test fixtures
   (`orc-tests/fixtures/external-stage-plugin/`, plugin-loader test
   plugins), and the skeleton repo to include the new `orc/abi/`,
   `orc/stage/<domain>/`, and `orc/support/` paths directly — no bundled
   plugin may compile through a deprecation shim. Gate the shims behind a
   CMake option (e.g. `ORC_SDK_DEPRECATED_INCLUDE_SHIMS`, default ON for
   third-party source compatibility) and schedule shim removal for one
   release after the tiered SDK ships.
   *Acceptance:* CI builds the tree with the shim option OFF and all
   plugin, unit, and `-L sdk` suites pass; grep of `orc/plugins/` and
   `orc-tests/` finds no pre-tier include paths; skeleton repo update
   tracked as a follow-up PR there.
5. **Single-source the header allowlist.** Add
   `orc/sdk/sdk_headers.yaml` (path, tier, domain, since-ABI, notes) as
   the manifest; rewrite `check_plugin_private_includes.sh` (bash-3.2
   compatible) to read the generated flat list, and add a CTest that fails
   when the manifest and the on-disk header set diverge.
   *Acceptance:* deleting/adding a header without a manifest edit fails
   `ctest -L sdk`; the script contains no hand-typed header list.
6. **Add an SDK self-hygiene gate.** New `-L sdk` test scanning all SDK
   headers for includes outside {SDK tiers, C/C++ stdlib, spdlog, fmt,
   platform headers in `orc/support/` only}.
   *Acceptance:* gate passes on the tree; seeding a `orc/core/...` include
   into any SDK header fails it.
7. **Update docs for the tier model.** Regenerate the header tables in
   [plugin-sdk.md](../docs/technical/plugin-sdk.md) from the manifest
   (script in `cmake/` or `tools/`), grouped by tier and domain; document
   the three tiers and their stability promises.
   *Acceptance:* doc tables byte-match the generator output; a CI/CTest
   check enforces this.

### Phase 2 — Standalone SDK package (no host link)

1. **Create `orc-sdk-support` static library.** Move
   `eia608_decoder.cpp`, `dropout_util.cpp`, `preview_helpers.cpp`,
   `colour_preview_conversion.cpp` from `orc/core/` into `orc/sdk/src/`;
   build as PIC static lib; link it into `orc-plugin-sdk` (INTERFACE) and
   into `orc-core` (so host behaviour is unchanged); move their headers to
   the `orc/support/` tier.
   *Acceptance:* full test suite passes; `nm`/scan shows stage plugins no
   longer import these symbols from `orc-core`.
2. **Remove the orc-core tether.** Delete `$<LINK_ONLY:orc-core>` from
   [orc/sdk/CMakeLists.txt](../orc/sdk/CMakeLists.txt); remove `orc-core`
   and `orc-common` from `decode-orc-plugin-sdkTargets`; update
   `check_plugin_private_links.sh` if patterns change.
   *Acceptance:* all in-tree plugins and tests link and pass; the export
   set contains only `orc-plugin-sdk` + `orc-sdk-support`.
3. **Slim the installed CMake config.** Reduce
   [DecodeOrcPluginSDKConfig.cmake](../cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKConfig.cmake)
   `find_dependency` calls to spdlog and fmt only.
   *Acceptance:* the external fixture
   ([orc-tests/fixtures/external-stage-plugin/](../orc-tests/fixtures/external-stage-plugin/))
   configures and builds against an install prefix in an environment
   without FFmpeg, yaml-cpp, PNG, or SQLite3 (CI job proves it).
4. **Publish the SDK as a release artifact.** CI packages the install tree
   (headers + static lib + CMake config + enforcement scripts) as
   `decode-orc-plugin-sdk-<version>-<platform>.tar.gz` attached to host
   releases; document `find_package` against an unpacked tarball and
   `FetchContent` usage.
   *Acceptance:* CI builds the external fixture from the packaged tarball
   (not the source tree) on Linux; artifact appears in the release
   workflow outputs.

### Phase 3 — ABI lifecycle automation and early gating

1. **Machine-readable ABI history.** Add `orc/sdk/abi_history.yaml`
   (version, api version, summary, cause category, contract headers
   affected); replace the prose history block in
   [orc_plugin_abi.h](../orc/sdk/include/orc/plugin/orc_plugin_abi.h) with
   a pointer to it; generate the docs version table from it. Fix the stale
   v8 row, the `required_host_abi: 7` example, and the "current = 7" text
   in [plugin-architecture.md](../docs/technical/plugin-architecture.md)
   as part of this task.
   *Acceptance:* a CTest fails when `kStagePluginHostAbiVersion` has no
   matching `abi_history.yaml` entry or the generated docs table is out of
   date; regenerated docs show v8.
2. **Bump procedure guard.** Add `tools/check_abi_bump.sh` run in CI: if
   the header constant differs from the last history entry, or a
   `orc/stage/` contract header changed without either a bump or an
   explicit `abi-neutral` marker in the history file, fail with a
   checklist message.
   *Acceptance:* simulated bump without history/docs edits fails CI;
   documented checklist appears in the failure output.
3. **Enforce `required_host_abi` in the registry.** Gate download and load
   on `required_host_abi == kStagePluginHostAbiVersion` when the field is
   non-zero; surface "requires rebuild for Orc ABI N (host is M)" in GUI
   and `orc-cli plugins list`.
   *Acceptance:* unit tests cover match, mismatch, and legacy-zero cases;
   mismatched entries are neither downloaded nor loaded and show the
   message.
4. **ABI-tagged artifact naming.** Extend the naming convention to
   `orc-plugin_<stage>_<platform>_abi<N>.<ext>`; resolution prefers the
   host-ABI-tagged asset and falls back to the legacy name with a warning;
   deduplicate the artifact-name regex into one shared helper used by both
   [stage_plugin_registry.cpp](../orc/core/stage_plugin_registry.cpp) and
   [plugin_remote_loader.cpp](../orc/core/plugin_remote_loader.cpp).
   *Acceptance:* unit tests cover tagged-preferred / legacy-fallback /
   no-match; regex exists in exactly one translation unit; docs updated.
5. **Publish the ABI impact decision table.** New section in
   [plugin-sdk.md](../docs/technical/plugin-sdk.md): what forces a bump
   (descriptor/entrypoint layout, contract vtables, cross-boundary type
   layout), what does not (support-tier changes, append-only
   `services_size` growth, registration-template changes), and the
   preferred additive patterns — including the capability-accessor
   factoring from R3 (new domain interfaces reached via an accessor,
   e.g. `frame.audio()`, instead of new virtuals on
   `VideoFrameRepresentation`) as the sanctioned way to grow a domain
   contract; apply it opportunistically at the next planned
   `VideoFrameRepresentation` revision rather than as a standalone bump.
   *Acceptance:* table present, cross-linked from
   `plugin-architecture.md` and the bump-guard failure message.

### Phase 4 — Plugin registry index and discovery

1. **Define the index schema and bootstrap the registry repository.**
   Versioned YAML (`registry_schema: 1`) in a dedicated repository
   (`orc-plugin-registry`), read from the default branch head: per plugin
   — id, display name, description, tags, maintainer, license (SPDX),
   source repo URL, and artifacts keyed by (platform, host ABI) with URL
   + mandatory sha256 + plugin version + minimum host app version.
   Forward-compatibility rules are part of the schema: hosts ignore
   unknown fields; additions within a schema major version are
   non-breaking. Document contribution-by-PR curation rules in the repo.
   *Acceptance:* schema documented in
   [plugin-architecture.md](../docs/technical/plugin-architecture.md) and
   the registry repo; fixture index files added to `orc-tests` for parser
   tests, including a newer-minor-schema fixture with unknown fields.
2. **Registry repo PR validation CI.** Workflow in the registry repo that
   validates every PR before merge: schema conformance, artifact URLs
   resolve, downloaded artifact digest equals the entry's sha256,
   artifact naming convention (incl. ABI token), and SPDX license
   present. Merge is the acceptance/publication step — no tagging or
   release needed for the list to go live.
   *Acceptance:* CI rejects fixture PRs for each failure class (bad
   schema, unreachable URL, digest mismatch, bad name, missing license);
   a valid fixture PR passes.
3. **Core index client.** Fetch the raw index over HTTPS from the
   configurable registry URL (default: the registry repo's default-branch
   raw file); refresh on demand — when the plugin manager opens or a CLI
   discovery command runs — with the last-good cached copy as offline
   fallback; parse with yaml-cpp (no regex JSON) and resolve artifacts by
   (platform, host ABI) using the Phase 3 gating, so an incompatible-host
   case is reported before any download. Entries installed from the index
   carry their sha256 into the local registry.
   *Acceptance:* unit tests (mocked transport) cover fetch, cached
   fallback on network failure, schema-version and unknown-field
   tolerance, (platform × ABI) resolution incl. no-compatible-build,
   and sha256 propagation; no network in unit tests.
4. **CLI discovery commands.** `orc-cli plugins search <term>`,
   `plugins info <id>`, `plugins install <id>` on top of the index client;
   `install` records the entry untrusted-until-confirmed consistent with
   existing trust semantics.
   *Acceptance:* command tests with mocked index; help text updated;
   existing `add/trust/enable` flows unchanged.
5. **GUI browse dialog.** "Browse plugins…" in the plugin manager: list +
   search + details (description, license, maintainer, source link) +
   install, alongside the existing URL entry. Opening the plugin manager
   triggers an asynchronous index refresh (non-blocking; cached list
   shown immediately with a staleness/offline indicator).
   Presenter-boundary only, per MVP rules; tier-appropriate tests under
   `orc-tests/gui/unit/`.
   *Acceptance:* `gui-model` tests for the browse model with mocked
   presenter, covering refresh-on-open, offline fallback, and stale
   indicator; `gui-widget` offscreen smoke test for the dialog.
6. **Separate trust from enable in the GUI.** Distinct trust affordance
   (explicit confirmation dialog showing publisher/source, license, and
   digest status); enabling no longer implicitly grants trust; index
   installs and URL adds funnel through the same confirmation.
   *Acceptance:* `gui-model` tests prove enable does not mutate
   `trust_state`; registry round-trip tests updated.
7. **Document the integrity roadmap.** Update the "Distribution
   integrity" section: what the index adds (mandatory digests, curated
   review), what remains future work (signed index, code signing), and the
   threat model of an unsigned local registry.
   *Acceptance:* section updated in both plugin docs; no code change.

### Phase 5 — Plugin developer documentation

1. **Author tutorial.** New `docs/technical/plugin-author-guide.md`:
   end-to-end walkthrough — obtain the SDK package (Phase 2 artifact) or
   host install; clone the skeleton
   (`https://github.com/simoninns/orc-plugin_skeleton`); implement a
   minimal transform stage; write `instructions.md`; run the enforcement
   gates standalone; load via `ORC_STAGE_PLUGIN_PATHS` and
   `orc-cli plugins list`. Reference the CI fixture
   [orc-tests/fixtures/external-stage-plugin/](../orc-tests/fixtures/external-stage-plugin/)
   as the canonical minimal example.
   *Acceptance:* every command in the guide is copy-paste runnable against
   a release build; added to `docs/technical/.nav.yml`.
2. **Publishing guide.** New `docs/technical/plugin-publishing.md`:
   repository naming (`orc-plugin_<stage>`), ABI-tagged release-asset
   naming, generating and publishing sha256 digests, rebuild expectations
   on host ABI bumps, and how to submit an index entry (Phase 4).
   *Acceptance:* guide added to nav; skeleton repo README cross-links it.
3. **Restructure the SDK reference for tiers.** Update
   [plugin-sdk.md](../docs/technical/plugin-sdk.md): tiered header
   reference generated from `sdk_headers.yaml`, stability guarantees per
   tier, standalone-package consumption, and the generated version-history
   table; update
   [plugin-architecture.md](../docs/technical/plugin-architecture.md)
   compatibility and registry sections for Phases 3–4.
   *Acceptance:* generated sections match generators (CI-checked); no
   references to the pre-tier layout remain outside the history table.
4. **Fix broken and stale references.** Remove/replace the
   `3rd-party-plugins/` references (`README.md:58`,
   `plugin-architecture.md:355`); point README's plugin section at the
   author guide; update `AGENTS.md` §9 doc-sync table to name the
   generated artifacts (manifest, history file) instead of hand-edited
   tables.
   *Acceptance:* repo-wide grep for `3rd-party-plugins` returns nothing;
   AGENTS.md table references the new source-of-truth files.
