# Plugin SDK Hardening Plan

Implementation plan for hardening the stage-plugin SDK/ABI design and
implementation. Derived from a design review of `orc/sdk/`, the plugin loader
(`orc/core/stage_plugin_loader.cpp`, `orc/core/stage_registry.cpp`), the
enforcement gates (`cmake/check_plugin_private_includes.sh`,
`cmake/check_plugin_private_links.sh`), and the published documentation
(`docs/technical/plugin-sdk.md`, `docs/technical/plugin-architecture.md`).

Direction decisions baked into this plan:

- The SDK boundary moves from a **blocklist** of private headers to an
  **allowlist** of contract headers under `orc/sdk/include/`.
- Plugins are loaded with `RTLD_LOCAL` and built with hidden symbol
  visibility; plugin shared-library lifetime is tied to live stage instances.
- The compatibility policy is **exact-match** for both version numbers
  (matching the current loader behaviour); the `services_size` append-only
  mechanism is retained but documented as an intra-version safety net only.
- `trust_state` in the plugin registry is enforced before `dlopen`, and
  downloaded artifacts gain checksum verification.
- The `StagePluginDescriptor` gains a toolchain/stdlib tag (requires a
  `host_abi_version` bump to 5, batched with all other ABI-touching changes
  in a single phase).

