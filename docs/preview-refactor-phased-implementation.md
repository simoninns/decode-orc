# Preview Dialogue Refactor — Phased Implementation Plan

## Purpose

This document converts the architectural goals in `docs/preview-refactor-plan.md` into an implementation roadmap with bounded phases, clear handoff points, and testable completion criteria.

The plan targets **7 phases** to keep delivery manageable while still reducing integration risk.

Delivery model: this roadmap is implemented **phase-by-phase on one feature branch and merged as a single focused PR after Phase 7 completes**. Phases are execution checkpoints, not independent merge units.

## Non-Negotiable Constraints

- Preserve MVP layering (`core` -> `presenters` -> `gui`/`cli`) and keep Qt types out of `core`.
- Keep existing user-visible behavior working unless a phase explicitly introduces a new behavior.
- Land each phase with unit/integration coverage matching existing project standards.
- Avoid touching chroma decode algorithm internals; focus on contracts, carriers, and plumbing.
- Keep FFmpeg container/codec output policy out of scope for this plan.
- Do not keep long-lived backward-compatibility shims once a new phase contract is in place; migrate directly within the branch.

## Phase Overview

| Phase | Focus | Main Outcome |
|---|---|---|
| 1 | Contracts and taxonomy foundation | Explicit preview data types, coordinate model, and capability contracts exist in shared/core layers |
| 2 | Carrier split and render pipeline pivot | Signal-domain VFR and colour-domain decoder output carriers replace stage-side baked RGB contract |
| 3 | Core preview view registry | Views become registry-driven by data type and request/export contract |
| 4 | GUI integration and ownership cleanup | `PreviewDialog` becomes the single owner/coordinator for supplementary views |
| 5 | CLI/core interface alignment | CLI remains non-preview and uses the same core orchestration interfaces as the GUI-facing path |
| 6 | Live parameter tweaking pipeline | Display-phase and decode-phase preview tweaks work without full DAG rebuild |
| 7 | Migration completion and hardening | Legacy paths removed; architecture/tests/docs finalized |

---

## Phase 1 — Contracts and Taxonomy Foundation

### Scope

Define the shared language and contracts without changing rendering behavior yet.

### Implementation Work

- Add explicit taxonomy types in `orc/view-types`:
  - `VideoDataType` with six values (Composite/Y+C/Colour x NTSC/PAL).
  - Colorimetric descriptors for colour types (matrix, primaries, transfer).
- Add shared coordinate model:
  - `PreviewCoordinate` (field index, line index, sample offset, data type context).
- Add preview capability declarations in core-facing contracts:
  - Stage-declared supported data types.
  - Navigation extent contract.
  - Geometry/aspect metadata contract.
  - Optional preview-tweakable parameter declaration + tweak class.
- Replace legacy contract entry points directly once the new shared contracts are available.

### Files/Layers Expected

- `orc/view-types/*` (new data types and metadata)
- `orc/core/*` (capability declaration structures)
- `orc/presenters/*` (pass-through DTO mapping updates)

### Tests

- Unit tests for:
  - Taxonomy enum/value mapping.
  - Colorimetric metadata round-trip and defaults.
  - `PreviewCoordinate` validation/bounds behavior.
  - Capability declaration schema and serialization expectations (if applicable).

### Exit Criteria

- All six data types are first-class in code.
- Colorimetric metadata is represented explicitly in contracts.
- No GUI type leaks into `core`/`view-types`.

---

## Phase 2 — Carrier Split and Render Pipeline Pivot

### Scope

Introduce the two-carrier model and begin retiring stage-side pre-baked RGB preview output.

### Implementation Work

- Define/standardize signal-domain carrier usage:
  - Use `VideoFieldRepresentation` (VFR) via DAG renderer for composite/Y+C preview paths.
- Define colour-domain carrier type:
  - Structured decoder output wrapper around component planes (no premature 8-bit quantization).
  - Include colorimetry metadata in this carrier.
- Add render-layer conversion helpers:
  - Signal-domain waveform/frame materialization for preview/export.
  - Colour-domain conversion to display target (BT.709 primaries + sRGB transfer) at rendering boundary.
- Switch preview rendering to carrier-backed rendering and remove stage-side baked RGB dependency in the same phase.

### Files/Layers Expected

- `orc/core/render*`, stage interfaces, DAG renderer integration points
- `orc/view-types/*` (new colour-domain carrier definitions)
- `orc/presenters/*` (if request/response contracts change)

### Tests

