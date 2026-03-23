# Preview Dialogue Refactor — Planning Document

## Background & Motivation

The preview dialogue (`PreviewDialog`) has grown organically alongside the stage pipeline. It works, but its structure reflects the order in which features were added rather than a coherent design. Several problems make it increasingly hard to extend or maintain:

- **No formal taxonomy of video data types.** The six kinds of video data a stage can produce are never explicitly named as a set; instead they are implied by a flat `PreviewOutputType` enum and scattered conditional logic.
- **Views are hardcoded, not registered.** `LineScopeDialog`, `FieldTimingDialog`, and `VectorscopeDialog` are wired into `PreviewDialog` and `MainWindow` by direct construction, not through any extensible mechanism. Adding a new view (e.g., a chroma waveform monitor or an RGB parade) requires modifying multiple files unrelated to the new view itself.
- **Vectorscope ownership is inconsistent.** `VectorscopeDialog` is owned by `MainWindow` rather than by the preview subsystem, while `LineScopeDialog` and `FieldTimingDialog` are owned by `PreviewDialog`. There is no single home for "tools that analyse a field".
- **Cross-view coordination is ad-hoc.** The crosshair click-to-line-scope flow passes raw pixel coordinates through multiple layers without a shared coordinate model, making it fragile and hard to reuse for CLI export.
- **CLI export is not structurally first-class.** Saving a preview or scope as PNG/CSV works, but only because individual paths happen to call the same rendering functions — not because the architecture guarantees it.
- **Stage capabilities are implicit.** Whether a stage produces composite, Y+C, or colour output — and therefore which views apply to it — is inferred from the `PreviewOption` list rather than declared as a first-class capability contract.
- **Chroma-output stages expose input data inconsistently.** Stages that decode colour (chroma sinks) should always be able to expose their composite or Y+C input alongside the colour output, but this is not enforced or structured.

The goal of this refactor is to replace these ad-hoc arrangements with a design that is explicit, extensible, and free of duplication — following the same registration-based philosophy already used for stages and analysis tools.

---

## Video Data Taxonomy

Every piece of video data that a stage can expose for preview belongs to one of six types:

| Type | Signal content | Notable characteristics |
|---|---|---|
| **Composite NTSC** | Single 16-bit sample stream (signal energy in mV) | NTSC field dimensions |
| **Composite PAL** | Single 16-bit sample stream (signal energy in mV) | PAL field dimensions |
| **Y+C NTSC** | Two 16-bit sample streams: Y (luma) + C (chroma) | NTSC field dimensions |
| **Y+C PAL** | Two 16-bit sample streams: Y (luma) + C (chroma) | PAL field dimensions |
| **Colour NTSC** | Decoded colour frame (typically YUV → RGB for display) | NTSC active picture dimensions |
| **Colour PAL** | Decoded colour frame (typically YUV → RGB for display) | PAL active picture dimensions |

The NTSC/PAL distinction within a type affects only video dimensions and a small number of signal parameters (e.g., field rate, FSC, IRE scale). The structural differences between types — number of channels, whether data is composite/component, whether it is in signal space or display colour space — are independent of the system standard.

Stages that produce colour output (chroma sinks) always receive composite or Y+C data as their input. That input data is therefore always available alongside the colour output and should be exposed as such without requiring special-case code.

### Colorimetric Metadata on Colour Types

The two Colour types carry an additional dimension beyond their video system: the **colorimetric interpretation** of the decoded signal. Analog video colour standards evolved over time, so the same physical recording may have been captured with different matrix coefficients, primaries, and transfer characteristics. The three relevant axes are:

| Axis | Representative values |
|---|---|
| **Matrix coefficients** | NTSC-1953 / FCC (ISO/IEC 23091-2 coefficient 4); BT.470 / BT.601 / ST 170 (coefficients 5 & 6) |
| **Primaries** | NTSC-1953; SMPTE C; EBU / BT.470 PAL; others |
| **Transfer characteristics** | 2.2 γ (NTSC, ST 170, BT.470 525-line); 2.8 γ (BT.470 625-line / PAL); 2.4 γ (BT.1886); BT.709 OETF; BT.1886 App. 1 |