Validation gates that apply to every phase (see [AGENTS.md](../AGENTS.md) §4.6
and §9.2):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
ctest --test-dir build -L sdk --output-on-failure
ctest --test-dir build -R "StagePluginLoader" --output-on-failure
ctest --test-dir build -R MVPArchitectureCheck --output-on-failure
```

---

## Phase 1 — Documentation accuracy

Bring the published SDK documentation and in-header documentation back in
sync with the implementation. No behavioural changes.

### Task 1.1 — Correct the minimal plugin example in plugin-sdk.md

Rewrite the "Implementing a Stage" / "Minimal plugin structure" section of
[docs/technical/plugin-sdk.md](../docs/technical/plugin-sdk.md) so the example
compiles against the real ABI:

- Descriptor uses the actual `StagePluginDescriptor` fields and declaration
  order from [orc_plugin_abi.h](../orc/sdk/include/orc/plugin/orc_plugin_abi.h)
  (`plugin_id`, `plugin_version`, `host_abi_version`, `plugin_api_version`,
  `license_spdx`, `is_core_plugin`).
- Entrypoint signature matches `OrcRegisterStagePluginFn` (returns `bool`,
  takes `const OrcPluginServices*` first, `void* context`, the
  `register_stage` callback, and `const char** error_message`).
- Remove the nonexistent `OrcStageRegisterFn` type.
- Factory returns `std::shared_ptr<DAGStage>` (`OrcStageFactoryFn`), not
  `std::unique_ptr`.
- `orc::plugin::get_stage_services()` shown with its real no-argument
  signature, preceded by `orc::plugin::set_services(services)`.

**Acceptance criteria:**
- The documented example matches a real in-tree plugin entrypoint (e.g.
  [orc/plugins/stages/mask_line/plugin.cpp](../orc/plugins/stages/mask_line/plugin.cpp)).
- No type, function, or field named in the section is absent from the SDK
  headers.

### Task 1.2 — Correct IStageServices capability claims

`IStageServices` currently provides only two buffered-file-writer factories
([orc_stage_services.h](../orc/sdk/include/orc/plugin/orc_stage_services.h)).
Fix every place that claims otherwise:

- [docs/technical/plugin-sdk.md](../docs/technical/plugin-sdk.md) "Host
  services" section (claims `log_message()`, `report_progress()`, artifact
  delivery callbacks).
- [docs/technical/plugin-architecture.md](../docs/technical/plugin-architecture.md)
  "Stage Services" section (claims artifact I/O, logging, progress).
- The ABI v4 history comment in
  [orc_plugin_abi.h](../orc/sdk/include/orc/plugin/orc_plugin_abi.h)
  ("IStageServices gains VFrameR delivery methods" — those methods do not
  exist).
- Document that logging goes through `OrcPluginServices::log` (via the
  `ORC_PLUGIN_LOG_*` macros), not through `IStageServices`.

**Acceptance criteria:**
- Every documented `IStageServices` method exists in the header, and every
  header method is documented.
- The version-history table in plugin-sdk.md matches the history comments in
  orc_plugin_abi.h.

### Task 1.3 — Correct entrypoint signatures in plugin-architecture.md and the umbrella header

- Replace the pre-v3 entrypoint prototypes in
  [docs/technical/plugin-architecture.md](../docs/technical/plugin-architecture.md)
  ("Plugin Binary Format" section) with the current signatures.
- Update the QUICK START comment block in
  [orc_plugin_sdk.h](../orc/sdk/include/orc/plugin/orc_plugin_sdk.h), which
  still shows `orc_register_stage_plugin` without the
  `const OrcPluginServices*` first parameter.
- Fix the ParameterizedStage description in plugin-sdk.md: the real API is
  map-based `get_parameters()` / `set_parameters()` plus
  `get_parameter_descriptors()`, not per-key `set_parameter()` /
  `get_parameter()`.

**Acceptance criteria:**
- Copying the umbrella-header quick start into a fresh plugin source file
  compiles (verified manually against an in-tree plugin build).
- plugin-architecture.md "Compatibility Gating" and "Plugin Binary Format"
  sections describe ABI v4 signatures exactly.

### Task 1.4 — Document the C++ ABI contract and compatibility policy

Add a "Binary compatibility model" subsection to both doc files stating
explicitly:

- The plugin boundary is a C++ ABI: `std::shared_ptr`, `std::string`,
  exceptions, and vtables cross it. Matching version numbers require the
  same compiler family, standard library, and build configuration
  (Debug/Release CRT on Windows).
- The host requires exact equality for `host_abi_version` and
  `plugin_api_version`
  ([stage_plugin_loader.cpp](../orc/core/stage_plugin_loader.cpp)
  compatibility checks); `services_size` guarding exists only as an
  intra-version safety net for appended service fields.
- Soften the "Signed-registry distribution" wording in
  plugin-architecture.md until Phase 8 lands (there is currently no
  signature or checksum verification).

**Acceptance criteria:**
- Both docs carry the same compatibility-model statement.
- No doc text promises signing, checksums, or trust enforcement that the
  code does not perform.

---

## Phase 2 — Build and include hygiene

Small mechanical cleanups with no ABI impact.

### Task 2.1 — Reconcile plugin library type documentation (resolved: SHARED)

`orc_add_stage_plugin()` in
[DecodeOrcPluginSDKHelpers.cmake](../cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKHelpers.cmake)
created `SHARED` libraries while its documentation said MODULE. MODULE would
be the semantically correct type for dlopen-only artifacts, but the in-tree
test architecture deliberately links stage plugin targets into the unit and
functional test executables to exercise stage classes in-process
([orc-tests/core/unit/CMakeLists.txt](../orc-tests/core/unit/CMakeLists.txt),
[orc-tests/core/functional/CMakeLists.txt](../orc-tests/core/functional/CMakeLists.txt)),
and MODULE targets cannot be linked. Converting would require splitting
every plugin into an object library plus a MODULE wrapper and separating the
`extern "C"` entrypoint TU in all plugin builds (the entrypoint symbols would
otherwise collide when multiple plugins are linked into one test binary) — a
restructuring that is not justified by the hygiene benefit.

**Resolution:** plugins remain `SHARED`; the helper's comments document the
dual use (dlopen at runtime, direct linking in tests) so the SHARED choice
reads as deliberate rather than accidental.

**Acceptance criteria:**
- `orc_add_stage_plugin()` comments state the SHARED rationale; no comment
  claims MODULE.
- All in-tree plugins build, are discovered, and load at runtime on Linux
  (`ctest --test-dir build -R "StagePluginLoader" --output-on-failure`
  passes).

### Task 2.2 — Replace relative SDK include paths in plugin code

Replace all `#include "../../../sdk/include/orc/plugin/..."` occurrences in
`orc/plugins/stages/` (~40 across stage headers/sources, e.g.
[mask_line_stage.h](../orc/plugins/stages/mask_line/mask_line_stage.h)) with
`#include <orc/plugin/...>`; the SDK include directory is already on every
plugin target via `orc-plugin-sdk`.

**Acceptance criteria:**
- `grep -rn 'sdk/include/orc/plugin' orc/plugins/stages` returns no matches.
- Full build and all unit tests pass.

### Task 2.3 — Remove the ORC_SDK_ABI_VERSION duplication footgun

