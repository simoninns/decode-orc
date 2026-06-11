# Design: Migration from TBC-based VFR to CVBS_U10_4FSC Internal Representation

---

## 1. Overview

### 1.1 Background

Decode-Orc's current internal representation is derived from the ld-decode TBC (Time Base Corrected) format — a field-based, "almost 4fsc" encoding with signal levels that are not fixed but defined by per-file metadata. No downstream code can reason about sample levels without metadata-derived calibration. The format is not aligned with any broadcast standard and couples every analysis and display stage to proprietary metadata semantics.

This design migrates the internal representation to `CVBS_U10_4FSC` as defined in the [CVBS File Format Specification](cvbs-file-format-specification/docs/index.md), which is itself grounded in EBU Tech. 3280-E (PAL) and SMPTE 244M-2003 (NTSC). The data model changes from **field-based** (driven by the TBC field-pair model) to **frame-based** (driven by the CVBS format's frame-as-smallest-unit principle). The `VideoFieldRepresentation` (VFR) interface is replaced by a new `VideoFrameRepresentation` (VFrameR) interface.

Decode-Orc advances from version 1.x to **version 2.0.0**. No backwards compatibility with v1.x project files is provided.

### 1.2 Design Goals

- Replace `VideoFieldRepresentation` with `VideoFrameRepresentation` using `FrameID` as the primary navigation unit.
- Internal sample encoding: `CVBS_U10_4FSC` — signed 16-bit container, 10-bit domain, with EBU 3280-E / SMPTE 244M-2003 normative level tables.
- All CVBS source encodings (`CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, `CVBS_S16_FSC`) converted to `CVBS_U10_4FSC` at ingestion.
- PAL and NTSC TBC source stages convert from TBC levels to `CVBS_U10_4FSC` with correct CVBS frame structure.
- Dropout representation migrates to the [CVBS dropout extension format](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md) (frame-based, flat-sample-offset addressing).
- Audio handling follows the CVBS specification `audio_locked` contract: frame-locked audio (PAL: 44100 Hz, NTSC/PAL_M: 44100000/1001 Hz) enables frame-indexed access and per-frame processing; free-running audio (44100 Hz) is preserved but not frame-aligned. Stages that manipulate frame sequences (FrameMapStage, stacker) may only process audio when the source is frame-locked.
- All chroma decoder mathematics and vectorscope geometry derived exclusively from spec-defined constants — no magic numbers.
- Project YAML format version bumps from `"1.x"` to `"2.0"`. No upgrade path is provided; v1.x project files are hard-rejected.
- SDK plugin ABI version is bumped; the public SDK documentation is updated.
- All user-facing documentation (user guides, stage reference, GUI help, release notes) is reviewed and revised to reflect the v2.0 frame-based model, updated terminology, and removed/renamed stages.

### 1.3 Scope Summary

This is a breaking change affecting every module of Decode-Orc. The high-level affected areas are:

| Area | Change |
|------|--------|
| `orc/core/include/video_field_representation.h` | Replaced by `VideoFrameRepresentation` |
| `orc/core/tbc_source_internal/` | TBC VFR implementations replaced with CVBS VFrameR converters |
| `orc/view-types/orc_source_parameters.h` | `SourceParameters` updated to frame-based geometry and spec-defined levels |
| `orc/plugins/stages/cvbs_source/` | Updated to emit VFrameR; adds dropout sidecar loading |
| `orc/plugins/stages/pal_tbc_comp_source/`, `pal_tbc_yc_source/` | TBC → CVBS_U10_4FSC frame conversion |
| `orc/plugins/stages/ntsc_tbc_comp_source/`, `ntsc_tbc_yc_source/` | TBC → CVBS_U10_4FSC frame conversion |
| `orc/plugins/stages/ld_sink/` | CVBS_U10_4FSC → TBC inverse conversion |
| `orc/plugins/stages/sinks/common/` | Chroma decoder updated for CVBS domain |
| All other transform and sink stages | VFrameR wrapper pattern |
| `orc/core/include/preview_renderer.h` | Frame-based rendering, spec-derived level mapping |
| `orc/gui/linescopedialog.h` | Renamed `FrameScopeDialog`; spec-derived mV conversion |
| `orc/gui/fieldtimingdialog.h` | Renamed `FrameTimingDialog` |
| `orc/core/analysis/vectorscope/` | All constants derived from spec |
| `orc/core/include/project.h`, `project.cpp` | Version 2.0 schema |
| `orc/sdk/include/orc/plugin/orc_plugin_abi.h` | ABI version bump |
| `orc-tests/` | All mocks and fixtures updated for VFrameR and int16 samples |

---

## 2. Terminology Changes

| Old term | New term | Rationale |
|----------|----------|-----------|
| `VideoFieldRepresentation` (VFR) | `VideoFrameRepresentation` (VFrameR) | Frame is the smallest unit per CVBS spec |
| `VideoFieldRepresentationWrapper` | `VideoFrameRepresentationWrapper` | Base class for transform stages |
| `VideoFieldRepresentationPtr` | `VideoFrameRepresentationPtr` | Shared pointer alias |
| `FieldID` | `FrameID` | Primary navigation unit |
| `FieldIDRange` | `FrameIDRange` | Range type |
| `FieldDescriptor` | `FrameDescriptor` | Frame-level metadata |
| `field_count()` | `frame_count()` | Navigation method |
| `has_field()` | `has_frame()` | Navigation method |
| `field_range()` | `frame_range()` | Navigation method |
| `get_field()` | `get_frame_copy()` | Bulk copy access |
| `get_line(FieldID, line)` | `get_line(FrameID, line)` | Line is now a 0-based frame line |
| `sample_type = uint16_t` | `sample_type = int16_t` | CVBS_U10_4FSC uses signed 16-bit |
| `DropoutRegion` | `DropoutRun` | Aligned to CVBS dropout extension format |
| `get_dropout_hints(FieldID)` | `get_dropout_hints(FrameID)` | Frame-level hints |
| `LineScopeDialog` | `FrameScopeDialog` | Scope operates on frame lines |
| `fieldtimingdialog` | `frametimingdialog` | Timing operates on frames |
| Field scope | Frame scope | Naming |
| `DAGFieldRenderer` | `DAGFrameRenderer` | Renderer navigates by FrameID |

---

## 3. CVBS_U10_4FSC Internal Signal Domain

### 3.1 Encoding

All internal sample data uses `CVBS_U10_4FSC` as defined in [sample-encoding-presets.md](cvbs-file-format-specification/docs/sample-encoding-presets.md):

- **Word format:** signed 16-bit little-endian integer in the C++ type `int16_t`.
- **Domain:** the unsigned 10-bit value (0–1023) maps directly to the int16 range (0–1023). Value 0 = int16 0; value 1023 = int16 1023.
- **Negative headroom:** values below 0 (down to −32768) are valid and represent signal content below sync tip — for example PAL pilot bursts, chroma excursions.
- **Positive headroom:** values above 1023 (up to 32767) are valid and represent signal content above the peak level — for example 100% yellow or super-white.
- **`has_nonstandard_values` flag** (from `SourceParameters`, read from CVBS metadata): governs whether headroom and protected values may be present:
  - `has_nonstandard_values = false`: the source is strictly conformant. All sample values lie within [4, 1019] — protected values (0–3 and 1020–1023) are absent and headroom values (negative or above 1023) do not occur.
  - `has_nonstandard_values = true`: the source still uses the standard signal levels (sync tip, blanking, black, white, peak) for the declared video format, but sample values may extend outside [0, 1023] (sub-sync or super-peak content) and protected values (0–3 and 1020–1023) may also appear.
  Consuming code must tolerate protected values and out-of-range samples without crashing regardless of this flag.

This is a breaking change from the current `uint16_t` TBC sample domain.

### 3.2 Normative Signal Level Tables

These are the sole reference values for all signal level analysis, preview rendering, chroma decoding, and vectorscope calibration. No metadata-derived level tables are used for these purposes. Values are taken directly from the authoritative standards.

**PAL** — EBU Tech. 3280-E (see [EBU-Tech-3280-E.md](analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md)):

| Level | 10-bit value | Notes |
|-------|-------------|-------|
| Sync tip | 4 | Minimum legal value |
| Blanking | 256 | 0 IRE reference |
| Black | 282 | Nominal picture black (0 IRE) |
| White (100%) | 844 | 100 IRE |
| Peak (w/ chroma) | 1019 | Maximum legal value |

**NTSC** — SMPTE 244M-2003 (see [SMPTE-244M-2003.md](analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md)):

| Level | 10-bit value | Notes |
|-------|-------------|-------|
| Sync tip | 16 | Minimum legal value |
| Blanking | 240 | 0 IRE reference |
| Black | 252 | 7.5 IRE pedestal |
| White (100%) | 800 | 100 IRE |
| Peak (w/ chroma) | 988 | Maximum legal value |

**PAL_M** — ITU-R BT.1700-1 / SMPTE 244M-compatible (see [BT-1700-E.md](analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md)):

| Level | 10-bit value | Notes |
|-------|-------------|-------|
| Sync tip | 16 | |
| Blanking | 240 | 0 IRE reference |
| Black | 252 | 7.5 IRE pedestal |
| White (100%) | 800 | 100 IRE |
| Peak (w/ chroma) | 988 | |

**NTSC-J:** NTSC with `black_level` = 240 (no 7.5 IRE pedestal). Handled as a `FrameDescriptor.black_level_override` flag. No separate `VideoSystem` enum value is required.

### 3.3 Normative Frame Sizes

These sizes apply when the signal state is `STANDARD_TBC_LOCKED` per the [video-standard-presets.md](cvbs-file-format-specification/docs/video-standard-presets.md):

| Standard | Samples/frame | Lines/frame | Samples/line | Orthogonal? |
|----------|-------------|------------|------------|-------------|
| PAL | **709,379** | 625 | 1135.0064 avg | **No** — 4 extra samples/frame at non-orthogonal positions |
| NTSC | **477,750** | 525 | 910 (exact) | Yes |
| PAL_M | **477,225** | 525 | 909 (exact) | Yes |

The PAL non-orthogonal structure is defined by EBU Tech. 3280-E §1.3.1: exactly 4 extra samples are distributed across the frame. The 4 extra samples occur on specific lines determined by the 4fsc/line-rate phase relationship, repeating at frame rate. Based on EBU Tech. 3280-E and the existing CVBS source implementation, the 1136-sample lines in a PAL frame are:
- Field 1, line 155 (0-indexed within field): 1136 samples
- Field 1, line 311 (0-indexed within field): 1136 samples
- Field 2, line 156 (0-indexed within field): 1136 samples
- Field 2, line 312 (0-indexed within field): 1136 samples

Consuming code must never assume all PAL lines have 1135 samples.

### 3.4 Sampling Constants

All code referencing subcarrier frequency, sample rate, or samples-per-line must use these constants with the cited sources:

```cpp
// EBU Tech. 3280-E §1.1: PAL 4fsc
constexpr double kPalFsc            = 4'433'618.75;        // Hz, exact
constexpr double kPalSampleRate     = 17'734'475.0;        // Hz = 4 × fsc, exact
constexpr double kPalSamplesPerLine = 1135.0064;           // EBU 3280-E §1.3.1 exact average
constexpr int32_t kPalFrameSamples  = 709'379;             // EBU 3280-E normative
constexpr int32_t kPalFrameLines    = 625;

// SMPTE 244M-2003 §4.1: NTSC 4fsc
constexpr double kNtscFsc           = 315.0e6 / 88.0;      // Hz = 3,579,545.45...
constexpr double kNtscSampleRate    = 4.0 * kNtscFsc;      // Hz = 14,318,181.81...
constexpr int32_t kNtscSamplesPerLine = 910;                // SMPTE 244M-2003 §4.1.1 exact
constexpr int32_t kNtscFrameSamples = 477'750;              // 525 × 910, normative
constexpr int32_t kNtscFrameLines   = 525;

// ITU-R BT.1700-1 Annex 1 Part B: PAL-M 4fsc
constexpr double kPalMFsc           = 909.0 / 4.0 * (525.0 * 30000.0 / 1001.0); // Hz ≈ 3,575,611.89
constexpr double kPalMSampleRate    = 4.0 * kPalMFsc;
constexpr int32_t kPalMSamplesPerLine = 909;                // ITU-R BT.1700-1 Annex 1 Part B
constexpr int32_t kPalMFrameSamples = 477'225;              // 525 × 909, normative
constexpr int32_t kPalMFrameLines   = 525;
```

These constants must be placed in a single shared header (e.g., `orc/core/include/cvbs_signal_constants.h`) referenced from all code that requires them.

### 3.5 Headroom Implications for All Display and Analysis Tools

The `CVBS_U10_4FSC` signed container permits values outside [4, 1019] when `has_nonstandard_values = true`. Every display tool, every analysis tool, and every chroma decoder must:
- Accept and correctly process headroom values without clamping or treating them as errors.
- Use the normative levels from §3.2 as the calibration reference for all display mappings.
- Display PAL pilot burst content (negative values, below sync tip) and chroma excursions above peak faithfully.
- **Auto-scale their display range**: the default scale must be set to the range expected by the video format (sync tip to peak, per §3.2), but when samples outside that range are present the display must expand its scale to include them. This prevents the standard signal range from appearing as a small portion of an unnecessarily large fixed axis. The normative level markers (§3.2) remain visible at their correct positions regardless of the current scale.

---

## 4. VideoFrameRepresentation Interface

### 4.1 Preliminary Code Analysis

Files that define or implement the current field-based interface — all must be replaced or updated:

| File | Role |
|------|------|
| `orc/core/include/video_field_representation.h` | Primary interface — full replacement |
| `orc/core/tbc_source_internal/tbc_video_field_representation.h` | Composite TBC impl — replaced by TBC→CVBS converter |
| `orc/core/tbc_source_internal/tbc_yc_video_field_representation.h` | YC TBC impl — same |
| `orc/view-types/orc_source_parameters.h` | `SourceParameters` — field geometry → frame geometry |
| `orc/core/include/dag_executor.h` | DAG artifact typing — updated type name |
| `orc/core/include/dag_field_renderer.h` | `DAGFieldRenderer` — renamed `DAGFrameRenderer` |
| `orc/core/include/field_id.h` | `FieldID` — new `FrameID` type alongside |

### 4.2 FrameID and Frame Navigation Types

```cpp
// orc/core/include/frame_id.h
using FrameID = uint64_t;
struct FrameIDRange { FrameID first; FrameID last; };
```

`FieldID` and `FieldIDRange` are retained as deprecated aliases during the transition, removed in v2.0 final.

### 4.3 FrameDescriptor

Replaces `FieldDescriptor`. Frame-level metadata. Field ordering is always field 1 first (enforced by source stages); colour sequence position is a frame property:

```cpp
struct FrameDescriptor {
  FrameID frame_id;
  VideoSystem system;                    // PAL, NTSC, PAL_M
  size_t height;                         // Total lines in frame (625 / 525)
  size_t samples_total;                  // Exact samples in frame (709379 / 477750 / 477225)
  size_t samples_per_line_nominal;       // 1135 / 910 / 909

  // Colour frame sequence position; -1 if unknown
  // PAL/PAL_M: 1–4 per EBU 3280-E §1.1.1 / BT.1700 Annex 1
  // NTSC: 0 (frame A) or 1 (frame B) per SMPTE 244M-2003 §3.2
  int colour_frame_index;

  // Optional frame addressing metadata (may be derived from VBI, CLV metadata, or other sources)
  std::optional<int32_t> frame_number;
  std::optional<uint32_t> timecode;

  // NTSC-J black level override: populated when black_level != 252
  // Value is in CVBS_U10_4FSC 10-bit domain
  std::optional<int32_t> black_level_override;

  // Set true for synthetic padding frames inserted by FrameMapStage to fill gaps.
  // Downstream stages must check this flag and skip signal processing on padding frames —
  // their sample data is not real signal content.
  bool is_padding_frame = false;
};
```

### 4.4 VideoFrameRepresentation Interface

This is the complete replacement for `VideoFieldRepresentation`:

```cpp
class VideoFrameRepresentation : public Artifact {
 public:
  using sample_type = int16_t;  // CVBS_U10_4FSC: signed 16-bit, 10-bit domain

  ~VideoFrameRepresentation() override = default;

  // Frame navigation
  virtual FrameIDRange frame_range() const = 0;
  virtual size_t frame_count() const = 0;
  virtual bool has_frame(FrameID id) const = 0;

  // Frame metadata
  virtual std::optional<FrameDescriptor> get_descriptor(FrameID id) const = 0;

  // Sample access — flat frame (preferred for batch processing)
  // Returns pointer to all samples_total samples; layout is [field1 samples][field2 samples]
  // Lifetime: valid until next call to get_frame or object destruction
  virtual const sample_type* get_frame(FrameID id) const = 0;

  // Sample access — line-based (convenience; 0-based frame line index)
  // Returns pointer to the start of the given line within the frame
  // For PAL: line length varies (1135 or 1136); use get_descriptor() for per-line lengths if needed
  virtual const sample_type* get_line(FrameID id, size_t line) const = 0;

  // Bulk copy access
  virtual std::vector<sample_type> get_frame_copy(FrameID id) const = 0;

  // YC (dual-file) support — same semantics as current VFR
  virtual bool has_separate_channels() const { return false; }
  virtual const sample_type* get_frame_luma(FrameID /*id*/) const { return nullptr; }
  virtual const sample_type* get_frame_chroma(FrameID /*id*/) const { return nullptr; }
  virtual const sample_type* get_line_luma(FrameID /*id*/, size_t /*line*/) const { return nullptr; }
  virtual const sample_type* get_line_chroma(FrameID /*id*/, size_t /*line*/) const { return nullptr; }

  // Hints (frame-level)
  virtual std::vector<DropoutRun> get_dropout_hints(FrameID /*id*/) const { return {}; }
  virtual std::optional<FramePhaseHint> get_frame_phase_hint(FrameID /*id*/) const { return std::nullopt; }
  virtual std::optional<ActiveLineHint> get_active_line_hint() const { return std::nullopt; }
  virtual std::optional<SourceParameters> get_video_parameters() const { return std::nullopt; }
  virtual std::optional<VbiData> get_vbi_hint(FrameID /*id*/) const { return std::nullopt; }

  // Audio — per-frame access is only valid when audio_locked() == true (CVBS spec audio_locked field).
  // PAL locked: 1764 stereo int16_t pairs per frame at 44100 Hz.
  // NTSC/PAL_M locked: 1470 stereo int16_t pairs per frame at 44100000/1001 Hz.
  // Free-running audio (audio_locked() == false) is not frame-aligned; get_audio_samples() returns empty.
  virtual uint32_t get_audio_sample_count(FrameID /*id*/) const { return 0; }
  virtual std::vector<int16_t> get_audio_samples(FrameID /*id*/) const { return {}; }
  virtual bool has_audio() const { return false; }
  // nullopt = no audio or locking state unknown; true = frame-locked; false = free-running
  virtual std::optional<bool> audio_locked() const { return std::nullopt; }

  // EFM t-values (per frame; LaserDisc extension, not in CVBS spec core)
  virtual uint32_t get_efm_sample_count(FrameID /*id*/) const { return 0; }
  virtual std::vector<uint8_t> get_efm_samples(FrameID /*id*/) const { return {}; }
  virtual bool has_efm() const { return false; }

  // AC3 RF symbols (per frame; LaserDisc extension)
  virtual uint32_t get_ac3_symbol_count(FrameID /*id*/) const { return 0; }
  virtual std::vector<uint8_t> get_ac3_symbols(FrameID /*id*/) const { return {}; }
  virtual bool has_ac3_rf() const { return false; }

  std::string type_name() const override { return "VideoFrameRepresentation"; }

 protected:
  VideoFrameRepresentation(ArtifactID id, Provenance prov)
      : Artifact(std::move(id), std::move(prov)) {}
};

using VideoFrameRepresentationPtr = std::shared_ptr<VideoFrameRepresentation>;
```

### 4.5 VideoFrameRepresentationWrapper

Replaces `VideoFieldRepresentationWrapper`. Same forwarding pattern — transform stages override only the methods for data they actually modify:

```cpp
class VideoFrameRepresentationWrapper : public VideoFrameRepresentation {
 public:
  // All methods auto-forward to source_ by default
  // Override only the methods the transform stage actually changes
  FrameIDRange frame_range() const override { return source_ ? source_->frame_range() : FrameIDRange{}; }
  // ... (complete forwarding for all methods)

 protected:
  VideoFrameRepresentationWrapper(
      std::shared_ptr<const VideoFrameRepresentation> source,
      ArtifactID id, Provenance prov);

  std::shared_ptr<const VideoFrameRepresentation> source_;
  std::optional<SourceParameters> cached_video_params_;
};
```

The hint semantics are unchanged: hints describe the **output** of a stage, not the input. A stage that corrects dropouts must return empty dropout hints. A stage that changes geometry must update `SourceParameters`.

### 4.6 Frame-Native Stage Design

All stages (source, transform, and sink) must be designed to reason natively at the frame level. Providing field-extraction utility functions would preserve field-centric thinking in a frame-based system — the opposite of the design goal.

The VFrameR `get_line(FrameID, line)` method provides direct access to any frame line by its 0-based index. The CVBS frame layout is `[field 1 lines][field 2 lines]` sequentially, so any stage that needs to distinguish the two fields does so by addressing the correct line range directly:

- Field 1 occupies frame lines `0` to `field1_height − 1`.
- Field 2 occupies frame lines `field1_height` to `frame_height − 1`.
- `field1_height` for PAL is 313; for NTSC it is 262; for PAL_M it is 262.

The per-line sample count for PAL (1135 or 1136) is a property the VFrameR implementation encapsulates. Consumers that need the exact sample count for a given line query `get_descriptor(FrameID)` for `samples_total` and `frame_height`, or rely on the implementation returning exactly the right number of samples from `get_line()`. Stages must not assume a fixed 1135 samples for every PAL line.

Concrete implications for each stage currently using field-level logic:

| Stage | Frame-native design |
|-------|-------------------|
| `dropout_correct` | Dropout runs are already frame-flat (`DropoutRun.sample_start`). Correction iterates over samples in the flat frame buffer directly — no field decomposition needed. |
| `field_invert` | Becomes `frame_field_swap`: re-orders the two field line blocks within the output frame. Operates on frame line ranges. |
| `field_map` | Re-orders frames or swaps field halves within frames using frame line ranges. No field-extraction layer needed. |
| `mask_line` | Addresses lines by 0-based frame line index. The caller specifies a frame line; the stage zeroes its samples. |
| `stacker` | Accumulates and averages frame sample buffers. No field decomposition. |
| `source_align` | Shifts frame sequences. No field decomposition. |

The chroma decoder (`SourceField`, `ChromaSinkStage`) is addressed separately in §8, where `SourceField` is refactored to carry frame-line ranges rather than copies of field data.

### 4.7 SourceParameters Updates

`SourceParameters` in `orc/view-types/orc_source_parameters.h` changes from field-centric to frame-centric geometry. The signal level fields change from TBC-derived 16-bit scaled IRE to spec-defined 10-bit values:

**Removed fields:**
- `field_width`, `field_height` — replaced by frame geometry
- `number_of_sequential_fields` — replaced by `number_of_sequential_frames`
- `is_first_field_first` — removed; TBC source stages are responsible for ensuring correct field ordering during frame assembly, so all frames entering the pipeline always have field 1 first. CVBS sources are already correct by definition of the CVBS file format. No per-frame ordering flag is needed.
- `blanking_16b_ire`, `black_16b_ire`, `white_16b_ire` — replaced by normative 10-bit levels

**Added fields:**
- `frame_width_nominal` — samples per line, nominal (1135 PAL / 910 NTSC / 909 PAL_M)
- `frame_height` — lines per frame (625 / 525)
- `number_of_sequential_frames` — replaces field count
- `sync_tip_level` — 10-bit sync tip level (4 PAL / 16 NTSC/PAL_M); populated by source stages from `cvbs_signal_constants.h`
- `blanking_level` — 10-bit blanking level (256 PAL / 240 NTSC/PAL_M); populated by source stages from `cvbs_signal_constants.h`
- `black_level` — 10-bit black level; populated by source stages from `cvbs_signal_constants.h`; may be replaced by `VideoParamsStage` (e.g. NTSC-J)
- `white_level` — 10-bit white level; populated by source stages from `cvbs_signal_constants.h`; may be replaced by `VideoParamsStage`
- `peak_level` — 10-bit peak level (1019 PAL / 988 NTSC/PAL_M); populated by source stages from `cvbs_signal_constants.h`
- `active_video_start` — first active sample on each line (0-based); **retained** from current struct; population changes from TBC metadata to spec constants from `cvbs_signal_constants.h`; may be replaced by `VideoParamsStage` for horizontal cropping
- `active_video_end` — last active sample on each line (0-based, exclusive); **retained** from current struct; population changes from TBC metadata to spec constants from `cvbs_signal_constants.h`; may be replaced by `VideoParamsStage` for horizontal cropping
- `has_nonstandard_values` — bool, read from CVBS metadata `has_nonstandard_values` field; also set true by `VideoParamsStage` when any level is replaced

**SourceParameters population contract**: source stages always populate every `SourceParameters` field with a valid value before the representation is exposed to the pipeline. `VideoParamsStage` may then replace specific field values with user-configured overrides.

**Which value downstream stages must use** depends on their role — this is a first-class design contract:

| Stage role | Levels (`black_level`, `white_level`) | Geometry (`active_video_*`, `first/last_active_frame_line`) |
|---|---|---|
| Signal processing (chroma decoder internals, burst analysis, SNR, vectorscope) | `cvbs_signal_constants.h` — always spec, never overridden | `cvbs_signal_constants.h` — always spec |
| Output and presentation (FFmpeg sink, raw sink, CVBS sink, preview renderer) | `SourceParameters` — respects user overrides | `SourceParameters` — respects user overrides |
| Chroma decoder output luma scaling | `SourceParameters` — user override controls the decoded result | `SourceParameters` — user override controls decode window |

The practical implication: if a user sets a `black_level` override, the FFmpeg output backend scales luma to that level, but the SNR observer still measures against the spec-defined black level. These are correct independent behaviours — the user changed the presentation, not the signal.

`signal_state` is **not** carried in `SourceParameters`. Decode-Orc v2.0 accepts only `STANDARD_TBC_LOCKED` input. Every source stage enforces this as a hard precondition: any CVBS file whose `signal_state_preset` is not `STANDARD_TBC_LOCKED` is rejected at open time with a clear error. The contract for all downstream code — chroma decoders, analysis tools, display tools — is that the internal VFrameR always carries 4fsc, TBC-applied, burst-locked data. This constraint is invariant; it does not need to be propagated as a runtime flag.

**Also removed** (spec-defined constants for the declared video system — derivable from `system` via `cvbs_signal_constants.h`; no value in carrying them as runtime fields):
- `is_subcarrier_locked` — always `true` under the `STANDARD_TBC_LOCKED` invariant; meaningless as a flag
- `colour_burst_start`, `colour_burst_end` — burst sample range on the line back porch; normative for 4fsc per EBU Tech. 3280-E Table 2 (PAL) and SMPTE 244M-2003 Table 3 (NTSC/PAL_M)
- `first_active_field_line`, `last_active_field_line` — spec-defined active line range in field-relative addressing; field-relative coordinates are eliminated in the frame-native design
- `sample_rate` — always exactly 4×fsc for the declared system (PAL: 17,734,475 Hz; NTSC: 14,318,182 Hz; PAL_M: 14,320,000 Hz); derivable from `system`
- `fsc` — normative constant per system (PAL: 4,433,618.75 Hz per EBU Tech. 3280-E §1.1; NTSC: 3,579,545.45 Hz per SMPTE 244M-2003 §3.1; PAL_M: 3,579,611.49 Hz per ITU-R BT.1700-1); derivable from `system`

**Retained fields:**
- `system` — primary dispatch enum (PAL / NTSC / PAL_M); not derivable from any other single parameter
- `is_widescreen` — widescreen aspect ratio flag; content property, not spec-defined
- `first_active_frame_line`, `last_active_frame_line` — equal to spec-defined defaults when `active_area_cropping_applied = false`; represent the actual active window when cropping has been applied
- `is_mapped` — pipeline state flag indicating a frame map stage has been applied
- `tape_format` — source tape format string (e.g., "VHS", "S-VHS", "Betamax", "LaserDisc"); source provenance metadata
- `decoder`, `git_branch`, `git_commit` — decoder tool identity and version; source provenance metadata
- `active_area_cropping_applied` — pipeline state flag; governs interpretation of `first_active_frame_line`/`last_active_frame_line`

---

## 5. Source Stage Changes

### 5.0 Source Stage Display Names

All source stages must carry a clear, unambiguous user-facing display name following the pattern:

```
<VideoSystem> <SourceType> <SignalType>
```

where:
- **VideoSystem**: `PAL`, `NTSC`, or `PAL-M` (hyphen in display text; underscore in identifiers)
- **SourceType**: `CVBS` (reading a processed CVBS file) or `TBC` (reading an ld-decode / vhs-decode TBC file)
- **SignalType**: `Composite` (single-channel composite) or `YC` (separate luma/chroma)

| Internal plugin ID | Display name | §Coverage |
|-------------------|--------------|-----------|
| `cvbs_source` (PAL composite) | `PAL CVBS Composite` | §5.1 |
| `cvbs_source` (PAL YC) | `PAL CVBS YC` | §5.1 |
| `cvbs_source` (NTSC composite) | `NTSC CVBS Composite` | §5.1 |
| `cvbs_source` (NTSC YC) | `NTSC CVBS YC` | §5.1 |
| `cvbs_source` (PAL-M composite) | `PAL-M CVBS Composite` | §5.1 |
| `tbc_source` (PAL composite) | `PAL TBC Composite` | §5.2 |
| `tbc_source` (PAL YC) | `PAL TBC YC` | §5.2 |
| `tbc_source` (NTSC composite) | `NTSC TBC Composite` | §5.3 |
| `tbc_source` (NTSC YC) | `NTSC TBC YC` | §5.3 |
| `tbc_source` (PAL-M composite) | `PAL-M TBC Composite` | §5.4 |

**CVBS source naming**: the `cvbs_source` plugin uses a single plugin identifier for all CVBS input formats. The display name is resolved at load time from the `.meta` file: the `video_standard_preset` field determines `PAL` / `NTSC` / `PAL-M`, and the presence of a `.y`/`.c` file pair vs. a `.composite` file determines `YC` vs. `Composite`. The resolved display name is set on the stage instance and shown in the pipeline view and project YAML stage label.

**TBC source naming**: the `tbc_source` plugin uses a single plugin identifier for all TBC input formats, following the same pattern as `cvbs_source`. The display name is resolved at load time from the `.tbc.json.db` metadata: the video system field determines `PAL` / `NTSC` / `PAL-M`, and the presence of a `.tbc`/`.chroma.tbc` file pair vs. a single `.tbc` file determines `YC` vs. `Composite`. Internally, the plugin dispatches to format-specific conversion classes (PAL, NTSC, PAL-M) to handle the differing frame assembly and level-mapping logic, but this is an implementation detail hidden behind the unified plugin ID. The existing separate plugin identifiers (`pal_tbc_comp_source`, `pal_tbc_yc_source`, `ntsc_tbc_comp_source`, `ntsc_tbc_yc_source`, `pal_m_tbc_comp_source`) are retired.

**Project type enforcement**: a project's `video_format` (PAL / NTSC / PAL-M) and `source_type` (Composite / YC) together define its type. Only source stages whose video system **and** signal type both match the project may be added. Composite and YC source stages may not coexist in the same project, just as PAL and NTSC sources may not. The full enforcement contract — covering project creation, stage addition, project load validation, and runtime assertion — is specified in §12.3.

### 5.1 CVBS Source Stage

#### 5.1.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/plugins/stages/cvbs_source/cvbs_source_stage.h` | Stage class hierarchy (`FixedFormatCVBSSourceStage`, `PALCVBSSourceStage`, `NTSCCVBSSourceStage`) |
| `orc/plugins/stages/cvbs_source/cvbs_source_stage.cpp` | `CVBSDecodedFieldRepresentation` inner class; sample encoding normalisation |

The CVBS source already decodes frame-at-a-time and currently exposes fields by splitting the normalised frame. The inner class `CVBSDecodedFieldRepresentation` must be replaced with `CVBSDecodedFrameRepresentation` implementing `VideoFrameRepresentation`.

#### 5.1.2 Required Changes

1. Replace `CVBSDecodedFieldRepresentation` with `CVBSDecodedFrameRepresentation`. Frame navigation uses `FrameID`; frame cache keys on `FrameID` instead of field pairs.

2. **Signal state enforcement**: the CVBS source must read `signal_state_preset` from the `.meta` file and **reject** the file with a hard error if the value is not `STANDARD_TBC_LOCKED`. No partial-open, no display-only fallback. The user must supply a properly processed CVBS file. This is a non-negotiable precondition for all downstream code.

3. **No resampling**: the current CVBS source resamples the 4FSC signal to match the TBC format before exposing it. This step is entirely removed. The internal representation is now native 4FSC (`CVBS_U10_4FSC`); CVBS files are already at 4FSC and require no spatial resampling of any kind.

4. **Sample encoding normalisation**: the source accepts any of the four defined CVBS sample encodings and translates them to `CVBS_U10_4FSC` before exposing samples through VFrameR. The `signal_state_preset` check (item 2) is performed first; normalisation only happens for accepted files.
   - `CVBS_U10_4FSC`: identity (file already stores int16 in the 10-bit domain)
   - `CVBS_U16_4FSC`: `value_10bit = uint16_value / 64`
   - `CVBS_TPG21_4FSC`: `value_10bit = int16_value / 64 + 508`
   - `CVBS_S16_FSC`: `value_10bit = int16_value / 32 + blanking_10bit` where `blanking_10bit` is 256 (PAL) or 240 (NTSC/PAL_M)
   - `RAW_S16_28M`, `RAW_S16_40M`: these raw-capture encodings are inherently incompatible with `STANDARD_TBC_LOCKED` and will be caught by the signal state check above. No special handling required.

5. **Dropout sidecar**: load dropout data from `<basename>.dropouts.meta` SQLite file per the [dropout extension format](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md). Populate `DropoutRun` entries for `get_dropout_hints(FrameID)`.

6. **Audio**: load `<basename>_audio_00.wav` (WAV sidecar) if present and read `audio_locked` from the `cvbs_file` metadata table. Expose via `audio_locked()` and `has_audio()`:
   - `audio_locked = TRUE`: `audio_locked()` returns `true`; per-frame access via `get_audio_samples(FrameID)` is valid. PAL: 1764 stereo samples/frame at 44100 Hz; NTSC/PAL_M: 1470 stereo samples/frame at 44100000/1001 Hz.
   - `audio_locked = FALSE`: `audio_locked()` returns `false`; `has_audio()` is `true` but `get_audio_samples(FrameID)` returns empty. The WAV stream is available for full-stream passthrough to audio sinks.
   - `audio_locked = NULL` or no WAV present: `has_audio()` returns `false`.

7. **Video format presets supported**: `PAL`, `NTSC`, `PAL_M`. The existing `PALCVBSSourceStage` and `NTSCCVBSSourceStage` classes may be expanded to include `PALMCVBSSourceStage`, or the format may be read from metadata and dispatched at runtime.

8. **NTSC-J**: when the CVBS metadata `.meta` has a non-NULL `black_level` field and the video system is NTSC, populate `FrameDescriptor.black_level_override` with that value (in the 10-bit domain of the declared sample encoding preset, converted to `CVBS_U10_4FSC` domain at load time).

### 5.2 PAL TBC Source Stage

#### 5.2.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/plugins/stages/pal_tbc_comp_source/pal_tbc_comp_source_stage.h` | PAL composite source |
| `orc/plugins/stages/pal_tbc_yc_source/pal_tbc_yc_source_stage.h` | PAL YC source |
| `orc/core/tbc_source_internal/tbc_video_field_representation.h` | Composite TBC VFR implementation |
| `orc/core/tbc_source_internal/tbc_yc_video_field_representation.h` | YC TBC VFR implementation |
| `orc/core/tbc_source_internal/tbc_audio_efm_handler.h` | Audio/EFM handler (44.1 kHz PCM) |

The TBC source reads padded fields (313 lines for each PAL field in TBC format). The VFR removes the padding to give 312+313 unpadded lines. The new TBC source must convert these to a CVBS_U10_4FSC frame with exactly 709,379 samples.

#### 5.2.2 TBC to CVBS_U10_4FSC Level Mapping (PAL)

The TBC signal levels are defined in the `.tbc.json.db` metadata: `blanking_16b_ire`, `black_16b_ire`, `white_16b_ire`. These map to TBC 16-bit sample values. The conversion to EBU 3280-E CVBS_U10_4FSC 10-bit domain is a linear mapping:

```
// EBU Tech. 3280-E normative PAL levels in CVBS_U10_4FSC domain
const int32_t kCvbsPalBlanking = 256;
const int32_t kCvbsPalWhite    = 844;

// Linear level mapping
int16_t tbc_to_cvbs_pal(uint16_t tbc_sample,
                         int32_t tbc_blanking, int32_t tbc_white) {
  // Scale from TBC [tbc_blanking .. tbc_white] to CVBS [256 .. 844]
  double normalised = static_cast<double>(tbc_sample - tbc_blanking)
                      / static_cast<double>(tbc_white - tbc_blanking);
  double cvbs = normalised * (kCvbsPalWhite - kCvbsPalBlanking) + kCvbsPalBlanking;
  // Preserve headroom: do NOT clamp to [4, 1019]; allow signed range
  return static_cast<int16_t>(std::round(cvbs));
}
```

#### 5.2.3 PAL Frame Structure Assembly

Converting two unpadded TBC PAL fields to a CVBS_U10_4FSC frame. The correct mechanism for bridging the TBC and 4FSC sample counts is **sample insertion at the spec-defined positions** — not resampling. The native 4FSC sample grid is authoritative; the TBC format omits the non-orthogonal samples that 4FSC requires.

1. **Input**: field 1 (312 lines × 1135 samples = 354,120 samples) + field 2 (313 lines × 1135 samples = 355,255 samples) = 709,375 samples total.
2. **CVBS requirement**: 709,379 samples (EBU Tech. 3280-E normative).
3. **Difference**: 4 extra samples must be inserted at the spec-defined positions (see below) — no resampling of the signal is performed.
4. **Insertion positions** (EBU Tech. 3280-E §1.3.1 non-orthogonal structure): the 4 extra samples are inserted at the end of the following 0-indexed field lines:
   - Field 1, line 155 → becomes 1136 samples
   - Field 1, line 311 → becomes 1136 samples
   - Field 2, line 156 → becomes 1136 samples
   - Field 2, line 312 → becomes 1136 samples
5. **Inserted sample value**: linear interpolation between the last sample of the standard line and the first sample of the next line. This minimises chroma subcarrier phase distortion. The interpolated value must be computed in the CVBS_U10_4FSC domain **after** level mapping (not before).
6. **Frame layout**: `[field 1: 709,379-field2_samples samples][field 2: remaining samples]`. For PAL, field 1 has 313 lines (first field per the CVBS convention based on existing source code), field 2 has 312 lines.

**Note on field ordering**: The CVBS source code shows `first_field_lines = 313` for PAL, meaning field 1 is the 313-line field. The VFR convention was `first field = 312 lines (odd)`. These must be reconciled against the EBU 3280-E convention and documented clearly in the implementation.

#### 5.2.4 PAL Colour Frame Sequence Tracking

The TBC metadata provides `field_phase_id` (mapped from `FieldPhaseHint`). Map this to `FrameDescriptor.colour_frame_index` (1–4 for PAL):
- Phase IDs 0–7 in the TBC metadata correspond to the 8-phase PAL subcarrier sequence.
- Even-numbered phase IDs correspond to colour frames 1–4 (two fields per frame, 8 phases total = 4 colour frames).
- Exact mapping must be verified against ld-decode metadata conventions and the EBU 3280-E §1.1.1 colour frame progression.

#### 5.2.5 Audio: TBC PCM → CVBS Frame-Locked Audio

The `.pcm` sidecar stores 44.1 kHz 16-bit stereo interleaved raw PCM. TBC sources always write `audio_locked = TRUE` because the TBC format synchronises audio to the video frame clock.

- **PAL**: the frame-locked rate is exactly 44100 Hz (1764 samples/frame × 25 fps). The TBC `.pcm` is already at this rate — no resampling is required. Segment the PCM into blocks of 1764 stereo pairs per frame.
- **NTSC / PAL_M**: the frame-locked rate is 44100000/1001 Hz ≈ 44055.944 Hz (1470 samples/frame × 30000/1001 fps). The TBC `.pcm` at 44100 Hz must be very slightly resampled to this rate using SoXR. After resampling, segment into blocks of 1470 stereo pairs per frame.
- Resampling must be performed lazily per-frame (not pre-processing the whole file).
- `audio_locked()` returns `true` for all TBC sources.

#### 5.2.6 EFM and AC3 RF Frame Association

EFM t-values and AC3 RF symbols were previously associated per-field. The new association is per-frame:
- EFM samples per frame ≈ 2 × (previous samples per field).
- The `TBCAudioEFMHandler` must be updated to provide frame-level access, merging two consecutive field reads.

### 5.3 NTSC TBC Source Stage

#### 5.3.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/plugins/stages/ntsc_tbc_comp_source/ntsc_tbc_comp_source_stage.h` | NTSC composite source |
| `orc/plugins/stages/ntsc_tbc_yc_source/ntsc_tbc_yc_source_stage.h` | NTSC YC source |

Same internal TBC implementation as PAL. Different geometry constants.

#### 5.3.2 TBC to CVBS_U10_4FSC Level Mapping (NTSC)

Same linear mapping approach as PAL (§5.2.2) using SMPTE 244M-2003 CVBS levels:

```
kCvbsNtscBlanking = 240
kCvbsNtscWhite    = 800
```

#### 5.3.3 NTSC Frame Structure Assembly

NTSC is orthogonal. 525 × 910 = 477,750 samples per frame — exactly matching the CVBS normative size. No extra sample insertion is required.

Field ordering: field 1 (262 lines / even-field per SMPTE 244M-2003) + field 2 (263 lines). After removing TBC padding (both stored as 263 lines), the CVBS frame has:
- Field 1: 262 lines
- Field 2: 263 lines

Frame layout: `[field 1: 262 × 910 = 238,420 samples][field 2: 263 × 910 = 239,330 samples]` = 477,750 total.

#### 5.3.4 NTSC Colour Frame Sequence Tracking

SMPTE 244M-2003 §3.2: NTSC 2-frame sequence (A/B). Map TBC `FieldPhaseHint.phase_id` to `FrameDescriptor.colour_frame_index` (0 = frame A, 1 = frame B).

### 5.4 PAL_M TBC Source Stage

PAL_M uses 525-line / 29.97 Hz timing with PAL colour modulation. 525 × 909 = 477,225 samples per frame exactly. Level table identical to NTSC/PAL_M (blanking=240, black=252, white=800). Colour sequence: 4-frame cycle per ITU-R BT.1700-1 Annex 1 Part B.

---

## 6. Dropout Representation

### 6.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/core/metadata/dropouts.h` | `DropoutRegion` struct: field-line-sample coordinates |
| `orc/plugins/stages/dropout_map/dropout_map_stage.h` | Reads dropout hints from VFR, maps to observations |
| `orc/plugins/stages/dropout_correct/dropout_correct_stage.h` | Applies dropout correction using `DropoutRegion` |
| `orc/plugins/stages/dropout_analysis_sink/dropout_analysis_sink_stage.h` | Accumulates dropout statistics |
| `VideoFieldRepresentation::get_dropout_hints(FieldID)` | Returns field-relative dropouts |

### 6.2 DropoutRun — New Internal Type

Aligned to the [CVBS dropout extension format](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md):

```cpp
// orc/core/metadata/dropouts.h — replaces DropoutRegion
struct DropoutRun {
  FrameID frame_id;        // 0-based frame index within capture
  uint64_t sample_start;   // 0-based sample offset within frame flat layout
  uint32_t sample_count;   // Number of consecutive affected samples
  uint8_t severity;        // 0–100 producer confidence/severity percentage
                           // (aligned to dropout-extension-format.md §severity)
};
```

`DropoutRegion` (field-line-sample) is retained for internal use by stages that still reason field-by-field, but is no longer part of any public interface. Conversion utilities are provided.

### 6.3 Frame-Flat to Field-Line Conversion Utilities

```cpp
// orc/core/metadata/dropout_util.h
namespace dropout_util {
  struct FieldLineSample { int32_t field; int32_t line; int32_t sample; };

  FieldLineSample frame_sample_to_field_line(
      VideoSystem system, uint64_t frame_sample_offset);

  uint64_t field_line_to_frame_sample(
      VideoSystem system, int32_t field, int32_t line, int32_t sample_within_line);
}
```

### 6.4 Dropout Sidecar I/O

- **CVBS source**: loads `.dropouts.meta` SQLite file at stage initialisation; populates `DropoutRun` vectors indexed by `FrameID`.
- **TBC source stages**: converts TBC `.tbc.json.db` per-field per-line dropouts to `DropoutRun` using `dropout_util::field_line_to_frame_sample()`.
- **ld-decode sink**: converts `DropoutRun` back to per-field per-line format for TBC metadata output.
- **CVBS sink** (new, §10): writes `.dropouts.meta` directly from `DropoutRun` data.

---

## 7. Preview and Viewing Tools

### 7.1 Preview Renderer

#### 7.1.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/core/include/preview_renderer.h` | `PreviewRenderer` — field-based rendering |
| `orc/core/include/dag_field_renderer.h` | `DAGFieldRenderer` — single-field rendering for GUI |
| `orc/gui/previewdialog.h` | Preview dialog |
| `orc/gui/fieldpreviewwidget.h` | Preview display widget |
| `orc/view-types/orc_preview_types.h` | `PreviewImage`, `PreviewRenderResult` |
| `orc/core/include/preview_renderer.h:422` | `tbc_sample_to_8bit()` — uses hardcoded IRE conversion |

The method `tbc_sample_to_8bit(uint16_t sample, double blackIRE, double whiteIRE)` uses TBC-domain scaling and must be replaced.

#### 7.1.2 Required Changes

1. **DAGFieldRenderer → DAGFrameRenderer**: navigates by `FrameID`, returns `VideoFrameRepresentationPtr`.

2. **PreviewOutputType changes**:
   - `Field` → `Frame_Field1` (single field within a frame: always field 1)
   - `Frame_Field2` (single field within a frame: always field 2)
   - `Frame_EvenOdd` → `Frame_Field1_First` (interlaced weave, field 1 on even rows)
   - Retain `Split` (both fields stacked vertically)
   - The notion of navigating individual fields by field index is removed; frames are the navigation unit.

3. **Sample-to-pixel mapping** — replace `tbc_sample_to_8bit()` with a spec-derived function:
   ```cpp
   // Maps CVBS_U10_4FSC int16 sample to 8-bit greyscale
   // Uses normative blanking and white levels from SourceParameters
   uint8_t cvbs_sample_to_8bit(int16_t sample,
                                int32_t blanking_level, int32_t white_level) {
     // Linear: blanking → 0, white → 255; headroom preserved by clamping display output
     double normalised = static_cast<double>(sample - blanking_level)
                         / static_cast<double>(white_level - blanking_level);
     return static_cast<uint8_t>(std::clamp(std::round(normalised * 255.0), 0.0, 255.0));
   }
   ```
   Note: clamping is only for the 8-bit display output. The underlying int16 sample is not clamped.

4. **Dropout rendering**: `DropoutRun.sample_start` and `.sample_count` are frame-flat. To render onto an image, convert to (line, start_sample, end_sample) using `dropout_util::frame_sample_to_field_line()`.

5. **Frame coordinate mapping**: `navigate_frame_line()`, `map_image_to_field()`, `map_field_to_image()`, and `get_frame_fields()` must all be updated to use `FrameID` and 0-based frame line indices.

### 7.2 Frame Scope (replaces Field Scope)

#### 7.2.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/gui/linescopedialog.h` | `LineScopeDialog` — currently acts as the field scope: navigates by `field_index` + `line_number`, displays one line's waveform |
| `setLineSamples()` | Receives `field_index` + `line_number`, `std::vector<uint16_t>` |
| IRE conversion | `7 mV/IRE PAL, 7.143 mV/IRE NTSC` — hardcoded in dialog |

The `LineScopeDialog` name is misleading: the dialog's navigation is field-based (`field_index` is its primary state). The migration replaces field-based navigation with frame-based navigation; the underlying function — displaying a single line's waveform — does not change. The line scope concept is intact; only the frame of reference for navigation changes.

The mV conversion factors must be derived from the signal levels, not hardcoded:
- PAL: 700 mV / (white_level − blanking_level) mV per 10-bit step (ITU-R BT.1700-1 defines 700 mV peak-to-peak for active video)
- NTSC: 714.3 mV / (white_level − blanking_level) mV per 10-bit step (SMPTE 170M-2004 §11.4)

#### 7.2.2 Required Changes

1. **Rename** `LineScopeDialog` → `FrameScopeDialog`.

2. **Navigation** changes from `(field_index, line_number)` to `(frame_id, frame_line)`:
   - `frame_id`: `FrameID` (0-based frame index)
   - `frame_line`: 0-based frame line (0 to height−1)
   - Navigation signals updated accordingly

3. **Sample type** changes from `std::vector<uint16_t>` to `std::vector<int16_t>`.

4. **Millivolt conversion** using spec-derived factors:
   ```cpp
   double cvbs_sample_to_mv(int16_t sample,
                             int32_t blanking_level, int32_t white_level,
                             VideoSystem system) {
     // ITU-R BT.1700-1 / SMPTE 170M-2004 active video amplitude
     const double active_mv = (system == VideoSystem::PAL) ? 700.0 : 714.3;
     return static_cast<double>(sample - blanking_level)
            / static_cast<double>(white_level - blanking_level) * active_mv;
   }
   ```

5. **Reference level markers**: add horizontal reference lines at all five normative level values (§3.2) labelled with their standard names and mV values.

6. **PAL variable line length**: for PAL frames, the selected line may have 1135 or 1136 samples. The dialog must display the correct sample count and not assume a fixed width.

7. **Headroom display**: the Y-axis must extend below 0 mV (to sync tip and below) and above 100 IRE (to peak and beyond). No arbitrary clamping of the display range.

8. **Line numbering mode selector**: a drop-down or radio button group allowing the user to choose how the selected line is identified in the dialog. Four modes (see §14.13 for conversion formulae and rationale):

   | Mode | Example (PAL line 313 internally) | Use case |
   |------|-----------------------------------|----------|
   | Frame-flat 0-based | `Line 0` … `Line 624` | Matches CVBS format, dropout coordinates, internal frame buffer |
   | Frame-sequential 1-based | `Line 1` … `Line 625` | Human-readable equivalent of above; default display mode |
   | Field-relative | `Field 1, Line 1` … `Field 2, Line 312` | Familiarity with legacy TBC field convention |
   | Broadcast interlaced | `Line 1` … `Line 625` (interlaced order) | VBI specification lookup (ITU-R BT.470-6 / SMPTE 170M-2004) |

   The internal state (`frame_id`, `frame_line`) is always 0-based and does not change when the display mode is switched. Only the label shown to the user changes. The conversion logic belongs in a shared utility (see §14.13) used by the scope dialog, timing dialog, and preview hover display.

### 7.3 Frame Timing Dialog (replaces Field Timing Dialog)

#### 7.3.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/gui/fieldtimingdialog.h` | Field timing analysis dialog |

#### 7.3.2 Required Changes

1. **Rename** `fieldtimingdialog` → `frametimingdialog`.

2. Display frame-level timing:
   - Colour frame sequence position (`colour_frame_index` from `FrameDescriptor`)
   - Both fields' line counts within the frame
   - Frame rate and video system (PAL / NTSC / PAL_M)

3. **PAL sequence display**: Show 4-frame cycle position (frames 1–4 per EBU Tech. 3280-E §1.1.1).

4. **NTSC sequence display**: Show 2-frame cycle position (A or B per SMPTE 244M-2003 §3.2).

5. **Line numbering mode**: any line references shown in the timing dialog (e.g. cursor position, region boundaries) must use the same line numbering mode selected in `FrameScopeDialog`, or expose the same mode selector independently. The conversion utility described in §14.13 is shared between both dialogs and the preview hover display.

### 7.4 Vectorscope

#### 7.4.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/core/analysis/vectorscope/vectorscope_analysis.h` | `VectorscopeAnalysisTool` |
| `orc/gui/preview/vectorscope_dialog.h` | Vectorscope dialog |
| `orc/gui/preview/vectorscope_geometry.h` | Vectorscope geometry — contains constants that may be magic numbers |
| `extractFromCompositeRepresentation()` | Demodulates composite VFR samples |
| `extractFromComponentFrame()` | Extracts U/V from decoded component output |

#### 7.4.2 Demodulation Constants — All Must Be Spec-Derived

All vectorscope demodulation constants must be derived from `cvbs_signal_constants.h` (§3.4). No hardcoded values.

**PAL subcarrier demodulation** (EBU Tech. 3280-E §1.2):
```
// The CVBS_U10_4FSC PAL sampling lattice has exactly 4 samples per subcarrier period.
// Sampling phase: 45°, 135°, 225°, 315° relative to +U axis.
// Consequently, for sample n:
//   U component ∝ sample[n] × cos(phase[n])
//   V component ∝ sample[n] × sin(phase[n])
// where phase[n] = (n % 4) × 90° + 45° (EBU 3280-E §1.2)
//
// The PAL V-axis alternates sign every line (the PAL alternating V-axis):
//   V_effective = V × (line % 2 == 0 ? +1 : −1)
```

**NTSC subcarrier demodulation** (SMPTE 244M-2003 §4.1):
```
// NTSC 4fsc: orthogonal, 910 samples/line, 4 samples per subcarrier period.
// Reference: sample 0 of frame line 10 (0-based: line 9) in colour frame A
// is an I-axis (+123°) sample (SMPTE 244M-2003 §4.1.2).
// I/Q axes per SMPTE 170M-2004 §11.3.
```

**Reference vectors for colour bar targets**:
```
// Derived from BT.601-5 matrix and 75% amplitude, 100% saturation colour bars
// per ITU-R BT.601-5 Table 2.
// Exact U, V values for each colour: Yellow, Cyan, Green, Magenta, Red, Blue
// must be calculated from the spec matrix — NOT from empirical measurements.
```

The computation for each reference colour:
```
R, G, B → (R′ − Y) = R − 0.2627R − 0.6780G − 0.0593B [BT.709] or BT.601 as appropriate
U = (B − Y) / 2Umax
V = (R − Y) / 2Vmax
```
The exact formula depends on whether PAL uses BT.601 or BT.709 primaries for the colour bars being represented. This must be stated explicitly in the implementation with specification citations.

#### 7.4.3 Frame-Level Operation

`extractFromCompositeRepresentation()` currently takes VFR and operates on individual fields. With VFrameR:
- Accepts `VideoFrameRepresentation` and a `FrameID`
- Processes both fields of the frame
- The field-specific V-axis sign alternation for PAL is handled at the frame level

#### 7.4.4 vectorscope_geometry.h Audit

Every numeric constant in `vectorscope_geometry.h` must be traced to its specification source or derived from `cvbs_signal_constants.h`. Constants that cannot be traced must be flagged and resolved before implementation.

---

## 8. Chroma Decoder

### 8.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/plugins/stages/sinks/common/chroma_sink_stage.h` | `ChromaSinkStage` — consumes VFR |
| `orc/plugins/stages/sinks/common/decoders/sourcefield.h` | `SourceField` — field data container |
| `orc/plugins/stages/sinks/common/decoders/decoder.h` | Abstract `Decoder` base; `configure(SourceParameters)` |
| `orc/plugins/stages/sinks/common/decoders/palcolour.h` | `PalColour` decoder; `MAX_WIDTH = 1135` |
| `orc/plugins/stages/sinks/common/decoders/comb.h` | Comb filter components |
| `orc/plugins/stages/sinks/common/decoders/transformpal.h` | Transform PAL 2D/3D |
| `orc/plugins/stages/sinks/common/decoders/componentframe.h` | Decoded component output |
| `ChromaSinkStage::convertToSourceField()` | Builds `SourceField` from VFR field |

### 8.2 SourceField Refactoring — Frame Line Views

`SourceField` was a copy-based container built for the TBC field-pair model. With frame-native stages (§4.6), `ChromaSinkStage` no longer extracts field copies — it gives the decoder a lightweight view into the frame data.

`SourceField` is refactored to hold **non-owning views** (pointers + metadata) into the VFrameR frame buffer:

```cpp
struct SourceField {
  int32_t seq_no = 0;            // 1-based frame sequence number × 2 ± 1 for field ordering
  bool is_first_field = true;    // True if this is the first field (field 1 block in the frame)
  std::optional<int32_t> frame_phase_id; // Colour frame sequence position from FrameDescriptor

  // Non-owning pointers into the VFrameR frame buffer (CVBS_U10_4FSC int16 samples)
  const int16_t* data = nullptr;      // Composite: points to start of field 1 or field 2 lines
  size_t line_count = 0;              // Number of lines in this field view
  size_t samples_per_line = 0;        // Nominal samples per line (1135 PAL / 910 NTSC)

  // YC sources: separate Y and C pointers into their respective frame buffers
  const int16_t* luma_data = nullptr;
  const int16_t* chroma_data = nullptr;
  bool is_yc = false;

  int32_t getOffset() const { return is_first_field ? 0 : 1; }

  // Active line helpers — unchanged semantics, using updated SourceParameters fields
  int32_t getFirstActiveLine(const ::orc::SourceParameters& p) const {
    return (p.first_active_frame_line + 1 - getOffset()) / 2;
  }
  int32_t getLastActiveLine(const ::orc::SourceParameters& p) const {
    return (p.last_active_frame_line + 1 - getOffset()) / 2;
  }
};
```

The frame buffer pointed to by `SourceField.data` is kept alive for the lifetime of the `SourceField` view by the `VideoFrameRepresentationPtr` held by `ChromaSinkStage`. No copy of field samples is made.

Because CVBS frames store fields as contiguous line blocks — field 1 lines followed by field 2 lines — `data` for field 1 points to `get_frame(frame_id)` directly, and `data` for field 2 points to `get_frame(frame_id) + field1_sample_count`. Line access within the decoder is then `data + line × samples_per_line` (for orthogonal formats). For PAL, the non-orthogonal line lengths mean the decoder must not use a fixed stride — see §8.6.

### 8.3 Signal Level Alignment

The decoder uses signal levels in two distinct contexts that must not be conflated:

**Internal signal processing** (sync detection, burst gating, subcarrier phase measurement): must use spec constants from `cvbs_signal_constants.h` directly. These are physical properties of the signal and must not be affected by user-level overrides. Replace all uses of `SourceParameters.blanking_16b_ire` and similar fields in internal signal processing with the appropriate constant (e.g. `kPalBlanking`, `kNtscBlanking`).

**Output luma range scaling** (mapping decoded luma to the ComponentFrame / output domain): must read from `SourceParameters.blanking_level`, `.black_level`, `.white_level`. These fields carry spec defaults but may be replaced by `VideoParamsStage` — the user override controls the decoded output range (e.g. NTSC-J adjustment). All output-path uses of `videoParameters.black_16b_ire` and `white_16b_ire` must be replaced with `videoParameters.black_level` and `white_level` respectively.

### 8.4 PalColour Constant Audit

`PalColour` contains values derived from EBU 3280-E and SMPTE 244M-2003. All of these must be validated against the corresponding specification sections and replaced with references to `cvbs_signal_constants.h`:

- `MAX_WIDTH = 1135` → `kPalMaxSamplesPerLine = 1136` (to handle non-orthogonal lines)
- Any hardcoded burst phase angles must be validated against EBU 3280-E §1.2
- Any hardcoded bandpass filter centre frequencies must be validated against the subcarrier frequencies in §3.4
- Any hardcoded blanking/black/white values in the decoder must be replaced with spec-defined constants

**Required citation format** (per AGENTS.md §5.3.6):
```cpp
// EBU Tech. 3280-E §1.2: sampling at 45°, 135°, 225°, 315° relative to +U axis
// ITU-R BT.1700-1 Annex 1 Part B Table 1 item 1 (625-line PAL): subcarrier frequency
// SMPTE 244M-2003 §4.1.1: NTSC orthogonal 910 samples/line
```

All internal filter arithmetic that previously operated on `uint16_t` samples must be reviewed for correctness with `int16_t` (signed). In particular:
- Shift-right `>> 1` used as division is implementation-defined for negative signed values in C++; replace with `/ 2`.
- Midpoint calculations `(a + b) / 2` are correct for signed arithmetic but must not overflow `int16_t`; promote to `int32_t` before adding.

### 8.5 ChromaSinkStage VFrameR Integration

`convertToSourceField()` is updated to build non-owning `SourceField` views:

1. Accept `VideoFrameRepresentation`, a `FrameID`, and a field index (0 = field 1, 1 = field 2).
2. Get `frame_ptr = vframer.get_frame(frame_id)` — pointer into the frame buffer.
3. Compute `field1_sample_count` from the frame descriptor and the known per-system field 1 line count.
4. Set `SourceField.data = frame_ptr` (field 1) or `frame_ptr + field1_sample_count` (field 2).
5. Set `SourceField.line_count` and `samples_per_line` from the frame descriptor.
6. Set `SourceField.is_first_field` and `frame_phase_id` from `FrameDescriptor`.

No heap allocation occurs for sample data. The VFrameR frame buffer is the single authoritative storage.

The `Decoder::configure()` method receives updated `SourceParameters` with frame geometry and spec-defined signal levels.

### 8.6 PAL Non-Orthogonal Line Handling in Decoder

The Transform PAL filters process lines of samples. For PAL CVBS_U10_4FSC input, specific lines have 1136 samples. Because `SourceField.data` now points directly into the non-uniformly-strided frame buffer, the decoder cannot use a single fixed stride to walk lines.

Two options:

1. **Per-line pointer table** (preferred for cache locality): at `SourceField` construction time, build a `std::array<const int16_t*, kPalFrameLines / 2>` of per-line start pointers by walking the frame buffer with correct cumulative offsets. The decoder indexes lines through this table with no stride arithmetic.

2. **Resampled copy** (simpler decoder, more memory): materialise a copy of the field with 1135 samples on every line (dropping the extra sample on 1136-sample lines, or averaging adjacent samples). This trades a small signal quality loss for simpler decoder code.

Option 1 is the correct choice: it preserves the exact EBU 3280-E non-orthogonal sample values and avoids any additional signal processing. Allocate the per-line pointer table in `ChromaSinkStage::convertToSourceField()` and store it in `SourceField`. Filter state arrays in `PalColour` and `TransformPAL` must be sized to `kPalMaxSamplesPerLine = 1136`.

### 8.7 3D Decoder Look-Behind/Look-Ahead

The 3D Transform PAL decoder uses `getLookBehind()` and `getLookAhead()` to request adjacent frames. With VFrameR, this is expressed in frames rather than fields:
- Current: `getLookBehind()` returns 2 (two fields = one frame behind).
- New: `getLookBehind()` returns 1 (one frame behind).
- The PAL 4-frame colour sequence requires look-behind to respect colour frame alignment.

---

## 9. ld-decode Sink Stage

### 9.1 Preliminary Code Analysis

| File | Role |
|------|------|
| `orc/plugins/stages/ld_sink/ld_sink_stage.h` | `LDSinkStage` — writes TBC + `.tbc.json.db` |
| Input type | `VideoFieldRepresentation` → must become `VideoFrameRepresentation` |

### 9.2 CVBS_U10_4FSC to TBC Level Inverse Mapping

The output TBC metadata must carry the correct `blanking_16b_ire`, `black_16b_ire`, `white_16b_ire`. The convention for ld-decode TBC output is to use a fixed 16-bit scaling where white maps to a known value (typically 54,400 for PAL — but this is implementation-specific). The ld-decode sink must write these level values consistently with what ld-decode expects.

Inverse mapping (CVBS_U10_4FSC 10-bit → TBC 16-bit):
```
tbc_sample = round((cvbs_10bit - kCvbsBlanking) × (tbc_white - tbc_blanking)
                   / (kCvbsWhite - kCvbsBlanking) + tbc_blanking)
```
Where `tbc_blanking` and `tbc_white` are the target TBC output levels (must be documented and justified against the ld-decode format expectations).

### 9.3 Frame to Field Splitting

Extract fields from VFrameR for TBC storage:
- PAL: extract field 1 (313 lines) and field 2 (312 lines) from the CVBS frame.
- Remove the 4 extra PAL samples from the 1136-sample lines to get uniform 1135-sample lines.
- Add TBC padding: both fields stored at 313 lines each (pad field 2 with one blank line).
- NTSC: extract field 1 (262 lines) and field 2 (263 lines); add padding (both stored at 263 lines).

### 9.4 Dropout Conversion

Convert `DropoutRun` (frame-flat) back to per-field per-line format for `.tbc.json.db` using `dropout_util::frame_sample_to_field_line()`.

### 9.5 Audio

The ld-decode TBC format stores 44.1 kHz raw PCM. The VFrameR provides audio at the frame-locked rate.

- **PAL**: frame-locked rate is 44100 Hz — write directly to `.pcm` without resampling.
- **NTSC / PAL_M**: frame-locked rate is 44100000/1001 Hz ≈ 44055.944 Hz — resample to 44100 Hz using SoXR before writing `.pcm`. The quality impact of this tiny rate adjustment is imperceptible.
- **Free-running audio** (`audio_locked() == false`): write the 44100 Hz WAV stream directly to `.pcm` without resampling.

---

## 10. New: CVBS File Format Sink Stage

The existing ld-decode sink is a legacy output. A native CVBS sink stage is the natural complement to the CVBS source and should be included in the v2.0 design:

**`CVBSSinkStage`**: accepts VFrameR (CVBS_U10_4FSC); writes:
- `<basename>.composite` — raw CVBS_U10_4FSC sample data (or `.y`/`.c` for YC)
- `<basename>.meta` — SQLite metadata with `preset`, `sample_encoding_preset = 'CVBS_U10_4FSC'`, `signal_state_preset`, `number_of_sequential_frames`, `decoder = 'cvbs-decode'`, `audio_locked` (TRUE/FALSE/NULL from `audio_locked()` / `has_audio()`)
- `<basename>_audio_00.wav` — WAV sidecar at the source's locked/free-running rate (when `has_audio()` is true)
- `<basename>.dropouts.meta` — SQLite dropout sidecar (when dropout hints present)

Parameters: `output_path`, `signal_type` (composite / yc), `capture_notes`. `signal_state_preset` is always written as `STANDARD_TBC_LOCKED` — it is an invariant of the pipeline and is not a user-configurable parameter.

---

## 11. Transform Stages

### 11.1 Phase and Field Relationship Correction

#### 11.1.1 Background — CVBS Spec Requirements

The CVBS specification ([index.md §Frame Ordering and Sequencing](cvbs-file-format-specification/docs/index.md)) is explicit that no guarantee exists that either ordering dimension is correct in a stored file:

> *A frame may contain the expected pair of fields but with intra-frame field order reversed. Due to the nature of RF capture and physical media characteristics — including disc jumps, scratches, media pauses, physical defects, and frame dropouts — the producer may have no reliable way to verify that captured frame and field ordering maintain proper sequential continuity.*
>
> *If frame-level reordering, field-level analysis, phase verification, or dropout detection is required, those concerns are the responsibility of the consumer application.*

The [video-standard-presets.md §Frame Ordering and Phase Verification](cvbs-file-format-specification/docs/video-standard-presets.md) adds:

> *Position within the colour frame sequence is not encoded in-band; consumers determine sequence position by measuring burst phase and checking that successive frames preserve the expected preset-specific progression.*

This creates two categories of problem, which are intentionally handled by separate stages because they have different causes, different detection methods, and need to be configured independently:

**Signal-level problems** (handled by `FramePhaseCorrectorStage`, §11.1.3):
- **Intra-frame field swap**: the two field blocks within a frame are in the wrong temporal order — field 2 is stored before field 1.
- **Colour frame sequence phase break**: the burst phase progression between consecutive frames does not follow the expected standard pattern, meaning a sequence discontinuity exists in the signal itself.

**Temporal-sequence problems** (handled by `FrameMapStage`, §11.2):
- **Repeated frames**: a player pause causes a frame to be captured more than once in succession.
- **Missing frames**: a player skip or chapter jump leaves a gap that needs to be either excised or padded so downstream stages receive a continuous frame sequence.
- **Manual reordering**: deliberate rearrangement of frame ranges by the user.

#### 11.1.2 Burst Phase Population at Source

Before any phase correction can operate, `FrameDescriptor.colour_frame_index` must be populated. For TBC sources this comes from the `field_phase_id` hint in the metadata. For CVBS sources it must be measured from the colour burst of each frame at load time.

All source stages — CVBS, PAL TBC, NTSC TBC, PAL_M TBC — must populate `FrameDescriptor.colour_frame_index`:
- **PAL**: measure burst phase; determine position within the 4-frame sequence (1–4) per EBU Tech. 3280-E §1.1.1.
- **NTSC**: measure burst phase; determine frame A or B (0 or 1) per SMPTE 244M-2003 §3.2.
- **PAL_M**: measure burst phase; determine position within the 4-frame sequence (1–4) per ITU-R BT.1700-1 Annex 1 Part B.
- Set `colour_frame_index = -1` if burst is absent or unmeasurable.

This is a prerequisite for `FramePhaseCorrectorStage`, the stacker (§11.4), and the 3D chroma decoder (§8.7).

#### 11.1.3 New Stage: `FramePhaseCorrectorStage`

**Responsibility**: signal-level phase and field relationship correction only. This stage does not add, remove, or reorder frames. It corrects what the decoder produced for existing frames.

**Location**: `orc/plugins/stages/frame_phase_corrector/`

The stage wraps the input VFrameR and presents a phase-corrected view.

**Intra-frame field swap correction:**

For each frame, compare the burst phase of the two field blocks against the expected relationship for the declared video standard:
- If the phase relationship is consistent (field 1 has the earlier temporal phase), pass the frame through unchanged.
- If the phases are swapped (field 2 appears temporally first), present the output frame with the two field blocks exchanged in the output VFrameR. The swap is implemented as an index redirect in the frame buffer view — no sample data is copied.
- Update the output `FrameDescriptor.colour_frame_index` to the corrected value derived from the corrected field order.

**Colour frame sequence phase verification:**

Walk the frame sequence. For each frame, compare `colour_frame_index` against the expected next value in the standard progression:
- **Continuous**: pass through unchanged.
- **Break detected** (index does not follow the expected progression):
  - Record the break position and magnitude as an observation.
  - Mark the frame at the break with `colour_frame_index = -1`. Downstream stages must treat a `-1` frame as a phase-sequence reset; the 3D chroma decoder and stacker both respect this marker.
  - Continue processing subsequent frames; they may re-establish a valid sequence from the next measurable burst.

This stage does **not** attempt to reorder frames to resolve breaks. A break simply means the signal had a discontinuity; reordering in this stage would silently change signal timing. Temporal-sequence manipulation is the responsibility of `FrameMapStage` (§11.2).

**Parameters:**
- `correct_field_swap` (bool, default true): detect and correct intra-frame field swap
- `verify_phase_sequence` (bool, default true): verify colour frame sequence continuity and mark breaks

**Observations emitted:**
- `frame_phase_corrector.field_swaps_corrected` — count of frames where field order was swapped
- `frame_phase_corrector.phase_breaks_detected` — count of sequence discontinuities detected
- `frame_phase_corrector.phase_breaks_marked` — count of frames marked with `colour_frame_index = -1`

### 11.2 Frame Sequence Mapping

#### 11.2.1 Background

Player behaviour introduces temporal-sequence problems that are distinct from signal-level phase issues. A player pause causes one or more frames to repeat in the capture. A player skip or chapter jump creates a gap — the capture is missing frames that represent real time. These are not signal errors; the frames themselves have valid phase. The problem is their position in the temporal sequence.

These concerns are separate from phase correction (§11.1) and require a different stage.

#### 11.2.2 `FrameMapStage` (replaces `FieldMapStage`)

The existing `FieldMapStage` (`orc/plugins/stages/field_map/`) is renamed `FrameMapStage` and migrated to VFrameR. It handles all temporal-sequence manipulation, both automatic and manual.

**Location**: `orc/plugins/stages/frame_map/` (renamed from `field_map/`)

**Manual range reordering** (migrated from `FieldMapStage`):

The range specification string (`"0-10,20-30,11-19"`) now addresses 0-based frame indices. The implementation builds a `FrameID → FrameID` lookup table at configuration time; `get_frame()` calls resolve through this table with no sample copying. `FrameDescriptor.colour_frame_index` is preserved from the source frame unchanged — this stage does not re-measure phase.

**Duplicate frame removal** (new):

When `remove_duplicates = true`, compare each consecutive frame pair by `colour_frame_index`:
- If two consecutive frames have the same `colour_frame_index`, the second is a player-pause duplicate. Remove it from the output sequence.
- Continue until the sequence advances to the next expected phase position.
- Emit an observation recording how many frames were removed and at which positions.

**Gap padding** (new):

When a sequence break is detected in the upstream `colour_frame_index` values (a player skip), and `pad_gaps = true`, insert synthetic padding frames into the output sequence to fill the gap:
- **Padding strategy** (parameter `pad_strategy`):
  - `nearest`: repeat the last valid frame for each missing position.
  - `black`: insert a blank frame (all samples at blanking level) for each missing position.
- Padding frames receive `colour_frame_index` values synthesised from the expected sequence progression, so downstream stages (stacker, 3D decoder) see a continuous colour frame sequence.
- Padding frames have `FrameDescriptor.is_padding_frame = true`. Every downstream stage must check this flag before processing a frame's sample data. Stages that perform signal analysis or signal-dependent processing (chroma decoder, SNR, burst analysis, vectorscope, dropout correction) must skip padding frames entirely — their sample content is not real signal. Stages that produce output per frame (output sinks, preview) must handle padding frames gracefully (e.g. output a blank or repeated frame as appropriate).
- Emit an observation recording gap positions and padding counts.

**Parameters:**
- `range_spec` (string): manual range specification, e.g. `"0-10,20-30,11-19"`; empty = pass-through
- `remove_duplicates` (bool, default false): automatically remove player-pause duplicate frames
- `pad_gaps` (bool, default false): automatically pad player-skip gaps with synthetic frames
- `pad_strategy` (enum: `nearest` | `black`, default `nearest`): padding frame source

**Audio handling**: Frame-sequence manipulation (duplicate removal, gap padding) can only process audio when `audio_locked() == true`. When `audio_locked() == false`, the audio stream has no frame-alignment guarantee and cannot be reliably reassembled after frame reordering or padding. When audio is free-running, `FrameMapStage` passes it through unchanged and does not attempt to add or remove audio samples alongside manipulated frames. An observation is emitted if frame manipulation is applied to a source with free-running audio.

**Observations emitted:**
- `frame_map.frames_removed` — count of duplicate frames removed
- `frame_map.frames_padded` — count of synthetic frames inserted
- `frame_map.gap_positions` — list of frame positions where gaps were padded

#### 11.2.3 Recommended Pipeline Ordering

```
[Source Stage] → [FramePhaseCorrectorStage] → [FrameMapStage] → [downstream]
```

Phase correction runs first: it fixes signal-level field and phase problems on the raw source frames. Frame mapping runs second: it adjusts the temporal sequence after the signal content has been made as correct as possible. Both stages are optional; a pipeline may use neither, either, or both depending on the source material.

### 11.3 All Other Wrapper-Pattern Stages

Every stage that currently inherits from `VideoFieldRepresentationWrapper` must be updated to inherit from `VideoFrameRepresentationWrapper`. The forwarding pattern and hint semantics are unchanged.

| Stage | File | Key Change |
|-------|------|-----------|
| `dropout_correct` | `orc/plugins/stages/dropout_correct/` | Internal logic uses `DropoutRun` frame-flat addressing |
| `dropout_map` | `orc/plugins/stages/dropout_map/` | Reads/writes `DropoutRun`; conversion utilities for legacy TBC metadata |
| `field_invert` | `orc/plugins/stages/field_invert/` | Becomes intra-frame field block swap; operates on frame line ranges |
| `stacker` | `orc/plugins/stages/stacker/` | Multi-frame stacking; colour frame sequence alignment (see §11.4 below) |
| `mask_line` | `orc/plugins/stages/mask_line/` | Frame-line indices; PAL variable-length lines |
| `source_align` | `orc/plugins/stages/source_align/` | Frame-based alignment |
| `video_params` | `orc/plugins/stages/video_params/` | See §11.6 |

### 11.4 Stacking and Colour Frame Alignment

The stacker stage must align frames on colour frame sequence position:
- **PAL/PAL_M**: stack frames at the same position in the 4-frame sequence.
- **NTSC**: stack frames at the same position in the 2-frame sequence (A or B).
- Use `FrameDescriptor.colour_frame_index` for alignment decisions.
- When `colour_frame_index == -1` (unknown), fall back to temporal alignment only.

**Audio**: The stacker can only align and average audio samples when `audio_locked() == true`. For locked audio, the stacker averages (or selects from the reference input) the per-frame audio blocks alongside the stacked video frames. When `audio_locked() == false`, audio has no frame-alignment guarantee; the stacker passes audio from the first (reference) input VFrameR through unchanged and emits an observation warning that audio stacking was skipped.

### 11.5 Analysis and Sink Stages

| Stage | Key Change |
|-------|-----------|
| `burst_level_analysis_sink` | Frame-based iteration; use spec-defined burst amplitude references for PAL (EBU 3280-E §1.2) and NTSC (SMPTE 244M-2003 §4.1) |
| `snr_analysis_sink` | Level references change from metadata-derived to spec constants from `cvbs_signal_constants.h` — SNR is a signal measurement against spec, not against user overrides |
| `cc_sink` | VBI line coordinates: convert from field-relative to frame-relative; map to broadcast-standard 1-based line numbers (ITU-R BT.470-6 / SMPTE 170M-2004) |
| `daphne_vbi_sink` | Same VBI coordinate system change |
| `dropout_analysis_sink` | Accumulates `DropoutRun` statistics by frame |
| `audio_sink` | Must check `audio_locked()`: when true, reassemble per-frame audio blocks into a continuous stream; when false, write the free-running 44100 Hz WAV stream. NTSC/PAL_M locked audio arrives internally at 44100000/1001 Hz — resample to 44100 Hz using SoXR before writing the output WAV, as container formats require a standard integer sample rate. |
| `ac3rf_sink` | AC3 symbols from VFrameR are per-frame |

### 11.6 VideoParamsStage

`VideoParamsStage` is a VFrameR wrapper. It does not modify sample data. It produces an output VFrameR whose `get_video_parameters()` returns a modified `SourceParameters` that downstream stages use to determine active regions and signal levels.

#### 11.6.1 Use Cases

- **Level adjustment**: sources that deviate from standard levels (e.g. NTSC-J with a raised black level, or a source where white clips below 844).
- **Horizontal crop**: trim blanking from the left or right edge of each line before decoding or output.
- **Vertical crop**: restrict the active frame area (e.g. exclude VBI lines from the decode window).

#### 11.6.2 Parameters

All parameters are optional. When a parameter is not set (sentinel value -1), the corresponding `SourceParameters` field is inherited from the upstream source unchanged.

| Parameter | Type | Sentinel | Effect |
|-----------|------|----------|--------|
| `black_level` | `int16_t` | -1 | Override `SourceParameters.black_level` in 10-bit domain |
| `white_level` | `int16_t` | -1 | Override `SourceParameters.white_level` in 10-bit domain |
| `active_video_start` | `int32_t` | -1 | Override `SourceParameters.active_video_start` (first active sample on each line) |
| `active_video_end` | `int32_t` | -1 | Override `SourceParameters.active_video_end` (last active sample on each line, exclusive) |
| `first_active_frame_line` | `int32_t` | -1 | Override `SourceParameters.first_active_frame_line` |
| `last_active_frame_line` | `int32_t` | -1 | Override `SourceParameters.last_active_frame_line` |

The upstream `SourceParameters` always carries valid (non-sentinel) values for all fields — source stages populate every field at load time. The user-supplied value unconditionally replaces the upstream value for whichever parameters are configured.

#### 11.6.3 SourceParameters Side Effects

When the stage applies any override it also sets:
- `active_area_cropping_applied = true` — whenever any of the four crop parameters is set.
- `has_nonstandard_values = true` — whenever `black_level` or `white_level` is overridden.

These flags allow downstream stages to distinguish a source that conforms to spec defaults from one that has been adjusted.

#### 11.6.4 Downstream Consumers

`VideoParamsStage` sets these values once; every downstream stage that reads `SourceParameters` sees the same crop and level configuration. The setting-once approach is why this belongs in a dedicated stage rather than per-stage parameters.

| Consumer | What it reads | Why |
|----------|---------------|-----|
| Chroma decoder (`palcolour`, `ntsc_decoder`, `monodecoder`) | `black_level`, `white_level`, `active_video_start/end`, `first/last_active_frame_line` | Luma range scaling; decode window bounds |
| `FrameCanvas` (inside chroma decode chain) | All four crop fields | Composites decoded luma/chroma into the correct region of the output frame |
| `OutputWriter` | All six parameters | Output frame dimensions; luma range scaling to 8-bit output |
| FFmpeg output backend | All six parameters | Output frame width/height; interlacing (parity of `first_active_frame_line`); luma level scaling |
| Raw output backend | Crop parameters | Output frame width/height |
| CVBS sink | `black_level`, `white_level` | Level scaling for output |

The `VideoParamsStage` approach — propagating overrides through `SourceParameters` hints — is the correct design because multiple unrelated output backends all depend on the same crop geometry and signal levels. Putting these as per-stage parameters on the chroma decoder would leave the output backends unaffected.

#### 11.6.5 What VideoParamsStage Does Not Do

- It does not resample or scale samples. A level override changes only how downstream stages interpret the samples, not the sample values themselves.
- It does not zero or blank samples outside the crop region. The full frame buffer remains accessible through the VFrameR; the crop is advisory metadata, not a data mask.
- It does not re-measure burst phase or update `colour_frame_index`.

#### 11.6.6 Observer Crop Independence

The crop parameters in `SourceParameters` are advisory for **output and presentation** stages. Analysis and observer stages (biphase, CC, VBI, burst level, SNR, dropout) must make an explicit, documented decision about whether to respect the crop:

- **Respect crop** (default for most analysis): the observer limits its work to the declared active region. This is appropriate when the analysis is only meaningful within the active picture area.
- **Ignore crop** (required for line-data observers): observers that read data from specific line regions — biphase marks, closed captions, VBI content, VITC timecode — must always read from the full frame buffer using spec-defined line coordinates, regardless of the crop setting. Applying the crop to these observers would silently disable them whenever the user crops away the lines they operate on.

The previous implementation had a bug where cropping the active area also cropped the observable range of all downstream stages, making it impossible to observe data in lines outside the crop window (for example, biphase data lines). This design explicitly prevents that: the full frame buffer is always accessible, and each observer independently decides whether the crop is relevant to its function. Observers that must ignore the crop must document this decision with a comment citing the reason.

---

## 12. Project Format Version 2.0

### 12.1 Preliminary Code Analysis

| File | Location |
|------|----------|
| `orc/core/project.cpp:474` | Version loaded: `root["project"]["version"].as<std::string>("1.0")` |
| `orc/core/project.cpp:675` | Version serialised |
| `orc/core/include/project.h` | `Project` class; `get_version()` |

### 12.2 Version 2.0 Schema

The project YAML `version` field changes to `"2.0"`. A v1.x project file must produce a clear rejection:

```
Project format version '1.0' is not supported by Decode-Orc 2.x.
Use 'orc-cli migrate-project <input.orc-project>' to produce a v2.0 file.
```

Key schema changes for v2.0:

```yaml
project:
  version: "2.0"
  name: "Example Project"
  description: ""
  video_format: PAL          # PAL | NTSC | PAL_M — declared at project creation, enforced (see §12.3)
  source_type: Composite     # Composite | YC (was 'source_format'; 'Unknown' is not valid)

dag:
  nodes:
    - node_id: "node_1"
      stage_name: "PAL_CVBS_Source"   # stage names updated for v2 stages
      ...
```

`signal_state` is not a project schema field. `STANDARD_TBC_LOCKED` is an invariant of the entire pipeline — Decode-Orc only operates on burst-locked 4fsc TBC data and hard-rejects anything else at source ingestion. There is nothing for the project file to declare or override.

`video_format` is a **normative declaration**, not informational metadata. A project is a PAL project, an NTSC project, or a PAL-M project — mixing video systems in a single project is prohibited at every level of the application. See §12.3 for the full enforcement contract.

Source stage names change:
- `PAL_Composite_Source` → `PAL_CVBS_Source` (unchanged from current)
- `NTSC_Composite_Source` → `NTSC_CVBS_Source`
- `PAL_YC_Source` → `PAL_CVBS_YC_Source`
- `NTSC_YC_Source` → `NTSC_CVBS_YC_Source`

### 12.3 Project Type Enforcement

A project has a fixed type defined by two fields that together fully characterise the source material: `video_format` (PAL / NTSC / PAL-M) and `source_type` (Composite / YC). Neither field may be mixed within a single project. Both are enforced at four levels:

**Project creation (GUI)**: the project creation dialog presents `PAL`, `NTSC`, and `PAL-M` as mutually exclusive video system options, and `Composite` / `YC` as mutually exclusive signal type options. No pipeline can be constructed until both choices have been made. Both values are written to the YAML at creation time and are read-only thereafter — there is no "change project type" operation.

**Stage addition (GUI and core)**: the stage picker only shows source stages whose declared video system **and** signal type both match the project. A `PAL TBC Composite` stage is not visible in a `PAL YC` project or in an `NTSC Composite` project. Non-source stages (transform, analysis, sink) are not filtered by project type.

**Project load validation**: `project.cpp` validates every source stage node in the loaded DAG against both fields. Any mismatch hard-rejects the project before any stage is instantiated:

```
Project declares video_format 'PAL', source_type 'Composite' but stage
'pal_tbc_yc_source' at node_id 'node_2' uses signal type 'YC'.
The project file is invalid.
```

**Runtime assertion**: as a defensive check, `SourceParameters.system` and the signal-type property of each source stage's VFrameR are compared against the project's declared fields at pipeline start. A mismatch produces a hard error and halts execution. A correctly loaded and validated project should never reach this state.

---

## 13. Application Version and SDK ABI

### 13.1 Application Version

`CMakeLists.txt`: `project(orc VERSION 2.0.0)`. This propagates to `build/generated/version.h`, the About dialog, and all package metadata (Flatpak, DMG, MSI).

### 13.2 SDK ABI Bump

The `VideoFieldRepresentation` → `VideoFrameRepresentation` change is a breaking change to the plugin ABI. Per AGENTS.md §9:
- Bump `kStagePluginHostAbiVersion` and `kStagePluginApiVersion` in `orc/sdk/include/orc/plugin/orc_plugin_abi.h`.
- Update version compatibility tables in `docs/technical/plugin-architecture.md` and `docs/technical/plugin-sdk.md` in the same PR as the ABI change.
- Update the `IStageServices` interface documentation if any new methods are added to support VFrameR.

---

## 14. Gaps and Implicit Requirements

The following items are not explicit in the request but are required for a correct and complete migration:

### 14.1 Audio Rate Handling and the `audio_locked` Contract

The CVBS specification (index.md §Audio Data) defines audio via the `audio_locked` boolean in the `cvbs_file` metadata table. There is no 48 kHz step — audio is 44.1 kHz-based throughout:

| `audio_locked` | Rate | Samples per frame | VFrameR per-frame access |
|----------------|------|-------------------|--------------------------|
| `TRUE` — PAL | 44100 Hz (exact) | 1764 stereo pairs | Valid |
| `TRUE` — NTSC/PAL_M | 44100000/1001 Hz ≈ 44055.944 Hz | 1470 stereo pairs | Valid |
| `FALSE` | 44100 Hz (free-running) | Not fixed | Not valid |
| `NULL` / no audio | — | — | Not valid |

**TBC source impact**: PAL TBC `.pcm` is already at 44100 Hz — no resampling needed. NTSC/PAL_M TBC `.pcm` (44100 Hz) requires a very slight rate conversion to 44100000/1001 Hz using SoXR. All TBC sources write `audio_locked = TRUE`.

**ld-decode sink**: reverse of the above — PAL writes direct; NTSC/PAL_M resamples slightly; free-running passes through. See §9.5.

**EFM timing**: EFM data timing is coupled to audio frame boundaries. Frame-based EFM association must account for the locked audio rate, not a fixed 44100 Hz assumption.

**Output sink normalisation**: output sinks (`audio_sink`, `ffmpeg_video_sink`) write to user-facing container formats that require a standard integer sample rate. NTSC/PAL_M locked audio arrives at 44100000/1001 Hz ≈ 44055.944 Hz internally; output sinks must resample to 44100 Hz using SoXR before writing. PAL locked (44100 Hz) and free-running (44100 Hz) audio require no resampling at the sink.

**`TBCAudioEFMHandler` refactoring**: must be updated to support frame-based access, the `audio_locked` state, and the NTSC/PAL_M rate conversion.

### 14.2 EFM and AC3 Frame Association Specification

The CVBS spec does not define EFM or AC3 storage. Decode-Orc must define its own extension format for these. This should follow the same SQLite-sidecar pattern as the CVBS dropout extension (§6.3). A separate extension document (similar to `dropout-extension-format.md`) should be written before implementation.

### 14.3 PAL Non-Orthogonal Sample Insertion Quality

The sample insertion at the 4 extra PAL positions (§5.2.3) affects chroma subcarrier phase. A linear interpolation inserts a new sample mid-cycle. The impact on chroma decoding quality must be measured. If it introduces visible artefacts, a higher-quality interpolation (sinc-windowed, or phase-aware) must be used. This needs empirical validation before the final implementation.

### 14.4 Vectorscope Colour Matrix Alignment

The vectorscope reference vectors (§7.4.2) must use the correct colour primaries matrix for the decoded signal:
- PAL uses EBU primaries (BT.601 or BT.709 depending on source).
- NTSC uses SMPTE primaries (BT.601/SMPTE 170M).
- The choice of matrix significantly affects where colour bar targets appear on the vectorscope.
- The implementation must parameterise the colour matrix and derive reference vectors from it, not hardcode them.

### 14.5 Preview Aspect Ratio Derivation

Aspect ratio for display must be derived from spec values rather than hardcoded:
- **PAL 4:3**: active samples = 948, active lines = 576. Display pixel aspect = 59/54 (CCIR 601 / BT.601-5 §2).
- **NTSC 4:3**: active samples = 768, active lines = 486. Display pixel aspect = 10/11 (BT.601-5 §2).
- These must be computed from `SourceParameters.active_video_start/end` and `first/last_active_frame_line` rather than hardcoded.

### 14.6 VBI Line Coordinate System

VBI extraction stages (CC sink, Daphne VBI sink) use broadcast-standard 1-based frame line numbers (ITU-R BT.470-6 for PAL, SMPTE 170M-2004 for NTSC). The CVBS format uses 0-based frame lines internally. A conversion utility must be provided:
```cpp
// Standard 1-based frame line (as in broadcast specs) to 0-based internal index
inline size_t broadcast_line_to_frame_line(int broadcast_line) {
  return static_cast<size_t>(broadcast_line - 1);
}
```
All VBI-related code must use this utility explicitly, with specification citations.

### 14.7 SourceField int16 Impact on Filter Arithmetic

`SourceField.data` changing from `uint16_t` to `int16_t` means midpoint calculations like `(a + b) / 2` are now correct for signed arithmetic, but shift operations like `(a + b) >> 1` may produce incorrect results for negative values. All filter code in `palcolour.cpp`, `comb.cpp`, `transformpal.cpp`, and `ntsc_decoders` must be audited for signed arithmetic correctness.

### 14.8 Quality Metrics Dialog

`orc/gui/qualitymetricsdialog.h` currently displays field-based quality metrics. This must be updated to display frame-based metrics. Level readings previously in 16-bit TBC units must now display in the CVBS_U10_4FSC 10-bit domain (or converted to mV/IRE using spec-derived factors).

### 14.9 Test Suite Volume

Every mock of `VideoFieldRepresentation` must be replaced with a mock of `VideoFrameRepresentation`. Every fixture that supplies `uint16_t` TBC samples must supply `int16_t` CVBS_U10_4FSC samples. Per AGENTS.md §4, this is non-negotiable: all stage tests must be updated in the same PR as the stage they test. The test volume for this migration is substantial and must be factored into the implementation plan.

### 14.10 Chroma Decoder NTSC Support

The analysis above has focussed on PAL. The NTSC chroma decoder chain (`comb.h`, NTSC 1D/2D/3D decoders) must undergo the same constant audit and level domain update as the PAL decoders. All SMPTE 244M-2003 and SMPTE 170M-2004 constants must be cited and derived from `cvbs_signal_constants.h`.

### 14.11 PAL/NTSC YC Source Stages

`pal_tbc_yc_source` and `ntsc_tbc_yc_source` are not addressed explicitly in §5 — they are parallel to `pal_tbc_comp_source` and `ntsc_tbc_comp_source` respectively, and must undergo identical TBC → CVBS_U10_4FSC level mapping and frame assembly. The additional concern is Y/C colour frame alignment: the luma (`.y`) and chroma (`.c`) files must be paired on the same colour frame index throughout the source. Any phase mis-alignment between the two files at load time must be detected and reported as an error; silent mis-alignment would corrupt every decoded frame.

### 14.12 `source_align` Stage — Field Order Enforcement

`source_align` currently enforces a "first field" ordering convention when synchronising multiple sources. In the frame-based model, both fields are always present in the frame; the concept of "enforcing first field" must be redefined. The stage must instead verify that `FrameDescriptor.field1_is_first_in_file` is consistent across all aligned sources and, if not, flag the misalignment. The VBI frame number extraction used for alignment (currently field-indexed) must be updated to frame-indexed access.

### 14.13 Line Numbering Display Modes

The current TBC viewer uses field-relative line numbers with no clear indication of which numbering system is in use. v2.0 introduces a shared `LineNumberingMode` enum and a conversion utility that all GUI components (FrameScopeDialog, FrameTimingDialog, preview hover display) use consistently.

#### Four Modes

| Mode | Description | Range (PAL) | Range (NTSC) |
|------|-------------|-------------|--------------|
| `kFrameFlat0Based` | 0-based index into the CVBS frame buffer; matches dropout coordinates and internal frame addressing | 0–624 | 0–524 |
| `kFrameSequential1Based` | 1-based equivalent; default display mode | 1–625 | 1–525 |
| `kFieldRelative` | Line number within the field (1-based), prefixed by field number | F1:1–F1:313, F2:1–F2:312 | F1:1–F1:262, F2:1–F2:263 |
| `kBroadcastInterlaced` | ITU-R BT.470-6 (PAL) / SMPTE 170M-2004 (NTSC) interlaced scan line numbering; used by VBI specifications | 1–625 | 1–525 |

#### Conversion Formulae

The input to all conversions is `frame_line` (0-based frame-flat index). The field boundary is determined from the `FrameDescriptor`:
- PAL: field 1 occupies lines 0–312 (313 lines); field 2 occupies lines 313–624 (312 lines).
- NTSC: field 1 occupies lines 0–261 (262 lines, even field per SMPTE 244M-2003); field 2 occupies lines 262–524 (263 lines, odd field).

**Field-relative** (both systems):
```
field = (frame_line < field1_line_count) ? 1 : 2
line_in_field = (field == 1) ? frame_line + 1
                              : frame_line - field1_line_count + 1
```

**Broadcast interlaced — PAL** (field 1 = odd, field 2 = even):
```
if frame_line < 313:  broadcast_line = 2 * frame_line + 1       // 1, 3, 5 … 625
else:                 broadcast_line = 2 * (frame_line - 313) + 2  // 2, 4, 6 … 624
```

**Broadcast interlaced — NTSC** (field 1 = even, field 2 = odd per SMPTE 244M-2003):
```
if frame_line < 262:  broadcast_line = 2 * frame_line + 2       // 2, 4, 6 … 524
else:                 broadcast_line = 2 * (frame_line - 262) + 1  // 1, 3, 5 … 525
```

#### Implementation Notes

- The conversion utility is a pure function in a shared header (e.g. `orc/core/include/line_numbering.h`). It takes `frame_line`, `VideoSystem`, and `LineNumberingMode` and returns a `LineLabel` struct with the display string and numeric fields.
- The selected mode is stored as a persistent GUI preference (per-video-system if users work with both PAL and NTSC).
- `kBroadcastInterlaced` is the most useful mode for VBI work because broadcast specifications (VITS, VITC, teletext, closed captions) all refer to lines by their ITU/SMPTE broadcast numbers.
- `kFieldRelative` is provided for users familiar with the legacy TBC viewer. Noting the field boundary explicitly (F1 vs F2) makes it unambiguous which field a line belongs to.

---

## 15. Stage Migration Reference

This section provides a per-stage migration classification for every stage in `orc/plugins/stages/`. Stages fall into three categories:

- **Wrapper update**: only the base class (`VideoFieldRepresentationWrapper` → `VideoFrameRepresentationWrapper`) and navigation types (`FieldID` → `FrameID`) need updating. No logic change.
- **Covered**: migration design is addressed fully in a dedicated section of this document.
- **Specific attention needed**: wrapper update plus one or more specific concerns listed.

### 15.1 Source Stages

All source stage display names follow the `<VideoSystem> <SourceType> <SignalType>` convention defined in §5.0.

The TBC source stages must be renamed throughout to include `tbc` in the directory name, source file names, and C++ class names — for example `pal_tbc_comp_source/pal_tbc_comp_source_stage.h` and class `PALTBCCompSourceStage`. The previous names (`pal_comp_source`, `ntsc_comp_source`, etc.) did not indicate they were TBC-specific, which was ambiguous once CVBS sources were introduced.

| Stage | Display name | Classification | Notes |
|-------|-------------|---------------|-------|
| `cvbs_source` | Resolved at load time — see §5.0 | **Covered** | §5.1 |
| `pal_tbc_comp_source` | `PAL TBC Composite` | **Covered** | §5.2 |
| `pal_tbc_yc_source` | `PAL TBC YC` | **Specific attention** | Parallel to `pal_tbc_comp_source` (§5.2); additionally must verify Y/C colour frame alignment at load time — see §14.11 |
| `ntsc_tbc_comp_source` | `NTSC TBC Composite` | **Covered** | §5.3 |
| `ntsc_tbc_yc_source` | `NTSC TBC YC` | **Specific attention** | Parallel to `ntsc_tbc_comp_source` (§5.3); must verify Y/C colour frame alignment at load time — see §14.11 |
| `pal_m_tbc_comp_source` | `PAL-M TBC Composite` | **Covered** | §5.4 |

### 15.2 Transform Stages

| Stage | Classification | Notes |
|-------|---------------|-------|
| `field_map` | **Covered** | Renamed `FrameMapStage` — §11.2 |
| `field_invert` | **Wrapper update** | Becomes intra-frame field block swap (§4.6); operates on frame line ranges, no other logic |
| `mask_line` | **Specific attention** | Wrapper update; line specifications currently field-relative (`"F:20"` = field line 20) must be updated to frame-relative 0-based indices; update stage documentation and any UI that shows line numbers to users |
| `dropout_correct` | **Specific attention** | Wrapper update; replacement-line search currently uses `FieldID ± 1` arithmetic to address adjacent fields — must be rewritten as `FrameID` + frame-line range arithmetic; `DropoutRegion` ↔ `DropoutRun` conversion at stage boundaries (§6.3) |
| `source_align` | **Specific attention** | VBI frame number extraction must move from field-indexed to frame-indexed access; field order enforcement semantics must be redefined for frame-based model — see §14.12 |
| `stacker` | **Covered** | §11.4; additionally: audio (`audio_locked` contract §14.1), EFM frame association (§14.2), dropout representation (§6.2) |
| `video_params` | **Covered** | §11.6 |

### 15.3 Dropout Stages

| Stage | Classification | Notes |
|-------|---------------|-------|
| `dropout_map` | **Specific attention** | Wrapper update; dropout specification format uses field-line-sample coordinates — must be updated to frame-flat addressing (§6.3); parse and encode functions rewritten |
| `dropout_correct` | (see §15.2) | |
| `dropout_analysis_sink` | **Specific attention** | Field iteration → frame iteration; `DropoutRegion` → `DropoutRun` (§6.2); statistics accumulated per frame instead of per field |

### 15.4 Audio / EFM / AC3 Sinks

| Stage | Classification | Notes |
|-------|---------------|-------|
| `audio_sink` | **Specific attention** | `get_audio_samples(FieldID)` → `get_audio_samples(FrameID)` (only valid when `audio_locked() == true`); must handle free-running audio via full-stream WAV passthrough when `audio_locked() == false`; NTSC/PAL_M locked audio (44100000/1001 Hz) must be resampled to 44100 Hz using SoXR before writing output; audio rate handling per §14.1 |
| `efm_sink` | **Specific attention** | `get_efm_samples(FieldID)` → `get_efm_samples(FrameID)`; EFM frame association semantics per §14.2; EFM t-values per frame ≈ 2 × previous per-field count (§5.2.6) |
| `raw_efm_sink` | **Specific attention** | Same as `efm_sink` without decode; field iteration → frame iteration |
| `ac3rf_sink` | **Specific attention** | `get_ac3_symbols(FieldID)` → `get_ac3_symbols(FrameID)`; AC3 frame association per §14.2 |

### 15.5 Analysis Sinks

| Stage | Classification | Notes |
|-------|---------------|-------|
| `burst_level_analysis_sink` | **Specific attention** | Field iteration → frame iteration; burst level statistics accumulated per frame; burst amplitude calibration uses spec constants from `cvbs_signal_constants.h` (not `SourceParameters`) per §4.7 contract |
| `snr_analysis_sink` | **Covered** | §11.5; level references must use spec constants from `cvbs_signal_constants.h` |
| `cc_sink` | **Covered** | §11.5; VBI line coordinate system per §14.6 |
| `dropout_analysis_sink` | (see §15.3) | |

### 15.6 Video Output Sinks

| Stage | Classification | Notes |
|-------|---------------|-------|
| `chroma_sink` (common base) | **Covered** | §8 in full |
| `ffmpeg_video_sink` | **Specific attention** | Inherits `chroma_sink` (§8); additionally: audio embedding must normalise to 44100 Hz — NTSC/PAL_M locked audio arrives internally at 44100000/1001 Hz and must be resampled using SoXR before passing to FFmpeg; PAL locked and free-running audio are already at 44100 Hz; closed caption line coordinates per §14.6 |
| `raw_video_sink` | **Specific attention** | Inherits `chroma_sink` (§8); no audio/caption concerns beyond the base |
| `hackdac_sink` | **Removed** | Replaced by `CVBSSinkStage` (§10) |
| `daphne_vbi_sink` | **Specific attention** | VBI binary output currently iterates per field; must be updated to per-frame iteration; VBI line coordinates per §14.6 |
| `ld_sink` | **Covered** | §9 |

---

## 16. Specification Compliance Cross-Reference

The following table maps each design area to its normative specification source. All implementation code must carry the appropriate specification citation (AGENTS.md §5.3.6):

| Design Area | Normative Source | Document |
|-------------|-----------------|----------|
| CVBS_U10_4FSC encoding | Sample Encoding Preset | [sample-encoding-presets.md](cvbs-file-format-specification/docs/sample-encoding-presets.md) |
| PAL signal levels | EBU Tech. 3280-E | [EBU-Tech-3280-E.md](analogue-video-specifications/docs/video_formats/EBU-Tech-3280-E/EBU-Tech-3280-E.md) |
| NTSC/PAL_M signal levels | SMPTE 244M-2003 | [SMPTE-244M-2003.md](analogue-video-specifications/docs/video_formats/SMPTE-244M-2003/SMPTE-244M-2003.md) |
| PAL sampling structure | EBU Tech. 3280-E §1.3 | Above |
| NTSC sampling structure | SMPTE 244M-2003 §4.1 | Above |
| PAL_M structure | ITU-R BT.1700-1 Annex 1 Part B | [BT-1700-E.md](analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md) |
| PAL subcarrier frequency | EBU Tech. 3280-E §1.1 | Above |
| NTSC subcarrier frequency | SMPTE 244M-2003 §3.1 | Above |
| PAL_M subcarrier frequency | ITU-R BT.1700-1 Annex 1 | Above |
| PAL frame sample count | EBU Tech. 3280-E normative | [video-standard-presets.md](cvbs-file-format-specification/docs/video-standard-presets.md) |
| NTSC frame sample count | SMPTE 244M-2003 normative | Above |
| PAL colour frame sequence | EBU Tech. 3280-E §1.1.1 | Above |
| NTSC colour frame sequence | SMPTE 244M-2003 §3.2 | Above |
| Dropout extension | CVBS dropout extension | [dropout-extension-format.md](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md) |
| CVBS metadata schema | CVBS spec core | [index.md](cvbs-file-format-specification/docs/index.md) |
| Signal state presets | CVBS spec | [signal-state-presets.md](cvbs-file-format-specification/docs/signal-state-presets.md) |
| Audio format and `audio_locked` contract | CVBS spec §Audio | [index.md](cvbs-file-format-specification/docs/index.md) |
| PAL analogue signal levels | ITU-R BT.1700-1 / BT.470-6 | [BT-1700-E.md](analogue-video-specifications/docs/video_formats/BT-1700-E/BT-1700-E.md) |
| NTSC analogue signal | SMPTE 170M-2004 | [SMPTE-170M-2004.md](analogue-video-specifications/docs/video_formats/SMPTE-170M-2004/SMPTE-170M-2004.md) |
| Aspect ratio (PAL/NTSC) | ITU-R BT.601-5 §2 | [BT-601-5-1995.md](analogue-video-specifications/docs/video_formats/BT-601-5-1995/BT-601-5-1995.md) |
| VBI line numbering | ITU-R BT.470-6 | [BT-470-6-1998.md](analogue-video-specifications/docs/video_formats/BT-470-6-1998/BT-470-6-1998.md) |
