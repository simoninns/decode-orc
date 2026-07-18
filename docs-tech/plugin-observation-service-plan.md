# Observation Service Implementation Plan

> **Status:** implementation plan for the refactor described in
> [Observation across the plugin boundary](plugin-observation-abi.md).
> Companion documents: [Plugin SDK / ABI Review](plugin-sdk-abi-review.md),
> [Plugin SDK Developer Guide](../docs/technical/plugin-sdk.md),
> [Plugin Architecture](../docs/technical/plugin-architecture.md).

## Scope

Replace plugin-linked observer classes (delivered via the `orc-sdk-support`
static archive) with a host-owned `IObservationService` reached through the
versioned `OrcPluginServices` table, following the existing
`IStageServices` / `render_colour_preview` precedent. End state: observer
implementations exist once, in the host; plugins request observations by
stable string identifier; the concrete `*_observer.h` headers leave the
plugin-facing SDK.

Affected observers (all currently compiled into `orc-sdk-support`,
`orc/sdk/CMakeLists.txt:63`): `BiphaseObserver`, `BlackPSNRObserver`,
`BurstLevelObserver`, `ClosedCaptionObserver`, `ColourFramePhaseObserver`,
`FieldQualityObserver`, `FmCodeObserver`, `WhiteFlagObserver`,
`WhiteSNRObserver`.

Consumers that construct observers today:

- `orc/plugins/stages/snr_analysis_sink` — `WhiteSNRObserver`, `BlackPSNRObserver` (persistent members)
- `orc/plugins/stages/burst_level_analysis_sink` — `BurstLevelObserver` (persistent member)
- `orc/plugins/stages/cc_sink` — `ClosedCaptionObserver` (persistent member; stateful across fields)
- `orc/plugins/stages/sinks/common/video_sink_stage.cpp` — `ClosedCaptionObserver`, `BiphaseObserver` (constructed on demand)
- `orc/core/dag_frame_renderer.cpp` — all nine (host-internal standard observer pass)

`ld_sink`, `daphne_vbi_sink`, and `dropout_analysis_sink` only read results
from `IObservationContext` and are unaffected.

---

## Phase 1 — Host-owned observation service (additive, ABI bump)

1. **Finalise the service contract.** Add
   `orc/sdk/include/orc/stage/observation/observation_service_interface.h`
   defining:
   - `ObserverInfo` — stable string id (reuse the existing namespace strings:
     `white_snr`, `black_psnr`, `burst_level`, `closed_caption`, `biphase`,
     `colour_frame_phase`, `disc_quality` for `FieldQualityObserver`,
     `fm_code`, `white_flag`), observer version, and the provided
     `ObservationKey` list.
   - `IObservationService` — `available_observers()`, and observer execution
     writing into a caller-supplied `IObservationContext&`.
   - **Stateful observers:** audit each observer for cross-frame state
     (`ClosedCaptionObserver` pairs fields across calls; `cc_sink` holds it as
     a persistent member). The contract must support this: expose a host-owned
     session object (`create_observer(observer_id)` returning an abstract
     `IObserverHandle` with `process_frame(representation, frame_id, context)`),
     so per-caller state lives in a host-allocated instance rather than a
     global. A convenience one-shot `run_observer(...)` may wrap it.
   - **Configuration:** the service exposes none. The observer configuration
     surface is dead code — no observer overrides
     `get_configuration_schema()` (all return the empty base default) and no
     caller invokes `set_configuration()` — so `create_observer(observer_id)`
     takes no configuration parameter and `ObserverInfo` carries no schema.
     If a configurable observer ever materialises, append a config-taking
     overload to `IObservationService` (append-only vtable growth, the
     accepted `IStageServices` pattern, recorded as a `contract-vtable`
     history entry).
   - Document thread-safety guarantees on every interface method (§5.3.3 of
     [AGENTS.md](../AGENTS.md)).
   *Acceptance:* header compiles standalone; registered in
   `orc/sdk/sdk_headers.yaml` (tier `stage`, domain `observation`,
   `since_abi: 9`); `tools/gen_sdk_header_allowlist.sh` and
   `tools/gen_sdk_header_docs.sh` regenerated; `SdkHeaderManifestSync`,
   `SdkHeaderDocsSync`, `SdkHeaderHygieneScan` pass.

2. **Implement the host service and observer registry.** Add a host-internal
   implementation in `orc/core/` (e.g. `core_observation_service.{h,cpp}`)
   with a static id → factory table covering all nine observers. The registry
   is the single source of truth for observer identity; unknown ids fail
   cleanly (return `false` / null handle, never throw across the boundary).
   *Acceptance:* unit tests under `orc-tests/core/unit/observers/` (label
   `unit`) cover id lookup, unknown-id failure, `available_observers()`
   completeness, and result delivery through the existing
   `MockObservationContext`
   (`orc-tests/core/unit/include/observation_context_interface_mock.h`); no
   filesystem or clock access per `TESTING.md`.