`ORC_SDK_ABI_VERSION` in
[orc_plugin_abi.h](../orc/sdk/include/orc/plugin/orc_plugin_abi.h) is a
manually duplicated literal alongside `kStagePluginHostAbiVersion`. Keep the
macro (it must remain preprocessor-usable) but add a
`static_assert(kStagePluginHostAbiVersion == ORC_SDK_ABI_VERSION, ...)`
immediately after both definitions so a future bump cannot desynchronise
them. Also remove the unused `<string>` / conditional `<fmt/format.h>`
includes from
[orc_plugin_services.h](../orc/sdk/include/orc/plugin/orc_plugin_services.h)
if compilation allows (fmt is used only by
orc_plugin_services_helpers.h).

**Acceptance criteria:**
- Changing either constant alone fails compilation.
- SDK umbrella header still compiles standalone in a plugin TU.

---

## Phase 3 — Loader isolation hardening

Runtime behaviour changes to the plugin loader. Each task needs unit-test
coverage per [TESTING.md](../TESTING.md) (mocked; `unit` label) plus the
existing `StagePluginLoader` functional coverage.

### Task 3.1 — Load plugins with RTLD_LOCAL (hidden visibility resolved: not applied)

- Change `open_shared_library()` in
  [stage_plugin_loader.cpp](../orc/core/stage_plugin_loader.cpp) from
  `RTLD_NOW | RTLD_GLOBAL` to `RTLD_NOW | RTLD_LOCAL`. This alone removes
  the cross-plugin hazard: each plugin's symbols (including SDK inline
  variables such as `orc::plugin::g_services`) stay private to that plugin
  instead of unifying with the first-loaded plugin's copies.
- **Hidden default visibility is deliberately not applied.** The in-tree
  test executables link plugin libraries and reference stage class symbols
  directly (same constraint as Task 2.1); `CXX_VISIBILITY_PRESET hidden`
  would make those symbols undefined at test link time. Default visibility
  also keeps typeinfo names as ordinary strings, which is what allows
  libstdc++'s string-comparison RTTI fallback to work across the
  `RTLD_LOCAL` boundary.
- Verify cross-boundary RTTI: the host performs `dynamic_cast` on
  plugin-created stages to `ParameterizedStage`, `TriggerableStage`,
  `IStagePreviewCapability`, `StageToolProvider`, and
  `AnalysisToolProvider`. Add a functional test that loads a real plugin
  through a test executable that does **not** link any plugin target (a
  linked plugin would be pre-mapped and make the check trivial) and
  confirms each implemented mixin cast succeeds under `RTLD_LOCAL`.

**Acceptance criteria:**
- All bundled plugins load; mixin interfaces are reachable via
  `dynamic_cast` from a non-plugin-linked functional test (labelled
  `functional` + `contracts`).
- GUI smoke: parameter dialogs and stage tools still resolve for plugin
  stages.

### Task 3.2 — Tie plugin library lifetime to live stage instances

`StageRegistry::clear()` calls `plugin_loader_.unload_all()`
([stage_registry.cpp](../orc/core/stage_registry.cpp)) while stage objects
created by plugin factories may still be alive elsewhere, leaving dangling
vtables after `dlclose`. Fix:

- In `StagePluginLoader::register_stage_trampoline`, wrap the returned
  factory so each created `DAGStagePtr` holds a keep-alive reference
  (shared ownership of the plugin handle entry, e.g. via a custom deleter
  or aliasing constructor capturing a `shared_ptr` handle token).
- `unload_all()` releases the loader's own references; the library is
  `dlclose`d only when the last stage instance is destroyed.
- Document the thread-safety and lifetime guarantees on the loader class
  (AGENTS.md §5.3.3).

**Acceptance criteria:**
- New unit test: a stage instance obtained from a mocked/factory-loaded
  plugin remains callable after `unload_all()`; the handle closes only after
  the last instance is released.
- No regression in `ctest --test-dir build -R "StagePluginLoader"`.

### Task 3.3 — Consolidate registration-time fault handling documentation

`plugin_safe_call`
([plugin_safe_call.h](../orc/core/include/plugin_safe_call.h)) warns that
guarded bodies must avoid C++ destructors, but the registration path runs
C++ (string/vector allocation in the trampoline). Either narrow the guarded
regions to the raw plugin function-pointer invocations, or document the
accepted residual risk at each `plugin_safe_call` call site in
[stage_plugin_loader.cpp](../orc/core/stage_plugin_loader.cpp) and
[stage_registry.cpp](../orc/core/stage_registry.cpp).