- Unit tests for:
  - VFR-based signal request at arbitrary node index.
  - Colour carrier integrity (precision preserved before boundary conversion).
  - Deterministic colour conversion metadata handling.
- Regression tests validating parity of expected preview outputs after the carrier migration.

### Exit Criteria

- Composite and Y+C preview data can be obtained from VFR path without stage-specific render baking.
- Colour preview path has explicit structured carrier with metadata.
- Rendering boundary conversion exists outside stage internals.

---

## Phase 3 — Core Preview View Registry

### Scope

Create a registration-driven view applicability and request/export contract in core/presenter layers.

### Implementation Work

- Add preview view contract in `orc/view-types`:
  - `supported_data_types()`
  - `request_data(VideoDataType, PreviewCoordinate)`
  - `export_as(format, path)`
- Add core registry in `orc/core`:
  - View registration at startup.
  - Query applicable views for active stage/data type.
  - Resolve request routing and export dispatch.
- Add presenter mediation:
  - Presenter API for querying available views and invoking request/export by identifier.
- Decouple vectorscope data from `PreviewImage` piggyback path; route through request contract.

### Files/Layers Expected

- `orc/core/preview*` (registry + routing)
- `orc/view-types/preview*` (view contracts)
- `orc/presenters/*preview*`

### Tests

- Unit tests for registry behavior:
  - Register/unregister/list views.
  - Data-type filtering correctness.
  - Duplicate registration handling.
  - Request/export error propagation.
- Regression tests confirming vectorscope data can be requested independently of full image render.

### Exit Criteria

- View applicability is registry-driven, not hardcoded dialog logic.
- Vectorscope request path no longer depends on `PreviewImage` side-channel.

---

## Phase 4 — GUI Integration and Ownership Cleanup

### Scope

Move GUI to consume registry/presenter contracts and unify supplementary-view ownership under preview subsystem.

### Implementation Work

- Refactor `PreviewDialog` to:
  - Query applicable views from presenter.
  - Create/show/hide launcher actions based on registry results.
  - Maintain and broadcast shared `PreviewCoordinate`.
  - Handle pixel-to-coordinate translation centrally.
- Move `VectorscopeDialog` ownership from `MainWindow` into preview subsystem.
- Update `LineScopeDialog`, `FieldTimingDialog`, `VectorscopeDialog` to implement/request via new contract.
- Preserve existing UX semantics for opening/closing tools and update cadence.

### Files/Layers Expected

- `orc/gui/preview*`, `orc/gui/main_window*`, view dialog classes
- corresponding presenter hookup files

### Tests

- GUI-focused tests where available, plus presenter/core tests validating:
  - Correct list of available tools for each data type.
  - Coordinate broadcast consistency across tools.
- Manual verification checklist:
  - Click-to-line-scope and click-to-vectorscope behavior.
  - Tool availability changes with stage/output type switches.

### Exit Criteria

- `MainWindow` no longer directly owns vectorscope tool lifecycle.
- `PreviewDialog` handles shared coordinate and tool orchestration through presenter/registry path.

---

## Phase 5 — CLI/Core Interface Alignment

### Scope

Keep `orc-cli` out of preview view/export responsibilities while ensuring it uses the same core-facing orchestration interfaces used by the GUI-facing presenter path.

Ensure source layout matches architectural ownership: when preview-related ownership is moved, the corresponding source files move into preview-focused locations as part of the same phase work.

### Implementation Work

- Ensure CLI command execution paths (process/orchestration/parameter application) go through the same presenter-mediated core interfaces as the GUI-facing path.
- Remove preview export requirements from CLI scope (no PNG/CSV preview export contract in `orc-cli`).
- If any preview-export CLI hooks/flags were introduced during refactor work, remove them instead of keeping compatibility shims.
- Move implementation files to reflect ownership boundaries introduced by the refactor:
  - Relocate preview-owned tool/dialog implementation out of generic window ownership areas and into preview subsystem source folders.
  - Keep presenter/core modules in matching preview namespaces/paths so architecture and source tree stay aligned.
  - Update CMake targets and includes in the same change; avoid leaving compatibility shims as long-term structure.

### Files/Layers Expected

- `orc/cli/command_process*`, CLI presenter interfaces
- `orc/presenters/*preview*`

### Tests

- CLI integration/unit tests for:
  - Representative CLI processing flows confirming presenter-mediated core orchestration is used.
  - Absence/rejection of preview export functionality in CLI surface area.
- Build-system and structure checks:
  - Confirm moved files are compiled from their new subsystem paths.
  - Confirm no stale includes or source references remain in old ownership locations.