3. **Expose the service on `OrcPluginServices`.** Append
   `IObservationService* observation_service;` after `stage_services`
   (`orc/sdk/include/orc/abi/orc_plugin_services.h:113`), guarded by
   `services_size`; add a `plugin::get_observation_service()` accessor with an
   `offsetof` guard mirroring `plugin::get_stage_services()`
   (`orc_plugin_services.h:136`); wire the host implementation in
   `StagePluginLoader::load_plugin()` next to the existing
   `CoreStageServicesAdapter` assignment
   (`orc/core/stage_plugin_loader.cpp:433`).
   *Acceptance:* `kStagePluginHostAbiVersion` and `ORC_SDK_ABI_VERSION`
   bumped 8 → 9 in `orc/sdk/include/orc/abi/orc_plugin_abi.h`; new
   `orc/sdk/abi_history.yaml` entry (cause `descriptor-append`, contract
   `orc/abi/orc_plugin_services.h`); version-history docs regenerated via
   `tools/gen_abi_history_docs.sh`; `tools/check_abi_bump.sh`,
   `AbiHistorySync`, `AbiVersionDocsSync` pass; accessor returns `nullptr`
   for a simulated older `services_size` (unit-tested).

4. **Add a contract test tying service identity to observer output.** Assert
   that every `ObserverInfo` returned by `available_observers()` matches the
   corresponding observer's `get_provided_observations()` keys and namespace
   string, so the id scheme cannot drift from the schema registered by
   `dag_executor.cpp:167`.
   *Acceptance:* test registered with labels `unit` and `contracts`; full
   `ctest --test-dir build -L sdk` and `-L unit` lanes green.

---

## Phase 2 — Port bundled consumers to the service

1. **Establish the injection seam and mock.** Add
   `orc-tests/core/unit/include/observation_service_interface_mock.h`
   (gMock for `IObservationService` / `IObserverHandle`). Bundled sink deps
   classes receive the service via constructor injection (interface pointer or
   reference), with the plugin entry point wiring
   `plugin::get_observation_service()`; a null service degrades gracefully
   (observation skipped, warning logged) since older hosts return `nullptr`.
   *Acceptance:* mock header in place; injection pattern documented in the
   deps-interface headers of the first ported stage.

2. **Port `snr_analysis_sink` and `burst_level_analysis_sink`.** Replace the
   observer members in `snr_analysis_sink_deps.h:69` and
   `burst_level_analysis_sink_deps.h:69` with service calls by id
   (`white_snr`, `black_psnr`, `burst_level`).
   *Acceptance:* stage unit tests updated to drive the mock service; existing
   result-key assertions (`white_snr`, `median_burst_10bit`, …) unchanged;
   `ctest -L sinks` green; `instructions.md` for each stage reviewed and
   updated only if user-visible behaviour changed.

3. **Port `cc_sink`.** Replace the persistent `ClosedCaptionObserver` member
   (`cc_sink_stage_deps.h:83`) with a host-owned observer session created via
   the service, preserving cross-frame field-pairing behaviour.
   *Acceptance:* `cc_sink_stage_test.cpp` updated; a test verifies the
   session is created once and reused across frames; `ctest -L sinks` green.

4. **Port shared `video_sink` code.** Replace the on-demand
   `ClosedCaptionObserver` / `BiphaseObserver` construction in
   `orc/plugins/stages/sinks/common/video_sink_stage.cpp:1086` and `:1142`
   with service calls; update the `BiphaseObserver` reference in the
   parameter help string at `:538` to observer-id terminology.
   *Acceptance:* `video_sink_stage_test.cpp` / safety test updated;
   `ctest -L sinks` green; `PluginPrivateIncludeScan` confirms no
   `*_observer.h` includes remain under `orc/plugins/stages/`.

5. **Point the host's standard observer pass at the registry.** Refactor
   `orc/core/dag_frame_renderer.cpp:234-259` to enumerate the Phase 1
   registry instead of constructing the nine classes directly, so host and
   plugins share one observer inventory.
   *Acceptance:* frame-render observer coverage unchanged (existing unit
   tests green); adding an observer to the registry requires no
   `dag_frame_renderer.cpp` edit.

---

## Phase 3 — Deprecate the plugin-facing observer classes