**Acceptance criteria:**
- Each `plugin_safe_call` site either guards only raw plugin calls or
  carries a rationale comment referencing the header's constraint.
- Existing loader fault-injection tests still pass.

---

## Phase 4 — SDK surface inventory and allowlist definition

Define the real contract surface before flipping enforcement. No gate
changes yet.

### Task 4.1 — Inventory actual plugin header usage

Produce (and commit under `docs-tech/`) an inventory of every non-SDK header
included by code in `orc/plugins/stages/`, ranked by usage count, tagged
with its owning module (orc-core include/, orc-core stages/, orc-common).
Current known heavy hitters: `logging.h`, `video_frame_representation.h`,
`triggerable_stage.h`, `stage_parameter.h`, `preview_helpers.h`,
`artifact.h`, `observation_context_interface.h`, `preview_renderer.h`,
`componentframe.h`, `frame_line_util.h`, `buffered_file_io.h`.

**Acceptance criteria:**
- Inventory document lists every header with usage counts and a
  keep/wrap/remove disposition for each.
- Dispositions distinguish: (a) belongs in the SDK contract, (b) reachable
  via an existing SDK header, (c) private — plugin usage must be migrated.

### Task 4.2 — Define the allowlisted SDK contract surface

From the inventory, define the allowlist: the set of headers plugins may
include. Structure:

- All headers under `orc/sdk/include/orc/plugin/` (existing).
- A new `orc/sdk/include/orc/stage/` tree that re-homes (moves, not copies)
  the contract headers currently living in `orc/core/include/` and
  `orc/core/stages/` that stages legitimately depend on: `stage.h`/DAGStage,
  `artifact.h`, `stage_parameter.h`, `triggerable_stage.h`,
  `parameter_types.h`, `common_types.h`, `node_type.h`,
  `video_frame_representation.h`, `observation_context.h` (or a trimmed
  plugin-facing subset), preview contract headers, and the logging macro
  surface plugins are allowed to use.
- Host-only headers (`dag_executor.h`, `stage_registry.h`,
  `stage_plugin_loader.h`, `lru_cache.h`, preset/analysis internals) stay in
  `orc/core/` and are dropped from SDK umbrella includes:
  [orc_stage_runtime.h](../orc/sdk/include/orc/plugin/orc_stage_runtime.h)
  currently pulls in `dag_executor.h`, which exposes the DAG executor to all
  plugins.

Update `plugin_api_version` implications: moving headers without changing
declarations is not an API break; any trimmed types must keep source
compatibility for in-tree plugins.

**Acceptance criteria:**
- A written allowlist (in the inventory doc) naming every permitted header.
- `orc_stage_runtime.h` and `orc_stage_preview.h` include only allowlisted
  headers; no SDK header includes a header the scan will forbid.
- orc-core still builds with headers re-homed (core code updates its
  include paths to the new SDK locations).

### Task 4.3 — Migrate plugin code off private headers

