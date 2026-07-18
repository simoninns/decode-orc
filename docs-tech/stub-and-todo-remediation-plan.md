# Stub and TODO Remediation Plan

> **Status:** remediation plan produced from a code audit for stubs, TODOs,
> and non-functional-looking code paths.
> Companion documents:
> [Observation Service Implementation Plan](plugin-observation-service-plan.md),
> [Observation across the plugin boundary](plugin-observation-abi.md).

## Scope

The audit found **no non-functional stages**: all 23 stage plugins have
fully implemented processing paths, and sink `execute()` returning `{}` is
the documented sink contract (work happens in `trigger()`). The real issues
are one correctness defect, an unimplemented parameter option, stale "stub"
documentation on code that is actually complete, and a large body of
unreferenced functions and whole files.

The work is grouped into four phases by the kind of change:

- **Phase 1 — Correctness and incomplete features:** behaviour changes.
- **Phase 2 — Documentation and comment accuracy:** comment/tracking fixes,
  no behaviour change.
- **Phase 3 — Dead-code pruning:** remove unreferenced functions.
- **Phase 4 — Orphaned files and repository hygiene:** remove whole unused
  files; decide on unreferenced test media.

Each phase is self-contained. Phases 3 and 4 are large-surface cleanups;
every deletion in them must be individually confirmed against the verification
steps given, because the automated scans that produced the candidate lists
carry known false positives.

---

## Phase 1 — Correctness and incomplete features

Behaviour-changing fixes: one validation defect and one parameter option that
silently does nothing.