1. **Mark the concrete observer headers deprecated.** Apply `[[deprecated]]`
   (with a message naming the replacement id) to the nine classes in
   `orc/sdk/include/orc/stage/observation/*_observer.h`; set
   `deprecated: true` on their entries in `orc/sdk/sdk_headers.yaml`
   (mirroring the existing `orc/stage/observers/` shim precedent, manifest
   lines 356-391); keep them compiling in `orc-sdk-support` for one release
   so out-of-tree plugins can migrate.
   *Acceptance:* deprecation warnings fire on include/instantiation; manifest,
   allowlist, and generated docs regenerated; all `sdk`-label gates pass;
   in-tree build remains warning-clean (no in-tree code includes the
   deprecated headers after Phase 2).

2. **Update SDK documentation.** Document `IObservationService` in the Host
   services section of `docs/technical/plugin-sdk.md` per the sync table in
   [AGENTS.md](../AGENTS.md) §9; add a migration note (class → observer id
   mapping table) for third-party authors; update the compatibility sections
   in `plugin-architecture.md` for the ABI 9 services-table extension.
   *Acceptance:* docs mention the deprecation window and target removal
   release; `SdkHeaderDocsSync` / `AbiVersionDocsSync` pass.

3. **Update the design document status.** Change the status banner of
   [plugin-observation-abi.md](plugin-observation-abi.md) from "design
   proposal" to adopted, referencing this plan.
   *Acceptance:* both documents cross-link; no stale "not a committed work
   item" wording remains.

---

## Phase 4 — Remove static-lib delivery, host-internalise observers

1. **Move observer implementations into the host.** Relocate the observer
   sources (`observer.cpp`, `observer_config.{h,cpp}`, and the nine
   `*_observer.cpp`) plus the concrete class headers from `orc/sdk/` into
   `orc/core/` as host-internal code; remove them from the `orc-sdk-support`
   source list (`orc/sdk/CMakeLists.txt:64-89`). `observation_schema.h`,
   `observation_context_interface.h`, `observation_context.h`, and
   `observation_service_interface.h` remain SDK headers (still used by
   `DAGStage::get_provided_observations()` and the context contract). The
   abstract `Observer` base follows the implementations host-internal:
   after Phase 2 no SDK type references it
   (`DAGStage::get_provided_observations()` returns `ObservationKey` values
   from `observation_schema.h`), and keeping a subclassable stage-tier
   vtable that plugins cannot register with the host would be a trap and a
   standing ABI-bump liability. `ConfidenceLevel` /
   `confidence_level_to_string` (currently in `observer.h`, no users outside
   the SDK) move with the implementations. Delete the unused configuration
   machinery outright: `Observer::set_configuration()`,
   `get_configuration_schema()`, and `ObserverConfiguration`
   (`orc/sdk/src/observer_config.{h,cpp}`). If plugin-provided observers are
   ever required, design a registration capability on the observation
   service instead of resurrecting the linkable base class (AGENTS.md §9:
   expand the SDK first).
   *Acceptance:* `orc-sdk-support` contains no observer object code; host
   build and all observer unit tests (now linking `orc-core`) green; no
   duplicate-symbol or ODR issues in a full build.

2. **Retire the public observer headers.** Remove the nine `*_observer.h`
   entries, the deprecated `orc/stage/observers/` shim tree, and the flat
   pre-tier `orc/stage/observation_*.h` shims from `orc/sdk/sdk_headers.yaml`;
   regenerate `cmake/sdk_header_allowlist.txt` and the `plugin-sdk.md` header
   tables; record the removal in `orc/sdk/abi_history.yaml` with a version
   bump (source-breaking for any plugin still on the classes).
   *Acceptance:* `tools/check_abi_bump.sh` and all four sync gates
   (`SdkHeaderManifestSync`, `SdkHeaderDocsSync`, `AbiHistorySync`,
   `AbiVersionDocsSync`) pass; `PluginPrivateIncludeScan` rejects any
   `*_observer.h` include from plugin code.

3. **Delete deprecation shims and dead wiring.** Remove the Phase 3
   `[[deprecated]]` shims and any transitional forwarding left in
   `orc/sdk/src/`; confirm `orc-plugin-sdk` (INTERFACE) and `orc-core` link
   lines still express the reduced `orc-sdk-support` correctly
   (`orc/sdk/CMakeLists.txt:109`, `orc/core/CMakeLists.txt:132`).
   *Acceptance:* grep for observer class names under `orc/sdk/` returns only
   the service interface; clean configure from scratch succeeds.

4. **Full validation sweep.** Run the complete gate set: full build,
   `ctest --test-dir build --output-on-failure`,
   `ctest -L sdk`, `ctest -R MVPArchitectureCheck`, and
   `ctest -R StagePluginLoader`.
   *Acceptance:* all green on the Nix/Linux flow; PR notes any
   platform-specific validation not performed locally per
   [AGENTS.md](../AGENTS.md) §11.
