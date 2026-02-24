# EFM Decoder Sink – Phased Implementation Plan

## Scope and goals

This plan defines how to add a new **EFM Decoder Sink** stage that:

- Consumes EFM t-values from the existing VFR path (as demonstrated by the current `efm_sink` stage).
- Integrates the standalone `efm-decoder` logic from <https://github.com/simoninns/efm-decoder>.
- Lives in its own sub-folder(s) under `orc/core/stages/efm-decoder`.
- Exposes standalone CLI options as stage parameters, with UX-friendly mapping (e.g., merged boolean pairs).
- Produces a textual decoder report that can be written to file and also surfaced via Orc stage reporting/spdlog.

This is a **plan-only document**; no code changes are included here.

Assumption for this plan: once integrated, the decoder inside decode-orc becomes the **primary/canonical** implementation going forward.

Documentation note: user-facing and developer documentation for this feature is maintained in the `decode-orc-docs` repository. Any documentation produced as part of implementation in this repository should be authored in Markdown format and be ready for direct integration into the main documentation repository.

---

## Architecture direction (high-level)

1. Add a new sink stage (triggerable + parameterized) dedicated to EFM decode output.
2. Keep all implementation under `orc/core/stages/efm-decoder/...` (stage, adapters, report formatting, and any imported decoder code).
3. Use the existing VFR EFM extraction mechanism (same source as `efm_sink`) as decoder input.
4. Store decode statistics in a structured in-memory report model, then:
   - log key events through Orc logging,
   - expose `generate_report()` for inspection,
   - optionally persist a text report to disk.

---

## Parameter mapping (CLI → stage parameters)

The standalone CLI options should be represented in Orc as follows.

### Core mapping

| Standalone option(s) | Proposed stage parameter | Type | Notes |
|---|---|---|---|
| `--mode <audio|data>` | `decode_mode` | enum string | Values: `audio`, `data`; default `audio`. |
| positional `<output>` | `output_path` | file path | Required output target from sink. |

### Merged boolean-pair mapping

| Standalone option(s) | Proposed stage parameter | Type | Notes |
|---|---|---|---|
| `--no-timecodes` / `--timecodes` / neither | `timecode_mode` | enum string | Merge into one UX-safe option: `auto`, `force_no_timecodes`, `force_timecodes`. |
| `--no-wav-header` (audio only) | `audio_output_format` | enum string | Present as positive choice: `wav` or `raw_pcm` (instead of negative flag). |

### Audio-mode options

| Standalone option | Proposed stage parameter | Type |
|---|---|---|
| `--audacity-labels` | `write_audacity_labels` | bool |
| `--no-audio-concealment` | `audio_concealment` | bool (positive semantic) |
| `--zero-pad` | `zero_pad_audio` | bool |

`audio_concealment` mapping rule: `true` = concealment enabled (default), `false` = maps to standalone `--no-audio-concealment`.

### Data-mode options

| Standalone option | Proposed stage parameter | Type |
|---|---|---|
| `--output-metadata` | `write_data_metadata` | bool |

### Report/output parameters (new for Orc sink behavior)

| Parameter | Type | Purpose |
|---|---|---|
| `write_report` | bool | Enable writing textual decode report to disk. |
| *(derived)* | — | Report path is auto-derived from `output_path` by replacing extension with `.txt` when `write_report=true`. |

---

## Proposed folder layout

Under `orc/core/stages/efm-decoder`:

- `efm_decoder_sink_stage.h/.cpp` – stage class, parameter handling, trigger workflow.
- `config/` – parameter-to-decoder config mapper + validation.
- `pipeline/` – decode orchestration wrapper (VFR t-values → decoder pipeline).
- `report/` – structured stats model + text report renderer/writer.

If direct source import from standalone is required, place imported files under a clearly scoped stage subfolder (for example `pipeline/` with contracts in `config/`) and wrap them via adapter interfaces.

---

## Phased delivery plan

## Phase 0 — Source acquisition and pinning

### Deliverables

- Bring the standalone decoder source into the workspace as a **one-time bootstrap import**.
- Record source provenance (origin repository + source commit hash used for bootstrap).
- Land the integrated decoder under the Orc stage tree as an in-tree component and treat it as the project’s primary decoder implementation.
- Define internal ownership/maintenance notes in docs (future changes occur in decode-orc).
- Keep an adapter boundary so stage orchestration remains clean and testable.
- Ensure any temporary standalone copy/submodule used for bootstrap is removed once integration is complete.

### Exit criteria

- Team can build and work offline/reproducibly from in-tree decoder sources.
- Imported source provenance is explicit and auditable.
- No planned dependency on ongoing upstream sync for day-to-day maintenance.
- No residual standalone `efm-decoder` project copy remains in the decode-orc repository.

---

## Phase 1 — Design freeze and integration contract

### Deliverables

- Finalized stage name, node metadata, and parameter schema.
- Decoder integration strategy chosen:
  - **Preferred:** in-process library-style integration (shared code reuse),
  - **Fallback:** tightly controlled adapter boundary if direct reuse requires refactor.