The Colour NTSC and Colour PAL data types must carry these three parameters as part of their definition, not as an afterthought. This feeds directly into two downstream concerns:

1. **Preview display conversion.** The rendering path must convert from the declared colorimetry to the display output colorimetry. The safe cross-platform target is BT.709 primaries with sRGB transfer curve, which maps cleanly onto the compositors on macOS (Quartz), Wayland, and Windows. This conversion belongs in the base rendering infrastructure, not in individual stage implementations.

2. **FFmpeg export metadata.** When exporting via FFmpeg, the declared matrix coefficients, primaries, and transfer characteristics should be written as stream metadata tags rather than silently defaulted. An additional option — to perform a full colorimetric conversion to a modern output standard rather than merely tagging — should also be available.

This work corresponds to [issue #140](https://github.com/simoninns/decode-orc/issues/140). It is in scope for this refactor because the taxonomy and stage capability contract are the right structural home for colorimetric metadata; introducing them now avoids a later retrofit. Any changes to the chroma decode math inside `ld-chroma-decoder` itself are out of scope and a separate upstream concern.

---

## Current Architecture Problems in Detail

### 1. Flat `PreviewOutputType` Enum

`PreviewOutputType` mixes two orthogonal concepts: the *source data type* (what kind of signal) and the *rendering mode* (how to display it — field, frame, split). There is no clear mapping between data types and which rendering modes or supplementary views apply to them.

### 2. No View Registration

Each supplementary view (line scope, timing graph, vectorscope) is instantiated and connected by hand in `PreviewDialog`/`MainWindow` setup code. The construction, destruction, and signal wiring of each view is embedded in unrelated files. A new view requires:
- New dialog class
- Manual construction in `PreviewDialog` or `MainWindow`
- Manual signal connections for data delivery and coordinate events
- Separate code path for CLI export

### 3. Scattered Coordinate Passing

When a user clicks on `FieldPreviewWidget`, pixel coordinates are translated into field/line indices and propagated to child views. This translation logic lives partly in the widget, partly in `PreviewDialog`, and partly in the presenter. There is no shared `PreviewCoordinate` model that all views can subscribe to.

### 4. `VectorscopeData` Piggybacked on `PreviewImage`

The vectorscope data is embedded in `PreviewImage.vectorscope_data` as a side-effect of the render path, rather than being requested and delivered through the same mechanism as all other view data. This means the vectorscope cannot be independently requested (e.g., for a CLI export or a standalone update without a full re-render).

### 5. Stage Capability is Implicit

The set of views that make sense for a given stage is not declared anywhere; it is inferred at runtime from the `PreviewOption` list. There is no compile-time or registry-time contract stating "this stage produces Y+C data, therefore the line scope will show two channels".

---

## Desired Architecture

### Core Principle: Views Register Against Data Types

Rather than `PreviewDialog` knowing about each view by name, all supplementary views — line scope, timing graph, vectorscope, waveform monitor, RGB parade, and any future additions — should register themselves with a **preview view registry** against the data types they can consume. The registry is part of `orc-core` (the data and capability side); the concrete Qt widget implementations remain in `orc-gui`.

When the active stage changes or the user switches output type, the registry tells `PreviewDialog` which views are applicable for that data type. `PreviewDialog` shows or hides view-launcher buttons accordingly, without knowing anything specific about each view.

### Fixed Stage-to-Preview Interface

The interface between a stage and the preview subsystem should be fixed and uniform. Every previewable stage declares:

1. **Which video data types it can expose** — one or more entries from the taxonomy above, including input data for chroma-output stages.
2. **The navigation extent** — number of renderable items (fields, frames) and the appropriate navigation granularity.
3. **The geometry** — active picture dimensions and display aspect ratio correction.

These declarations are made once, at stage registration time. No individual stage should need to implement custom export logic, coordinate translation, or view management.

The variation in *how* data is rendered for each type (composite waveform, dual-channel Y+C, RGB display frame) lives in the base rendering infrastructure, not in each stage.

### View Interface

Each supplementary view implements a minimal interface:

- **`supported_data_types()`** — declares which of the six video data types can feed this view.
- **`request_data(VideoDataType, coordinate)`** — asks the view to update itself given a field/line coordinate in the shared coordinate model.
- **`export_as(format, path)`** — produces a PNG or CSV export using the same data path, enabling CLI export without any GUI-specific code.

Views register with the preview view registry at application startup, mirroring how stages and analysis tools register themselves.

### Shared Coordinate Model

A `PreviewCoordinate` structure (field index, line index, x-sample offset, and data type context) is defined once in `orc/view-types`. All views receive and emit coordinates in this model. The base `PreviewDialog` machinery handles:

- Translation from pixel click on `FieldPreviewWidget` to `PreviewCoordinate`
- Broadcasting the coordinate to all currently-active registered views
- Maintaining a "last known coordinate" for views that open after a click

No individual view implements pixel-to-field translation.

### Chroma-Output Stage Input Exposure

For stages that produce colour output, the composite or Y+C input data is always present. The stage declares which input type it receives as part of its capability declaration. The preview infrastructure automatically makes the appropriate input-side views (line scope on composite or Y+C, timing graph) available alongside the colour-output views (vectorscope, waveform monitor), without any per-stage conditional code.

### Live Parameter Tweaking

For stages such as the chroma sink, a subset of parameters directly affects the visual output and is worth exposing as a live control panel inside the preview window — so the user can tweak and immediately see the result, rather than closing the preview, opening the stage parameter dialog, re-triggering a full encode, and re-opening the preview.

The stage capability declaration (being introduced as part of this refactor) should indicate which parameters are **preview-tweakable**, and each such parameter should carry a **tweak class** that determines how fast the update cycle is:

| Tweak class | Examples | Update cost | Update path |
|---|---|---|---|
| **Display-phase** | Colorimetric matrix, primaries, transfer characteristics (issue #140) | Near-instant | Re-apply conversion to the already-decoded cached output; no re-decode needed |
| **Decode-phase** | `chroma_gain`, `chroma_phase`, `luma_nr`, `chroma_nr`, `decoder_type`, `transform_threshold`, `ntsc_phase_comp` | Single-field re-decode | Apply new parameters to the stage instance, re-request the current preview field only — no full encode to a file |

**Output parameters** (file path, encoder preset, container format) are not preview-tweakable and are excluded from the live panel.

The live loop for each class:

- **Display-phase:** parameter slider moves → debounced → `ApplyDisplayParameters` request → re-render current cached field with new conversion → preview updates. No re-trigger.
- **Decode-phase:** parameter slider moves → debounced → `ApplyStageParameters` + `RenderPreview` request sequence → stage re-decodes the current field/frame using the new parameters → preview updates. This reuses the existing `render_preview()` path that stages already implement for on-demand single-field decoding; it is not a full encode-to-file trigger.

The key architectural requirement is a new, lightweight `ApplyStageParameters` request type in `RenderCoordinator` — distinct from `UpdateDAG` (which triggers a full DAG rebuild) — that updates only the named stage's in-memory parameter state and then immediately follows with a preview render. This avoids unnecessary work for all other stages in the graph.

The live parameter panel itself is surfaced in `PreviewDialog` automatically for any stage that declares preview-tweakable parameters in its capability contract. The panel is driven by the same `ParameterDescriptor` metadata already used by the stage parameter dialog, so no new UI descriptor logic is needed — just a different presentation context. The panel should be collapsible so it does not consume permanent screen space.

The colorimetric parameters introduced for issue #140 are display-phase tweaks: changing the declared matrix, primaries, or transfer characteristics for preview purposes does not require re-running the chroma decoder; it only affects the YUV-to-display-RGB conversion step in the render path.

### CLI Export Integration

Because every view exposes `export_as(format, path)` and data is requested through a common `request_data()` contract, the CLI can invoke any registered view's export by:

1. Resolving the stage data type from its capability declaration.
2. Looking up applicable views in the registry.
3. Calling `request_data` and then `export_as` on each desired view.

No GUI-specific code is involved. The presenter layer mediates between the CLI and the registry/views, following the existing MVP structure.

### MVP Layer Mapping

| Concern | Layer |
|---|---|
| Video data types, coordinate model, view interface contract | `orc/view-types` |
| Stage capability declarations, view registry, data fetch logic | `orc/core` |
| Presenter mediating between CLI/GUI and core registry | `orc/presenters` |
| Concrete Qt widget implementations of each view | `orc/gui` |
| CLI export commands invoking view `export_as` | `orc/cli` |

Core never knows about Qt. The view interface in `orc/view-types` is defined purely in terms of the coordinate model and the data types — no GUI types cross the boundary.

---

## Desired Outcomes

When this refactor is complete, the following should be true:

1. **Adding a new supplementary view** (e.g., a chroma waveform monitor) requires writing one new file: the view implementation. No changes to `PreviewDialog`, `MainWindow`, or any existing view are needed.

2. **Adding a new stage with a new video data type** requires only that the stage declares its data type in the standard capability interface. All views that support that data type automatically become available for that stage.

3. **Crosshair and click-to-view coordination** is handled entirely by `PreviewDialog`'s base logic via the shared `PreviewCoordinate` model. No view implements pixel translation independently.

4. **CLI export of any preview view** works without touching GUI code, using the same rendering path as the GUI.

5. **Chroma-output stages always expose their input data** with no per-stage special casing.

6. **The six video data types are named explicitly** in code and used as the primary discriminant for view applicability, rather than being inferred from option lists or enum values.

7. **No code duplication** across view implementations for common concerns: coordinate handling, export formatting, aspect-ratio correction, IRE scale markers, and data-type routing.

8. **Colorimetric metadata is first-class** (issue #140). The Colour NTSC/PAL types carry matrix coefficients, primaries, and transfer characteristics. The preview display path converts from declared colorimetry to sRGB/BT.709 for the host compositor. FFmpeg export writes the declared colorimetry as stream metadata, with an opt-in full conversion path.

9. **Live parameter tweaking works from inside the preview window.** For stages that declare preview-tweakable parameters, a collapsible control panel appears in `PreviewDialog`. Display-phase parameters (colorimetric metadata) update the preview without re-decoding. Decode-phase parameters (chroma gain, phase, NR, decoder type, etc.) trigger a single-field re-decode via a lightweight `ApplyStageParameters` path, not a full encode-to-file re-trigger.

---

## Constraints & Principles

- **MVP boundaries are preserved.** Core never depends on GUI headers. All new types crossing the boundary go through `orc/view-types`.
- **Stage implementations are not changed arbitrarily.** The refactor touches the *interface* between stages and the preview subsystem; internal stage rendering logic changes only where duplication can be eliminated by shared helpers.
- **Existing unit test coverage is maintained.** New core types and the view registry must be covered by unit tests following the existing patterns.
- **CLI and GUI remain independent consumers** of the same presenter layer. Neither is privileged in the design.
- **The registration pattern mirrors the existing `StageRegistry`** design so the codebase remains conceptually uniform.
- **Chroma decode math is not touched.** Colorimetric parameters (issue #140) are declared and propagated by the preview/export infrastructure; any changes to the underlying `ld-chroma-decoder` decode algorithm are a separate upstream concern.

---

## Out of Scope (for this document)

A detailed phased implementation plan — including which files change in which order, migration strategy for existing stages, and test scaffolding — is deferred to a follow-up planning document once the architectural goals above have been reviewed and confirmed.