1. **Implement real source/sink detection in `validateProject()`.**
   The per-node loop at
   [`project_presenter.cpp:1445-1451`](../orc/presenters/src/project_presenter.cpp#L1445-L1451)
   unconditionally sets `has_source = true; has_sink = true`, so validation
   degrades to "has at least one node" and cannot detect a source-only or
   sink-only graph. Classify each node by its stage category and set the flags
   accordingly.
   - *Acceptance:* a source-only project fails validation; a project with at
     least one source and one sink passes. Presenter unit test with mocked
     nodes (no filesystem).

2. **Fix `getValidationErrors()` to match.**
   Apply the same classification at
   [`project_presenter.cpp:1461-1476`](../orc/presenters/src/project_presenter.cpp#L1461-L1476)
   so the emitted "no source nodes" / "no sink nodes" strings reflect real
   graph content.
   - *Acceptance:* a sink-only project reports exactly "Project has no source
     nodes"; a valid project reports no errors. Unit test with mocked nodes.

3. **Audit callers of the two validation helpers.**
   Confirm the stricter result does not break an existing GUI/CLI flow (e.g. an
   in-progress graph that legitimately lacks a sink yet).
   - *Acceptance:* callers audited; any needing the permissive check are
     adjusted or documented in the PR.

4. **Resolve `frame_map` `pad_strategy = "nearest"`.**
   The `if (pad_strategy == "nearest")` block at
   [`frame_map_stage.cpp:499-504`](../orc/plugins/stages/frame_map/frame_map_stage.cpp#L499-L504)
   is an empty comment; padding frames always render black, so "nearest" is
   silently identical to "black". Either:
   - **(a)** Implement it: have `get_frame()` for a padding descriptor return
     the nearest real source frame's samples while still reporting
     `is_padding_frame`, or
   - **(b)** Remove the "nearest" option from the parameter descriptor.
   - *Acceptance:* if (a), a stage unit test asserts a "nearest"-padded frame
     carries the neighbour's samples; if (b), the option is gone from the
     parameter enum and the dead branch removed. Either way `instructions.md`
     is updated in the same change (per AGENTS.md §9.1).

---

## Phase 2 — Documentation and comment accuracy

No behaviour change — only bring comments and TODOs in line with reality.

1. **Correct the `disc_mapper` analyzer header docstring.**
   The class comment at
   [`disc_mapper_analyzer.h:47-52`](../orc/core/analysis/disc_mapper/disc_mapper_analyzer.h#L47-L52)
   still reads "Stubbed analyzer. Currently returns a failure result...", but
   the analyzer is a fully implemented six-stage VBI mapping pipeline returning
   `success = true`. Rewrite it to describe the actual behaviour.
   - *Acceptance:* no "stub"/"disabled" wording remains; comment matches the
     implementation.

2. **Remove the stale `disc_mapper` fallback string.**
   "Disc mapper analysis is currently stubbed out" at
   [`disc_mapper_analysis.cpp:197`](../orc/core/analysis/disc_mapper/disc_mapper_analysis.cpp#L197)
   is reached only when `decision.success == false` with an empty rationale — a
   state the analyzer no longer produces. Replace with an accurate generic
   failure message.
   - *Acceptance:* no "stubbed out" string remains; the failure branch still
     produces a meaningful summary when `rationale` is empty.

3. **Fold the `ld_sink` observation TODO into the observation-service work.**
   `LDSinkStage::execute()` accepts but ignores the observation context
   (`(void)observation_context; // TODO(sdi): Use for observations`,
   [`ld_sink_stage.cpp:47`](../orc/plugins/stages/ld_sink/ld_sink_stage.cpp#L47)).
   Record it in the
   [Observation Service Implementation Plan](plugin-observation-service-plan.md)
   as a consumer to wire once the host-owned service lands, and remove the bare
   TODO from the stage.
   - *Acceptance:* no orphaned TODO remains; the item is tracked in the
     observation-service plan.

---

## Phase 3 — Dead-code pruning

Remove unreferenced functions. Candidates come from a whole-program `cppcheck
--enable=unusedFunction` scan (244 raw hits), filtered by real reference count
(keep only names with ≤2 total in-tree references and zero test references) to
**102 high-confidence dead functions**. The raw scan is dominated by false
positives — Qt `moc` signal/slot connections, virtual overrides invoked
through base pointers, and macro-registered entry points all look "unused" to
cppcheck — so **every candidate must be individually confirmed** before
deletion.

**Reproducing the scan:**

```bash
nix shell nixpkgs#cppcheck --command cppcheck \
  --enable=unusedFunction --cppcheck-build-dir=<builddir> \
  --quiet --language=c++ --std=c++20 -j 4 \
  --suppress=missingIncludeSystem --suppress=normalCheckLevelMaxBranches \
  -I orc/sdk/include -I orc/core/include -I orc/presenters/include \
  -I orc/common/include -I orc/view-types -I orc/gui \
  orc/core orc/presenters orc/cli orc/gui orc/view-types orc/plugins
```

Including `orc/gui` is essential — omitting it flags every GUI-consumed
presenter method as a false positive. `--cppcheck-build-dir` is required or
`-j` silently disables the `unusedFunction` check.

**Per-function verification (apply before deleting any candidate):**
`grep -rn "\bNAME\b" orc/ orc-tests/`; confirm the only matches are the
declaration and definition; confirm it is not a `virtual`/`override`, not a Qt
slot referenced in a `.ui` or `connect(...)`, and not registered via a macro.
Rebuild and run the label-appropriate `ctest` after each task.

1. **Delete the known-dead targeted functions.** Confirmed dead by direct
   inspection during the audit:
   - `SourceAlignStage::get_frame_number_from_vbi()` (returns `-1`
     unconditionally) and its sole, itself-unreferenced caller
     `find_alignment_offsets()`
     ([`source_align_stage.cpp:401-406`](../orc/plugins/stages/source_align/source_align_stage.cpp#L401-L406));
     the real VBI auto-detection lives in
     [`source_alignment_analysis.cpp`](../orc/core/analysis/source_alignment/source_alignment_analysis.cpp).
   - `RenderPresenter::getObservations()` (always returns `"{}"`, zero callers)
     with its declaration and the presenter-local
     `ObservationData{is_valid, json_data}` struct
     ([`render_presenter.h:60-63`](../orc/presenters/include/render_presenter.h#L60-L63),
     [`render_presenter.cpp:1002-1004`](../orc/presenters/src/render_presenter.cpp#L1002-L1004)).
     Leave the structured SDK preview `ObservationData`
     ([`orc_rendering.h:182`](../orc/sdk/include/orc/stage/preview/orc_rendering.h#L182))
     in place. This path is unrelated to on-disk observation output — the
     persistence sinks serialize observations themselves.
   - `RenderPresenter::getCacheStats()` (constant `"Cache: active"`,
     [`render_presenter.cpp:1021`](../orc/presenters/src/render_presenter.cpp#L1021)).
   - *Acceptance:* all removed with their declarations; build green, no
     dangling references.

2. **Prune core / common internal dead code (~20 functions).**
   Non-virtual, non-SDK helpers with no callers, e.g. `load_dag_from_yaml` /
   `save_dag_to_yaml` ([`dag_serialization.cpp`](../orc/core/dag_serialization.cpp)),
   `get_renderable_nodes` / `get_dag` / `get_dag_version` / `cache_size`
   ([`dag_frame_renderer`](../orc/core/dag_frame_renderer.cpp)),
   `get_suggested_view_node` / `get_preview_item_label` /
   `get_preview_item_display_info` / `get_equivalent_index`
   ([`preview_renderer.cpp`](../orc/core/preview_renderer.cpp)),
   `clear_node` / `get_frame_count` ([`observation_cache.cpp`](../orc/core/observation_cache.cpp)),
   `get_default_transform_stage` / `get_plugin_registry_path`
   ([`stage_registry.cpp`](../orc/core/stage_registry.cpp)),
   `serialize_plugin_index_yaml`, `set_root_inputs`, `apply_decisions`,
   `toolsForSource`, `buffered_bytes`, `amplitude_input_precision`.
   - *Acceptance:* each confirmed unreferenced and removed; core builds and
     `ctest` passes.

3. **Prune plugin-stage internal dead code (~24 functions).**
   e.g. `writeOutputFile`
   ([`video_sink_stage.cpp:2267`](../orc/plugins/stages/sinks/common/video_sink_stage.cpp#L2267)),
   `min_input_count` / `max_input_count`
   ([`stacker_stage.h`](../orc/plugins/stages/stacker/stacker_stage.h)),
   `interpolate_extra_sample`, the `tbc_reader` size getters
   (`get_field_count` / `get_field_length` / `get_line_length`), the
   `tbc_metadata_reader` helpers (`execute_query`, `get_int64`), and the
   EFM-lib helpers (`countErrorsLeft` / `countErrorsRight`, `eightToFourteen`,
   `f3FrameTypeAsString`, `toBcd`, `isPause`, `pFlag`, `leadinSections`,
   `tocDiscType`, `trackStartTimes` / `trackEndTimes`, `missingLeadingSectors`).
   - *Note:* the EFM-lib and `dec_*correction` files are ported from
     `ld-decode`; the maintainer may keep unused API for port fidelity. Decide
     keep-vs-prune per file, not per function.
   - *Acceptance:* per-file decision recorded; removed functions confirmed
     unreferenced; plugin builds and stage tests pass.

4. **Prune GUI dead code (~11 functions).**
   Widget/dialog accessors with no caller and no test reference, e.g.
   `timingWidget` / `firstFieldHeight` / `secondFieldHeight` /
   `currentFieldIndex2` ([`frametimingdialog.h`](../orc/gui/frametimingdialog.h)),
   `scrollBar` ([`frametimingwidget.h`](../orc/gui/frametimingwidget.h)),
   `monitorWidget` / `yOnlyMode` (waveform monitor), `viewGeometry` /
   `viewportSize`, `invalidateConfigurationStatus`, `requestFrameLineNavigation`.
   - *Extra care:* confirm none are Qt slots wired via `connect()` or a `.ui`
     file, and none are test-only accessors.
   - *Acceptance:* each confirmed non-slot and unreferenced; GUI builds and
     `-L gui` tests pass.

5. **Prune remaining presenter dead code (~15 functions).**
   Confirmed-dead presenter methods outside the `dropout_presenter` cluster
   (task 6), e.g. `clearCache`, `invalidateRenderCachesForNode`,
   `exportPreviewViewData`, `getFieldQualityMetrics` / `getFrameQualityMetrics`,
   `getLineSamples`, `getOutputCount`, `isTriggerActive`
   ([`render_presenter.cpp`](../orc/presenters/src/render_presenter.cpp)),
   `getAvailableTools` / `isAnalysisRunning`
   ([`analysis_presenter.cpp`](../orc/presenters/src/analysis_presenter.cpp)),
   `reportProgress`, `fieldToFrameCoordinates`, `getVbiForFrame`.
   - *Caution:* presenter methods are the most likely to be reached via a
     virtual `IRenderPresenter`/`IAnalysisPresenter` interface — verify the
     interface declaration is also unused, or the override will be re-flagged
     and the vtable slot kept.
   - *Acceptance:* each confirmed unreferenced through the concrete class and
     any interface; presenter and GUI tests pass.

6. **Investigate the two abandoned-surface clusters.** These are not stray
   functions but near-complete public APIs, so treat each as a
   remove-the-whole-thing decision rather than piecemeal deletion:
   - **`DropoutPresenter` (~14 functions)** — the scan flags nearly its entire
     surface (`detectDropouts`, `getCorrections`, `autoDecideDropouts`,
     `exportCorrections`, …) as unused
     ([`dropout_presenter.cpp`](../orc/presenters/src/dropout_presenter.cpp)),
     consistent with the dropout-editor rework that moved to an
     `IRenderPresenter`/adapter seam. Determine whether the class is dead in
     its entirety and, if so, remove it with its tests and registration.
   - **`LdDecodeMetaData` getter/setter surface (~14 functions)** — a large
     unreferenced API in
     [`lddecodemetadata.cpp`](../orc/plugins/stages/tbc_source/lddecodemetadata.cpp)
     (`appendField`, `updateField`, `setNumberOfFields`, `getFirstFieldNumber`,
     `convertClvTimecodeToFrameNumber`, …), consistent with the TBC-metadata
     refactor that left this ported class largely superseded. Decide whether to
     trim to the live surface or keep as a faithful `ld-decode` port.
   - *Acceptance:* each cluster confirmed live (with the call sites documented)
     or removed/trimmed wholesale; build green and the relevant tests pass.

7. **Leave SDK public headers alone unless deliberately pruning the SDK.**
   Four flagged functions live in `orc/sdk/include/` and are part of the
   external-plugin API, so "unused in-tree" does not mean deletable:
   `add_decision` / `get_all`
   ([`dropout_decision.h`](../orc/sdk/include/orc/stage/dropout/dropout_decision.h)),
   `observation_type_to_string`
   ([`observation_schema.h`](../orc/sdk/include/orc/stage/observation/observation_schema.h)),
   `get_row` ([`eia608_decoder.h`](../orc/sdk/include/orc/support/eia608_decoder.h)).
   Removing any is an ABI/API change and must follow the SDK header process in
   AGENTS.md §9 (manifest + docs regen), not this cleanup.
   - *Acceptance:* left in place, or removed only via the SDK-header process
     with `sdk_headers.yaml` and docs updated and the `-L sdk` gates green.

---

## Phase 4 — Orphaned files and repository hygiene

Remove whole files that no build target compiles and no source includes, then
settle the one repository-hygiene finding. Detection method: non-plugin `.cpp`
(plugin dirs use `file(GLOB_RECURSE …)`, so no plugin `.cpp` can be orphaned)
cross-checked against every `CMakeLists.txt`/`.cmake` and against `#include`
usage; every tracked `.h`/`.hpp` cross-checked against `#include` of its
basename across `orc/` and `orc-tests/`.

1. **Delete the orphaned source file.**
   [`orc/gui/linescopedialog.cpp`](../orc/gui/linescopedialog.cpp) is absent
   from the explicit `ORC_GUI_LIB_SOURCES` list (never compiled), has no
   companion header, and the only mention of it is a comment in
   [`previewdialog.cpp:445`](../orc/gui/previewdialog.cpp#L445) noting it was
   replaced by the frame scope dialog.
   - *Acceptance:* file removed; `grep -ri linescope orc/` returns only
     unrelated comments; GUI builds and `-L gui` tests pass.

2. **Delete the orphaned headers (never `#include`d anywhere).**
   - [`orc/view-types/orc_cvbs_source_parameters.h`](../orc/view-types/orc_cvbs_source_parameters.h)
     — `cvbs_source_parameters` appears in no other file.
   - [`orc/gui/amplitude_unit_helpers.h`](../orc/gui/amplitude_unit_helpers.h)
     — GUI code uses `amplitude_conversion.h` directly and never includes this
     wrapper.
   - [`orc-tests/core/unit/factories_interface_mock.h`](../orc-tests/core/unit/factories_interface_mock.h),
     [`orc-tests/core/unit/stages/stage_factories_interface_mock.h`](../orc-tests/core/unit/stages/stage_factories_interface_mock.h),
     [`orc-tests/core/unit/stages/daphne_vbi_sink/daphne_vbi_writer_util_interface_mock.h`](../orc-tests/core/unit/stages/daphne_vbi_sink/daphne_vbi_writer_util_interface_mock.h)
     — gmock mock headers with no test includes (superseded by inline mocks).
   - *Verification per file:* confirm zero `#include` of the basename across
     `orc/` and `orc-tests/`. Neither production header is a public SDK header,
     so removal needs no manifest change.
   - *Acceptance:* all five removed; full build (`-DBUILD_UNIT_TESTS=ON
     -DBUILD_GUI_TESTS=ON`) and `ctest` pass.

3. **Repository hygiene — decision on unreferenced test media.** No build
   artifacts, generated files, or editor/OS cruft are tracked: `.gitignore`
   already covers object files, libraries, `build/`, `*.tbc`, `*.lds`, etc.
   The only finding is that everything under
   [`test-projects/`](../test-projects/) — the ten `.orcprj` example projects
   and the `sw-tpg21-references/` set (`.composite` + `.meta`) — is referenced
   by **zero** in-tree code, tests, or docs. The two `.composite` binaries
   total **~7.3 MB** of binary CVBS media (`75CB&Red-PAL.composite` 5.5 MB,
   `75CB-NTSC.composite` 1.9 MB) committed to git history. These are
   manual/interactive reference projects, not automated fixtures. Decide one of:
   - **(a)** Keep as intentional example projects, documented in a
     `test-projects/README.md`; or
   - **(b)** Move the large binary media out of the repo (external fetch /
     release asset / Git LFS), keeping the small `.orcprj`/`.meta` text files;
     or
   - **(c)** Remove them if they are stale developer scratch projects.
   - *Note:* removal only stops growth going forward; the blobs remain in
     history unless history is rewritten (out of scope — flag separately if
     wanted).
   - *Acceptance:* a decision is recorded; if (b)/(c), the working tree no
     longer tracks the affected files and any doc/tooling naming those paths is
     updated.

---

## Non-issues confirmed by the audit

Recorded so they are not re-flagged in future sweeps:

- **All sink `execute()` returning `{}`** — documented sink contract; work
  happens in `trigger()`.
- **`OrcGraphModel::loadNode` throwing "not implemented"**
  ([`orcgraphmodel.cpp:490`](../orc/gui/orcgraphmodel.cpp#L490)) — intentional
  MVP guard directing callers to the presenter, not a gap.
- **`ac3rf_sink` framing `FIXME`**
  ([`Ac3InputFraming.cpp:82`](../orc/plugins/stages/ac3rf_sink/ac3rf-lib/ac3/Ac3InputFraming.cpp#L82))
  — an inline "auto-sync could be improved" note inside a working ported loop;
  not a dead path.
- **Single-source pass-through in `stacker` / `source_align` / `audio_align`;
  empty-spec pass-through in `mask_line`** — by design.