### Exit Criteria

- `orc-cli` exposes no preview export feature.
- CLI processing paths use the same core-facing presenter orchestration interfaces as GUI-facing flows.
- Source tree structure matches runtime ownership and module boundaries (no ownership/source-location mismatch).

---

## Phase 6 — Live Parameter Tweaking Pipeline

### Scope

Add preview-time parameter application split into display-phase and decode-phase paths.

### Implementation Work

- Add lightweight render coordinator request:
  - `ApplyStageParameters` (in-memory targeted update) distinct from full `UpdateDAG`.
- Implement tweak classes in capability contract:
  - Display-phase: re-render cached decoded output with updated display conversion only.
  - Decode-phase: apply stage params and re-request current field/frame decode only.
- Add collapsible preview tweak panel in `PreviewDialog` driven by existing `ParameterDescriptor` metadata.
- Exclude output/file parameters from preview tweak panel.

### Files/Layers Expected

- `orc/core/render_coordinator*`
- stage capability metadata definitions
- `orc/presenters/*preview*`
- `orc/gui/preview_dialog*`

### Tests

- Unit tests for coordinator request semantics:
  - `ApplyStageParameters` does not trigger full DAG rebuild.
  - Correct invalidation/re-render behavior by tweak class.
- Functional tests (unit/integration) for representative chroma sink parameters:
  - Display-phase updates without re-decode.
  - Decode-phase updates re-decode current item only.

### Exit Criteria

- Preview tweak panel appears for stages declaring tweakable parameters.
- Display/decode tweak classes follow intended cost model.

---

## Phase 7 — Migration Completion and Hardening

### Scope

Remove legacy preview architecture remnants, complete migration across stages, and lock down docs/tests.

### Implementation Work

- Migrate remaining stages to capability declarations and carrier-backed preview path.
- Retire legacy `PreviewOutputType` coupling and old hardcoded view wiring.
- Remove superseded `render_preview()` usage from stages.
- Finalize architecture docs and contributor guidance.
- Add follow-up issue links for intentionally deferred FFmpeg tagging policy work.

### Files/Layers Expected

- cross-cutting cleanup in `orc/core`, `orc/presenters`, `orc/gui`, `orc/cli`, `docs/`

### Tests

- Full regression run:
  - `cmake --build build -j`
  - `ctest --test-dir build --output-on-failure`
  - `ctest --test-dir build -R MVPArchitectureCheck --output-on-failure`
- Add/refresh tests to ensure no stage bypasses capability contract path.

### Exit Criteria

- Legacy hardcoded preview/view paths are removed.
- All preview data routing uses explicit data type + carrier contracts.
- Documentation reflects final architecture and extension workflow.

---

## Cross-Phase Delivery Rules

- Execute phases sequentially on one branch; each phase must leave the branch buildable/testable before moving on.
- Prefer direct replacement over compatibility layering; remove obsolete code as soon as the replacement lands.
- Land test updates in the same PR as behavior/contract changes.
- Run MVP architecture checks at each phase checkpoint touching cross-layer includes.

## Suggested PR Breakdown

Use one focused PR for the full refactor, with phase-labeled commits (or commit groups) to preserve reviewability and rollback points during development.

## Risk Register and Mitigations

- **Risk:** Mid-branch instability while large contracts are replaced directly.
  - **Mitigation:** Require green build/tests at each phase checkpoint before starting the next phase.
- **Risk:** Hidden assumptions in vectorscope timing/data flow.
  - **Mitigation:** Introduce vectorscope request path with dedicated tests before GUI ownership move.
- **Risk:** Performance regressions in live tweaking.
  - **Mitigation:** Add instrumentation/bench assertions around display-phase vs decode-phase updates.
- **Risk:** MVP boundary erosion during GUI/registry integration.
  - **Mitigation:** Run architecture check in each relevant PR and block merges on failures.

## Definition of Done (Program-Level)

The refactor is complete when all of the following are true:

- View applicability and export are registry-driven by explicit data types.
- Signal-domain and colour-domain carriers are separate and preserved until output boundaries.
- Colorimetric metadata is first-class in preview and stage handoff contracts.
- GUI consumes preview request/export APIs via presenter/core contracts, while CLI remains non-preview and uses shared presenter-mediated core orchestration for its processing responsibilities.
- Live preview tweaks support display-phase and decode-phase update classes.
- Legacy hardcoded preview wiring paths are removed, and tests/docs reflect the new model.