Update `orc/plugins/stages/` code to include only allowlisted headers.
Expected work items from the inventory: replace direct `logging.h` usage
with the sanctioned logging surface chosen in Task 4.2, replace
`buffered_file_io.h` usage with `IStageServices` writer factories, and route
any remaining private-type usage through SDK headers. Expand `IStageServices`
first where a capability is genuinely missing (AGENTS.md §9: "expand the SDK
first rather than bypassing it").

**Acceptance criteria:**
- No file under `orc/plugins/stages/` includes a non-allowlisted header.
- All stage unit-test suites (`-L sources`, `-L transforms`, `-L sinks`,
  `-L contracts`) pass unchanged.
- Any new `IStageServices` methods have mocked unit tests and doc updates
  per the AGENTS.md §9 sync table.

---

## Phase 5 — Enforcement flip to allowlist

### Task 5.1 — Rewrite the include scan as an allowlist gate

Rewrite [check_plugin_private_includes.sh](../cmake/check_plugin_private_includes.sh)
to fail on any `#include` in plugin code that is not: an allowlisted
`<orc/plugin/...>` / `<orc/stage/...>` header, a plugin-local header
(within the same plugin directory), a C/C++ standard-library header, or an
explicitly permitted third-party header (fmt/spdlog). Keep the existing
scan of `orc/plugins/stages/` and `3rd-party-plugins/`.

**Acceptance criteria:**
- `ctest --test-dir build -L sdk` passes on the migrated tree.
- Deliberately adding `#include "dag_executor.h"` (or any orc-core private
  header) to a plugin source fails the gate.
- The gate script still runs standalone against an external plugin tree
  (documented invocation unchanged).

### Task 5.2 — Restrict SDK target include propagation

Update [orc/sdk/CMakeLists.txt](../orc/sdk/CMakeLists.txt) so plugins can no
longer resolve private headers by accident:

- Remove the `orc/core/stages` include-directory injection.
- Stop propagating orc-core's full include tree to plugins: link plugins
  against orc-core for symbols but scope include directories so only the
  SDK trees (`orc/sdk/include`) and required third-party interface paths
  (spdlog/fmt) are visible. If orc-core's PUBLIC includes cannot be narrowed
  without breaking host consumers, add a separate `orc-core-link-only`
  interface target for plugin linking.

**Acceptance criteria:**
- A plugin TU that includes a private orc-core header fails to *compile*
  (not just the scan) in-tree.
- Host targets (`orc-gui`, `orc-cli`, presenters, tests) build unchanged.
- `ctest --test-dir build --output-on-failure` fully passes.

### Task 5.3 — Update SDK docs for the new boundary

Update [docs/technical/plugin-sdk.md](../docs/technical/plugin-sdk.md)
(SDK Headers table, "What Plugins Must Not Include") and
[docs/technical/plugin-architecture.md](../docs/technical/plugin-architecture.md)
to describe the allowlist model, the new `<orc/stage/...>` header family,
and the compile-time + scan enforcement. Update the AGENTS.md §9 doc-sync
table if header locations changed.

**Acceptance criteria:**
- Docs list the complete allowlisted header set.
- AGENTS.md §9 references remain accurate.

---

## Phase 6 — ABI revision 5: descriptor toolchain tag and registration helper

All ABI-touching changes batched into one `host_abi_version` bump.

### Task 6.1 — Add a toolchain/stdlib tag to StagePluginDescriptor

Append (never reorder) fields to `StagePluginDescriptor` in
[orc_plugin_abi.h](../orc/sdk/include/orc/plugin/orc_plugin_abi.h)
identifying the build environment, e.g. `const char* toolchain_tag`
populated by an SDK-provided macro (`ORC_SDK_TOOLCHAIN_TAG`) encoding
compiler family/major version, standard library, and build configuration.
The loader rejects plugins whose tag is incompatible with the host's,
with a clear diagnostic. Bump `kStagePluginHostAbiVersion` to 5 and update
`ORC_SDK_ABI_VERSION`, the history comments, and both doc version tables
(AGENTS.md §9 sync table applies).

**Acceptance criteria:**
- Loader unit tests cover: matching tag loads; mismatched tag rejected with
  a message naming both tags; ABI-4 plugin rejected with version mismatch.
- All in-tree plugins rebuilt and loading at v5.
- Version tables updated in both docs and header history comments.

### Task 6.2 — Provide a registration helper to eliminate per-plugin boilerplate

Each of the ~21 in-tree plugins hand-rolls the same `plugin.cpp` pattern
(descriptor, entrypoints, NodeTypeInfo cross-check — see
[mask_line/plugin.cpp](../orc/plugins/stages/mask_line/plugin.cpp)). Add an
SDK helper (e.g. `ORC_DEFINE_STAGE_PLUGIN(descriptor, stage_name, StageType)`
macro or a variadic template for multi-stage plugins) in a new SDK header
that expands to both entrypoints, calls `set_services()`, registers each
stage, and reports errors through `error_message`. Keep the raw entrypoint
path supported for plugins needing custom logic.

**Acceptance criteria:**
- At least three representative plugins (one source, one transform, one
  sink) migrated to the helper with net negative line count and identical
  runtime behaviour.
- Helper documented in plugin-sdk.md; SDK Headers table updated.
- Remaining plugins migrated mechanically (may be a follow-up task in the
  same phase; all must pass their existing suites).

### Task 6.3 — Decide and document the NodeTypeInfo metadata cross-check

The per-plugin metadata mismatch check duplicates NodeTypeInfo constants
into `plugin.h`. With the helper from Task 6.2, either fold the cross-check
into the helper (single source of truth: the stage's
`get_node_type_info()`), or drop the duplicated constants entirely.

**Acceptance criteria:**
- No plugin carries hand-duplicated NodeTypeInfo constant blocks after
  migration.
- Contract tests (`-L contracts`) covering registry/node discovery parity
  still pass.

---

## Phase 7 — Out-of-tree package export

Make the documented third-party workflow real. Currently
[DecodeOrcPluginSDKConfig.cmake](../cmake/decode-orc-plugin-sdk/DecodeOrcPluginSDKConfig.cmake)
includes a `decode-orc-plugin-sdkTargets.cmake` that nothing generates.

### Task 7.1 — Export SDK and host link targets

- Add `install(TARGETS orc-plugin-sdk orc-core ... EXPORT decode-orc-plugin-sdkTargets)`
  and `install(EXPORT decode-orc-plugin-sdkTargets NAMESPACE orc:: ...)` so
  `orc::plugin-sdk` resolves from an installed prefix
  ([orc/sdk/CMakeLists.txt](../orc/sdk/CMakeLists.txt),
  [orc/core/CMakeLists.txt](../orc/core/CMakeLists.txt)).
- Give every SDK include directory an `INSTALL_INTERFACE` counterpart; the
  allowlisted `<orc/stage/...>` tree from Phase 4 must install alongside
  `<orc/plugin/...>`.
- Install any transitively required third-party interface config
  (`find_dependency` entries in the Config file must match reality).

**Acceptance criteria:**
- From a clean prefix: `cmake --install build` then building a minimal
  external plugin (outside the repo, using only
  `find_package(decode-orc-plugin-sdk REQUIRED)` +
  `orc_add_stage_plugin()`) succeeds.
- The built external plugin loads into `orc-cli` via
  `ORC_STAGE_PLUGIN_PATHS`.

### Task 7.2 — Add a CI job validating the installed package

Extend `build-and-test.yml` with a step that installs the SDK to a scratch
prefix, configures and builds a minimal fixture plugin against it, and runs
the installed enforcement-gate scripts against the fixture tree.

**Acceptance criteria:**
- CI fails if the exported package regresses (missing target, missing
  header, broken helper macro).
- Fixture plugin source lives in-repo (e.g. `orc-tests/` fixture directory)
  and is excluded from the in-tree plugin scans.

### Task 7.3 — Align skeleton and docs with the working flow

Update plugin-sdk.md Quick Start / CMake Integration for the verified flow,
and reconcile the `orc-plugin_skeleton` repository expectations (descriptor
fields, entrypoint signature, helper macro from Task 6.2). Note in the PR
what could not be validated locally per AGENTS.md §11.

**Acceptance criteria:**
- Docs describe only verified steps.
- Skeleton repo divergences are listed as follow-up issues if the repo
  cannot be updated in the same change.

---

## Phase 8 — Registry trust and download integrity

### Task 8.1 — Enforce trust_state before loading

`trust_state` is parsed and persisted
([stage_plugin_registry.cpp](../orc/core/stage_plugin_registry.cpp)) but
never consulted. Define the trust model: registry entries with
`trust_state: untrusted` and a non-core origin are not `dlopen`ed until the
user marks them trusted (GUI/CLI affordance via presenters — MVP boundaries
apply). Core-plugin default search paths remain trusted implicitly.

**Acceptance criteria:**
- Unit tests (mocked registry) cover: untrusted entry skipped with a
  diagnostic; trusted entry loads; core search-path plugins unaffected.
- CLI can list and change trust state; GUI exposure routed through a
  presenter.
- plugin-architecture.md Plugin Registry table documents the enforced
  semantics.

### Task 8.2 — Verify checksums for downloaded artifacts

Add a `sha256` field to the registry YAML schema for
`github_release_asset` entries. The downloader verifies the digest before
caching/loading; mismatch quarantines the file and emits an error
diagnostic. Absent checksum on an untrusted entry is a warning (or error —
decide and document).

**Acceptance criteria:**
- Unit tests with mocked download/file interfaces cover match, mismatch,
  and absent-checksum paths (no real network or filesystem in unit tests,
  per AGENTS.md §4.2).
- Registry schema table in plugin-architecture.md updated (AGENTS.md §9
  sync table row for registry YAML schema).

### Task 8.3 — Restore honest "signed registry" positioning

Revisit the distribution claims in both docs: describe exactly what is
verified (ABI version, API version, toolchain tag, checksum, trust state)
and what is not (no code signing). If code signing remains a goal, record
it as future work in the docs rather than implying it exists.

**Acceptance criteria:**
- Security-relevant claims in docs are each backed by a code path.
- `ctest --test-dir build -L sdk` and the full suite pass at the end of the
  phase.