- Agreed mapping table (above) reviewed and locked.
- Error and validation behavior defined (mode-specific parameter validation, required paths, extension hints).

### Exit criteria

- No unresolved parameter semantics.
- Clear decision on how standalone code is consumed in Orc.

---

## Phase 2 — Stage skeleton and build wiring

### Deliverables

- New stage files created under `orc/core/stages/efm-decoder`.
- Stage registered with `Orc_REGISTER_STAGE`.
- `orc/core/CMakeLists.txt` updated to compile new stage sources.
- `orc/core/stage_init.cpp` force-link entry added.
- Node appears in stage registry with sink semantics and trigger support.

### Exit criteria

- Project builds successfully.
- Stage is discoverable from registry and can be instantiated.

---

## Phase 3 — Parameter model and config translation

### Deliverables

- `get_parameter_descriptors()` exposes complete mapped parameter set.
- `set_parameters()` + parser convert Orc parameter map to decoder config.
- Mode-aware validation:
  - reject data-only options when `decode_mode=audio`,
  - reject audio-only options when `decode_mode=data`.
- Positive UX parameter semantics implemented (`timecode_mode`, `audio_output_format`, `audio_concealment`).

### Exit criteria

- Invalid combinations fail fast with actionable errors.
- Parameter defaults reproduce standalone CLI defaults.

---

## Phase 4 — Decode pipeline integration (VFR EFM input)

### Deliverables

- Trigger path extracts EFM samples from input `VideoFieldRepresentation`.
- Input adaptation feeds decoder pipeline in expected format/order.
- Decode outputs written to `output_path` according to mode + output format.
- Cancellation/progress callbacks integrated with existing `TriggerableStage` pattern.

### Exit criteria

- End-to-end decode runs from Orc source+VFR path to audio/data output.
- Cancellation leaves stage in clean state and clear status.

---

## Phase 5 — Reporting and diagnostics

### Deliverables

- Structured decode statistics captured from pipeline stages (sync, correction, counts, duration, etc.).
- `generate_report()` implemented to expose concise inspection data.
- Text report renderer outputs detailed human-readable report.
- Optional report file writing via `write_report` (report path auto-derived from `output_path` + `.txt`).
- Logging policy:
  - concise lifecycle info through Orc logs,
   - decoder stage uses inherited Orc logging (no stage-local log overrides).

### Exit criteria

- GUI/inspection can retrieve non-empty report after successful trigger.
- Text report file matches decode session results and is deterministic.

---

## Phase 6 — Validation matrix and parity checks

### Deliverables

- Functional validation matrix covering:
  - `decode_mode` audio/data,
  - `timecode_mode` variants,
  - WAV vs PCM output,
  - metadata/labels/report file toggles.
- Output parity spot-check against standalone decoder on same `.efm` inputs.
- Failure-case tests (missing EFM in VFR, invalid paths, invalid mode-option combos).

### Exit criteria

- No regressions in existing EFM sink behavior.
- New stage outputs and diagnostics are stable across repeated runs.

---

## Phase 7 — Documentation and handover

### Deliverables

- User-facing usage notes for the new stage (parameters + expected outputs).
- Developer notes on integration boundary with standalone decoder sources.
- Documentation handover artifacts prepared for `decode-orc-docs`, authored in Markdown and ready for direct integration into the main documentation set.
- Known limitations and follow-up backlog (if any).

### Exit criteria

- Feature is discoverable and operable without tribal knowledge.

---

## Risks and mitigations

1. **Bootstrap import divergence from original standalone source**
   - Mitigation: record bootstrap commit and keep an internal migration note describing any intentional behavior changes.

2. **Parameter UX confusion due to mode-specific flags**
   - Mitigation: merged enums + strict validation + explicit descriptions.

3. **Report verbosity vs log noise**
   - Mitigation: separate report artifact from runtime log level controls.

4. **Performance and memory overhead on long captures**
   - Mitigation: stream-oriented processing and bounded buffering where possible.

---

## Cleanup checklist (post-integration)

- Remove any temporary standalone import directory used during bootstrap.
- Remove any temporary git submodule entry and related metadata (including .gitmodules updates if used).
- Remove temporary CMake wiring or include paths that referenced the standalone project root.
- Verify all decoder code used by Orc now lives only under `orc/core/stages/efm-decoder`.
- Verify no build scripts or docs still instruct developers to fetch/use the standalone repo for normal development.
- Run configure/build to confirm repository integrity after cleanup.
- Run at least one audio-mode and one data-mode EFM Decoder Sink trigger to confirm behavior is unchanged after cleanup.

---

## Definition of done

The EFM Decoder Sink is complete when:

- It exists entirely under `orc/core/stages/efm-decoder` and is fully wired into Orc build/registry.
- It decodes VFR-provided EFM input into audio/data outputs with mapped parameters.
- It exposes and optionally writes a useful textual decode report.
- It passes the validation matrix and does not break existing pipeline stages.
- Any temporary standalone import/submodule used during bootstrap has been removed from the repo.
