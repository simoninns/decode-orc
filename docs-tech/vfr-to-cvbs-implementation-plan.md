# Implementation Plan: VFR to CVBS_U10_4FSC Migration

This plan structures the migration described in [vfr-to-cvbs-migration-design.md](vfr-to-cvbs-migration-design.md) into self-contained implementation phases. Each phase's tasks are actionable independently after the phases they depend on are complete. Tests are part of each task — every task that adds or changes behaviour must include the associated test suite in the same change, per [AGENTS.md §4](../AGENTS.md).

---

## Resolved Design Decisions

Three open questions from the migration design were resolved before this plan was authored:

- **PAL non-orthogonal sample insertion ([design §14.3](vfr-to-cvbs-migration-design.md))**: Use linear interpolation for the 4 extra samples inserted during PAL TBC frame assembly. Quality is measured empirically on real PAL source material after implementation; sinc-windowed interpolation replaces linear only if chroma artefacts are detected.
- **Vectorscope PAL colour matrix ([design §14.4](vfr-to-cvbs-migration-design.md))**: BT.601 primaries for PAL vectorscope reference vectors. SD PAL analogue signals from LaserDisc and tape are conventionally measured against BT.601 colour bars.
- **v1.x project file migration ([design §12.2](vfr-to-cvbs-migration-design.md))**: v1.x project files are hard-rejected with a clear, actionable error. No `migrate-project` command is implemented in v2.0. The rejection message must not reference a command that does not exist.
- **`FieldID`/`FieldIDRange` immediate removal ([design §4.2](vfr-to-cvbs-migration-design.md))**: `FieldID` and `FieldIDRange` are removed immediately in Phase 1 rather than kept as deprecated aliases. Deprecated aliases would allow old code to compile silently while remaining semantically broken; immediate removal ensures every downstream consumer migrates to `FrameID` before the project will build. This creates a forced compile break between Phase 1 and Phase 4 (see Testability Review below).

---

## Testability Review

*Read this section before beginning implementation. It identifies structural constraints on when manual testing is possible and recommends two ordering adjustments.*

### Compile consistency at phase boundaries

Phase 1 deletes `video_field_representation.h` and `DropoutRegion`. The five old source stage plugins (`pal_comp_source`, `pal_yc_source`, `ntsc_comp_source`, `ntsc_yc_source`, and their shared TBC internals) reference `VideoFieldRepresentation`; they will not compile after Phase 1 until replaced in Phase 4. Phase 2's `PreviewRenderer` task also deletes `tbc_sample_to_8bit`, breaking any remaining old-path rendering. The project will not produce a working binary between Phase 1 and the completion of Phase 4.

Implementors must either:
- Retire and replace the five old source plugin directories with stub implementations (that compile and satisfy the `VideoFrameRepresentation` interface but produce no output) within Phase 1, deferring full converter logic to Phases 4–5; or
- Treat Phases 1–4 as a single implementation batch that must land together before the application builds again.

Each merged state on the main branch must be a consistent compilation unit. If work is split across PRs, the stub approach is strongly preferred.

### First manual test milestone

The first meaningful manual test in orc-gui is possible only after Phase 3 (CVBS source) — or Phase 4 (PAL TBC source), depending on available test files. Phases 1 and 2 produce no user-visible output; their correctness is verified entirely through unit tests and successful compilation.

### `FrameScopeDialog` arrives too late for source-stage level verification

The updated `FrameScopeDialog` (Phase 10) is the primary tool for confirming source-stage level mapping is correct. During Phases 3–9, developers have only the unmigrated scope dialog, which displays values in the old TBC 16-bit domain and is uninformative for CVBS_U10_4FSC signals.

**Recommendation:** Move the `FrameScopeDialog` task from Phase 10 to immediately after Phase 3. Its only upstream dependencies are Phase 1 (core types) and Phase 2 (`LineNumberingMode`, `cvbs_sample_to_8bit`). Having it available from the first testable phase means level-mapping errors in Phases 3–5 are detectable without waiting for all GUI work to complete.

### Vectorscope VFrameR integration should track the chroma decoder

The Phase 10 vectorscope task bundles the BT.601 constant audit together with the `extractFromCompositeRepresentation()` VFrameR integration. The VFrameR plumbing is required as soon as Phase 6 (chroma decoder) completes; leaving it in Phase 10 means the vectorscope is non-functional from Phase 6 through Phase 9.

**Recommendation:** Split the vectorscope task: move the `extractFromCompositeRepresentation()` VFrameR integration into Phase 6, and keep only the constant audit and BT.601 reference vector derivation in Phase 10.

---

## Phase 1: Signal Foundation — Core Types and Constants

*All other phases depend on this phase. Nothing can be implemented before these types are in place.*

### Task: `FrameID` and `FrameIDRange` types

**Files:** create `orc/core/include/frame_id.h`

Define:
```cpp
using FrameID = uint64_t;
struct FrameIDRange { FrameID first; FrameID last; };
```
Include full arithmetic operators, comparison operators, and `std::hash<FrameID>` specialisation. Remove `FieldID` and `FieldIDRange` in this same change — they have no deprecated-alias role; all downstream code must migrate to `FrameID` when its phase is reached.

**Acceptance criteria:** `frame_id.h` compiles without any other project header. `std::unordered_map<FrameID, T>` compiles without a user-supplied hash.

**Tests:** `orc-tests/core/unit/types/frame_id_test.cpp` (label: `unit`). Cover arithmetic, range construction, hash inequality for distinct values.

---

### Task: CVBS signal constants header

**Files:** create `orc/core/include/cvbs_signal_constants.h`

All constants carry AGENTS.md §5.3.6-format citations. Values from [design §3.4](vfr-to-cvbs-migration-design.md):

```cpp
// EBU Tech. 3280-E §1.1: PAL subcarrier and sample rate
constexpr double kPalFsc            = 4'433'618.75;
constexpr double kPalSampleRate     = 17'734'475.0;
constexpr double kPalSamplesPerLine = 1135.0064;
constexpr int32_t kPalFrameSamples  = 709'379;
constexpr int32_t kPalFrameLines    = 625;
constexpr int32_t kPalField1Lines   = 313;
constexpr int32_t kPalMaxSamplesPerLine = 1136;

// EBU Tech. 3280-E normative signal levels (CVBS_U10_4FSC 10-bit domain)
constexpr int32_t kPalSyncTip  = 4;
constexpr int32_t kPalBlanking = 256;
constexpr int32_t kPalBlack    = 282;
constexpr int32_t kPalWhite    = 844;
constexpr int32_t kPalPeak     = 1019;

// SMPTE 244M-2003 §4.1: NTSC
constexpr double kNtscFsc             = 315.0e6 / 88.0;
constexpr double kNtscSampleRate      = 4.0 * kNtscFsc;
constexpr int32_t kNtscSamplesPerLine = 910;
constexpr int32_t kNtscFrameSamples   = 477'750;
constexpr int32_t kNtscFrameLines     = 525;
constexpr int32_t kNtscField1Lines    = 262;

// SMPTE 244M-2003 normative signal levels
constexpr int32_t kNtscSyncTip  = 16;
constexpr int32_t kNtscBlanking = 240;
constexpr int32_t kNtscBlack    = 252;
constexpr int32_t kNtscWhite    = 800;
constexpr int32_t kNtscPeak     = 988;

// ITU-R BT.1700-1 Annex 1 Part B: PAL_M
constexpr double kPalMFsc             = 909.0 / 4.0 * (525.0 * 30000.0 / 1001.0);
constexpr double kPalMSampleRate      = 4.0 * kPalMFsc;
constexpr int32_t kPalMSamplesPerLine = 909;
constexpr int32_t kPalMFrameSamples   = 477'225;
constexpr int32_t kPalMFrameLines     = 525;
constexpr int32_t kPalMField1Lines    = 262;
// PAL_M levels are identical to NTSC (kNtscSyncTip, kNtscBlanking, etc.)

// EBU Tech. 3280-E §1.3.1: PAL non-orthogonal line positions
// These are 0-based frame-flat line indices of the four 1136-sample lines per frame.
// Field 1 lines: 155, 311 (0-indexed within field 1 — frame-flat: same values)
// Field 2 lines: 156, 312 (0-indexed within field 2 — frame-flat: add kPalField1Lines)
constexpr int32_t kPalExtraSampleLines[4] = {155, 311, 313 + 156, 313 + 312};
```

No file other than `cvbs_signal_constants.h` may define these values. Any existing magic number that duplicates them must be replaced with the constant in that file's migration phase.

**Acceptance criteria:** `grep -rE '\b(4433618|17734475|709379|477750|477225|1135\.0064)\b' orc/` produces results only within `cvbs_signal_constants.h` and files that include it. Derived relationships hold: `kNtscFrameSamples == kNtscFrameLines * kNtscSamplesPerLine`, `kPalMFrameSamples == kPalMFrameLines * kPalMSamplesPerLine`.

**Tests:** `orc-tests/core/unit/types/cvbs_signal_constants_test.cpp` (label: `unit`). Assert the derived relationships; assert `kPalFrameSamples == kPalFrameLines * 1135 + 4`.

---

### Task: `VideoFrameRepresentation` interface and `FrameDescriptor`

**Files:** create `orc/core/include/video_frame_representation.h`; create `orc/core/include/frame_descriptor.h`; delete `orc/core/include/video_field_representation.h`

`FrameDescriptor` ([design §4.3](vfr-to-cvbs-migration-design.md)):
```cpp
struct FrameDescriptor {
  FrameID frame_id;
  VideoSystem system;
  size_t height;
  size_t samples_total;
  size_t samples_per_line_nominal;
  int colour_frame_index;  // -1 = unknown; PAL/PAL_M: 1-4; NTSC: 0-1
  std::optional<int32_t> frame_number;
  std::optional<uint32_t> timecode;
  std::optional<int32_t> black_level_override;  // NTSC-J; value in 10-bit domain
  bool is_padding_frame = false;
};
```

`VideoFrameRepresentation` ([design §4.4](vfr-to-cvbs-migration-design.md)) — implement the full interface exactly as specified: `sample_type = int16_t`; navigation (`frame_range`, `frame_count`, `has_frame`); flat access (`get_frame`, `get_line`, `get_frame_copy`); YC (`has_separate_channels`, `get_frame_luma`, `get_frame_chroma`, `get_line_luma`, `get_line_chroma`); hints (`get_dropout_hints`, `get_frame_phase_hint`, `get_active_line_hint`, `get_video_parameters`, `get_vbi_hint`); audio (`has_audio`, `audio_locked`, `get_audio_sample_count`, `get_audio_samples`); EFM/AC3 (`has_efm`, `get_efm_sample_count`, `get_efm_samples`, `has_ac3_rf`, `get_ac3_symbol_count`, `get_ac3_symbols`).

`VideoFrameRepresentationWrapper` ([design §4.5](vfr-to-cvbs-migration-design.md)) — full forwarding for all methods. Transform stages override only what they change.

`using VideoFrameRepresentationPtr = std::shared_ptr<VideoFrameRepresentation>;`

`DropoutRun` is forward-declared here; defined in the next task.

**Acceptance criteria:** `ctest -R MVPArchitectureCheck` passes. `video_field_representation.h` no longer exists. The header is includable in `orc/core/` without Qt or GUI headers.

**Tests:** Compile-only test. Create `orc-tests/core/unit/mocks/mock_video_frame_representation.h` — a `MockVideoFrameRepresentation` implementing all pure virtual methods with GoogleMock. This mock is used by every subsequent stage test.

---

### Task: `SourceParameters`, `DropoutRun`, and `dropout_util`

**Files:** `orc/view-types/orc_source_parameters.h`; `orc/core/metadata/dropouts.h`; create `orc/core/metadata/dropout_util.h`

**`SourceParameters`** — apply all field changes from [design §4.7](vfr-to-cvbs-migration-design.md):

Remove: `field_width`, `field_height`, `number_of_sequential_fields`, `is_first_field_first`, `blanking_16b_ire`, `black_16b_ire`, `white_16b_ire`, `is_subcarrier_locked`, `colour_burst_start`, `colour_burst_end`, `first_active_field_line`, `last_active_field_line`, `sample_rate`, `fsc`.

Add: `frame_width_nominal` (int32_t), `frame_height` (int32_t), `number_of_sequential_frames` (int32_t), `sync_tip_level` (int32_t), `blanking_level` (int32_t), `black_level` (int32_t), `white_level` (int32_t), `peak_level` (int32_t), `has_nonstandard_values` (bool).

Retain: `system`, `is_widescreen`, `first_active_frame_line`, `last_active_frame_line`, `is_mapped`, `tape_format`, `decoder`, `git_branch`, `git_commit`, `active_area_cropping_applied`, `active_video_start`, `active_video_end`.

**`DropoutRun`** — replace `DropoutRegion` in `orc/core/metadata/dropouts.h`:
```cpp
struct DropoutRun {
  FrameID frame_id;
  uint64_t sample_start;   // 0-based sample offset within the flat frame buffer
  uint32_t sample_count;
  uint8_t severity;        // 0-100
};
```
Delete `DropoutRegion`.

**`dropout_util.h`** — create with:
```cpp
namespace dropout_util {
  struct FieldLineSample { int32_t field; int32_t line; int32_t sample; };
  FieldLineSample frame_sample_to_field_line(VideoSystem sys, uint64_t frame_sample_offset);
  uint64_t field_line_to_frame_sample(VideoSystem sys, int32_t field, int32_t line, int32_t sample_within_line);
}
```
Implementations use `kPalField1Lines`, `kNtscField1Lines`, `kPalMField1Lines` and the per-system samples-per-line constants from `cvbs_signal_constants.h`. PAL: account for non-orthogonal line lengths using `kPalExtraSampleLines` when computing cumulative offsets.

**Acceptance criteria:** `SourceParameters` contains none of the removed fields. `DropoutRun` matches [design §6.2](vfr-to-cvbs-migration-design.md). Round-trip through both conversion functions is lossless for all systems and boundary positions.

**Tests:** `orc-tests/core/unit/metadata/dropout_util_test.cpp` (label: `unit`). Cover all three systems; first and last sample in each field; PAL 1136-sample line boundaries.

### Manual Testing in orc-gui

No manual testing is possible after this phase. All changes are type definitions, constants, and header deletions with no user-visible behaviour in the application. Verify correctness through unit tests (`ctest -L unit`) and successful compilation.

---

## Phase 2: Navigation and Rendering Infrastructure

*Depends on Signal Foundation. GUI phases depend on this phase.*

### Task: `LineNumberingMode` conversion utility

**Files:** create `orc/core/include/line_numbering.h`

Implement per [design §14.13](vfr-to-cvbs-migration-design.md):
```cpp
enum class LineNumberingMode {
  kFrameFlat0Based, kFrameSequential1Based, kFieldRelative, kBroadcastInterlaced
};
struct LineLabel {
  std::string display;   // formatted string for the user
  int field;             // 1 or 2 (kFieldRelative only; else 0)
  int line_in_field;     // 1-based within field (kFieldRelative only; else 0)
  int broadcast_line;    // 1-based broadcast line (kBroadcastInterlaced only; else 0)
};
LineLabel make_line_label(size_t frame_line, VideoSystem sys, LineNumberingMode mode);
```

Conversion formulae from [design §14.13](vfr-to-cvbs-migration-design.md) must be implemented exactly — broadcast interlaced PAL and NTSC follow different field/line parity conventions. This is a pure function in a shared header with no Qt or I/O dependency; it is used by `FrameScopeDialog`, `FrameTimingDialog`, and the preview hover display.

Also implement:
```cpp
// ITU-R BT.470-6 (PAL) / SMPTE 170M-2004 (NTSC): convert spec 1-based line to 0-based internal index
inline size_t broadcast_line_to_frame_line(int broadcast_line) {
  return static_cast<size_t>(broadcast_line - 1);
}
```

**Acceptance criteria:** PAL broadcast interlaced: frame_line 0 → broadcast_line 1, frame_line 1 → broadcast_line 3, frame_line 313 → broadcast_line 2. NTSC broadcast interlaced: frame_line 0 → broadcast_line 2, frame_line 262 → broadcast_line 1.

**Tests:** `orc-tests/core/unit/types/line_numbering_test.cpp` (label: `unit`). Cover PAL and NTSC; all four modes; boundary lines (0, last in field 1, first in field 2, last in frame).

---

### Task: `DAGFrameRenderer`

**Files:** rename `orc/core/include/dag_field_renderer.h` → `orc/core/include/dag_frame_renderer.h`; update implementation file

Rename `DAGFieldRenderer` → `DAGFrameRenderer`; rename `FieldRenderResult` → `FrameRenderResult`. Replace all `FieldID` navigation with `FrameID`. Update LRU cache key to `FrameID`. Remove any field-splitting logic — the renderer operates on full frames from `VideoFrameRepresentation`. Update all callers (`orc/gui/`, `orc/cli/`) to include the renamed header and use the renamed type.

**Acceptance criteria:** `dag_frame_renderer.h` compiles without including `video_field_representation.h`. The project builds without `dag_field_renderer.h` present.

**Tests:** Update existing DAGFieldRenderer tests to `DAGFrameRenderer` with `MockVideoFrameRepresentation`. Verify LRU cache hit/miss with `FrameID` keys.

---

### Task: `PreviewRenderer` sample mapping and coordinate updates

**Files:** `orc/core/include/preview_renderer.h` and implementation

Replace `tbc_sample_to_8bit(uint16_t, double blackIRE, double whiteIRE)` with ([design §7.1.2](vfr-to-cvbs-migration-design.md)):
```cpp
uint8_t cvbs_sample_to_8bit(int16_t sample, int32_t blanking_level, int32_t white_level);
```
Linear mapping: blanking_level → 0, white_level → 255. Clamp only the 8-bit output — the int16 sample is not clamped.

Update `PreviewOutputType` enum: `Field` → `Frame_Field1`; add `Frame_Field2`; rename `Frame_EvenOdd` → `Frame_Field1_First`; retain `Split`.

Update coordinate mapping methods (`navigate_frame_line`, `map_image_to_field`, `map_field_to_image`, `get_frame_fields`) to use `FrameID` and 0-based frame line indices.

Update dropout overlay rendering: `DropoutRun.sample_start` and `.sample_count` are frame-flat; convert to (line, start_sample, count) via `dropout_util::frame_sample_to_field_line()` before rendering onto the image.

**Acceptance criteria:** `tbc_sample_to_8bit` is deleted. Blanking maps to 0; white maps to 255; values outside [blanking, white] (headroom) clamp the output only. Old `Field` enum value is absent. Display aspect ratio is derived from `SourceParameters.active_video_start`, `active_video_end`, `first_active_frame_line`, and `last_active_frame_line` per BT.601-5 §2; no hardcoded PAL or NTSC pixel aspect ratio constants remain in the renderer. The preview auto-scales its vertical display range: the default axis spans `[sync_tip_level, peak_level]` from `SourceParameters`; it expands to include any sample value outside that range when such values are present, per [design §3.5](vfr-to-cvbs-migration-design.md).

**Tests:** Unit tests for `cvbs_sample_to_8bit` at boundary values and headroom values; dropout coordinate conversion through `frame_sample_to_field_line`; aspect ratio computation for PAL (59/54) and NTSC (10/11) from mock `SourceParameters`; auto-scale range expansion when a sample value outside `[sync_tip_level, peak_level]` is present in the input.

### Manual Testing in orc-gui

No meaningful manual testing is possible after this phase in isolation:

- `LineNumberingMode` is not yet wired to any dialog (the `FrameScopeDialog` update is in Phase 10 unless moved earlier per the recommendation in the Testability Review).
- `DAGFrameRenderer` is an internal rename with no functional change to the rendered output.
- `cvbs_sample_to_8bit` has no caller until a migrated source stage (Phase 3 or 4) is in place.

If old source stage stubs (see Testability Review) are present and the application builds, launching orc-gui and confirming it starts without crashing is the only available check.

---

## Phase 3: CVBS Source Stage

*Depends on Signal Foundation. Concurrent with TBC source phases.*

### Task: Replace `CVBSDecodedFieldRepresentation` with `CVBSDecodedFrameRepresentation`

**Files:** `orc/plugins/stages/cvbs_source/cvbs_source_stage.h`, `cvbs_source_stage.cpp`

Delete `CVBSDecodedFieldRepresentation`. Implement `CVBSDecodedFrameRepresentation` satisfying `VideoFrameRepresentation`. Frame cache keys on `FrameID`.

At open time ([design §5.1.2](vfr-to-cvbs-migration-design.md)):
1. Read `signal_state_preset` from the `.meta` SQLite `cvbs_file` table. Hard-reject (throw with a clear message) if the value is not `STANDARD_TBC_LOCKED`. No partial-open, no fallback.
2. Read `sample_encoding_preset` from `.meta`. Implement normalisation to `CVBS_U10_4FSC` for all four declared encodings:
   - `CVBS_U10_4FSC`: identity (already int16 in 10-bit domain)
   - `CVBS_U16_4FSC`: `value = uint16_value / 64`
   - `CVBS_TPG21_4FSC`: `value = int16_value / 64 + 508`
   - `CVBS_S16_FSC`: `value = int16_value / 32 + blanking_10bit` where `blanking_10bit` is `kPalBlanking` (PAL) or `kNtscBlanking` (NTSC/PAL_M)
3. Populate `SourceParameters` with spec-defined values from `cvbs_signal_constants.h` (not from metadata level fields).
4. Measure colour burst phase from the signal for each frame to populate `FrameDescriptor.colour_frame_index` per [design §11.1.2](vfr-to-cvbs-migration-design.md). PAL: position within the 4-frame sequence (1–4) per EBU Tech. 3280-E §1.1.1; NTSC: 0 (frame A) or 1 (frame B) per SMPTE 244M-2003 §3.2; PAL_M: position within the 4-frame sequence (1–4) per ITU-R BT.1700-1 Annex 1 Part B. Set `colour_frame_index = -1` when burst is absent or unmeasurable.

Display name: read `video_standard_preset` and file type (composite vs YC) from `.meta` at load time; set the stage instance display name per [design §5.0](vfr-to-cvbs-migration-design.md) pattern `<VideoSystem> CVBS <SignalType>`.

Remove all resampling of the 4FSC signal — the CVBS source requires none.

Expand coverage to include PAL_M dispatch (alongside existing PAL and NTSC classes).

**Acceptance criteria:** A CVBS file with any non-`STANDARD_TBC_LOCKED` state is rejected with an error before any frame data is returned. All four encoding normalisations are implemented. Frame count matches `number_of_sequential_frames` from `.meta`. `SourceParameters` level fields are populated from `cvbs_signal_constants.h`, not from metadata. `colour_frame_index` cycles correctly for each system on a standard-signal file; is `-1` for frames where burst is not detectable.

**Tests:** `orc-tests/core/unit/stages/cvbs_source/` (labels: `unit`, `sources`). Mock the SQLite reader interface. Cover: signal state rejection for each non-standard value; each encoding normalisation formula at blanking and white input; `SourceParameters` population for PAL/NTSC/PAL_M; burst phase measurement producing the correct `colour_frame_index` for each system; `colour_frame_index = -1` for frames with zero-valued burst samples.

---

### Task: CVBS source sidecar loading (dropout, audio, EFM, AC3)

**Files:** `orc/plugins/stages/cvbs_source/cvbs_source_stage.cpp`

Load all sidecars at stage initialisation per [design §5.1.2 items 5–10](vfr-to-cvbs-migration-design.md):

**Dropout** ([dropout-extension-format.md](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md)): open `<basename>.dropouts.meta` SQLite. Load dropout rows into `std::vector<DropoutRun>` indexed by `FrameID`. Return from `get_dropout_hints(FrameID)`. Absent file → `get_dropout_hints()` returns empty; no error.

**Audio**: open `<basename>_audio_00.wav` if present; read `audio_locked` from `cvbs_file` table.
- `audio_locked=TRUE`: `audio_locked()` returns `true`; `get_audio_samples(FrameID)` returns the frame-locked block (PAL: 1764 stereo int16 pairs; NTSC/PAL_M: 1470 stereo int16 pairs).
- `audio_locked=FALSE`: `audio_locked()` returns `false`; `get_audio_samples()` returns empty; `has_audio()` returns `true`.
- NULL / no WAV: `has_audio()` returns `false`.

**EFM** ([efm-extension-format.md](cvbs-file-format-specification/docs/extensions/efm-extension-format.md)): open `<basename>.efm` (binary) and `<basename>.efm.meta` (SQLite). Both must be present; if either is absent, `has_efm()` returns `false`. Map each `FrameID` to its byte range using the `efm_frame` table (`t_value_offset`, `t_value_count`). Expose via `get_efm_samples(FrameID)`.

**AC3** ([ac3-extension-format.md](cvbs-file-format-specification/docs/extensions/ac3-extension-format.md)): parallel to EFM; uses the `ac3_frame` table.

**NTSC-J**: when `video_standard` is NTSC and `.meta` carries a non-NULL `black_level` field, populate `FrameDescriptor.black_level_override` with that value converted to the `CVBS_U10_4FSC` 10-bit domain.

**Acceptance criteria:** Absent sidecar files set `has_*()`→false without throwing. Frame-locked audio blocks have the correct stereo pair count for each system. EFM and AC3 `has_*()` are false when either sidecar file is missing.

**Tests:** Unit tests with mock file I/O for each sidecar. Cover: dropout frame lookup; audio_locked=true, false, null; EFM absent-sidecar contract; AC3 frame range mapping; NTSC-J black_level_override conversion.

### Manual Testing in orc-gui

Phase 3 is the first phase where end-to-end manual testing is possible, provided a CVBS source file is available.

**Stage loading and display name:**
- Open a CVBS composite `.meta` file via the source picker. The stage display name should read `<System> CVBS Composite` (e.g., `PAL CVBS Composite`).
- Navigate frames in the preview; frames should render with blanking regions at near-black and active picture at appropriate brightness.
- The GUI frame count should match `number_of_sequential_frames` in the `.meta` file.

**Error rejection:**
- Open a CVBS `.meta` file whose `signal_state_preset` is set to any non-`STANDARD_TBC_LOCKED` value. The GUI must display a clear rejection error before showing any frame data, without crashing.

**Sidecar data:**
- Open a CVBS file with a `.dropouts.meta` sidecar; verify dropout overlay markers appear in the preview on the expected frames.
- Open a CVBS file with an `_audio_00.wav` sidecar; verify the audio presence indicator shows correctly (`audio_locked` state visible in source properties).
- If EFM or AC3 sidecars are present, verify the corresponding `has_efm` / `has_ac3_rf` status in source properties.

---

## Phase 4: PAL TBC Source Stage

*Depends on Signal Foundation. Creates the unified `tbc_source` plugin structure used by all TBC phases.*

### Task: Unified `tbc_source` plugin structure

**Files:** create `orc/plugins/stages/tbc_source/`; retire `orc/plugins/stages/pal_comp_source/`, `pal_yc_source/`, `ntsc_comp_source/`, `ntsc_yc_source/`

Create a single `TBCSourceStage` class that reads the video system from `.tbc.json.db` at open time and dispatches to a format-specific converter class (`PalTBCConverter`, `NtscTBCConverter`, `PalMTBCConverter` — implemented in subsequent tasks). The plugin registers a single ID `tbc_source`; the five existing plugin IDs are retired from the registry.

Display name resolved at load time from metadata: video system → `PAL` / `NTSC` / `PAL-M`; composite vs YC presence → `Composite` / `YC`; formatted as `<VideoSystem> TBC <SignalType>` per [design §5.0](vfr-to-cvbs-migration-design.md).

Refactor `orc/core/tbc_source_internal/` headers: remove `VideoFieldRepresentation` usage now. The internal TBC file-reading code (field loading, metadata parsing) is preserved and used by the converter classes as private implementation detail.

**Acceptance criteria:** All five old plugin IDs are absent from the plugin registry. Opening a PAL composite `.tbc` file resolves display name `PAL TBC Composite`. MVP gate passes.

**Tests:** Unit tests for plugin registration, display name resolution from mock `.tbc.json.db` reader. No converter logic tests yet — those are in the next task.

---

### Task: PAL TBC level mapping and frame assembly

**Files:** `orc/plugins/stages/tbc_source/pal_tbc_converter.h/.cpp`

Implement [design §5.2.2](vfr-to-cvbs-migration-design.md) level mapping: read `blanking_16b_ire` and `white_16b_ire` from `.tbc.json.db`. Apply:
```cpp
// EBU Tech. 3280-E: map TBC levels to CVBS_U10_4FSC
int16_t tbc_to_cvbs_pal(uint16_t tbc_sample, int32_t tbc_blanking, int32_t tbc_white) {
  double n = static_cast<double>(tbc_sample - tbc_blanking) / (tbc_white - tbc_blanking);
  double cvbs = n * (kPalWhite - kPalBlanking) + kPalBlanking;
  return static_cast<int16_t>(std::round(cvbs));
  // No clamp: preserve headroom below sync and above peak
}
```

Implement [design §5.2.3](vfr-to-cvbs-migration-design.md) PAL frame assembly:
- Input from TBC: field 1 (TBC) = **312** lines × 1135 samples (354,120 samples); field 2 (TBC) = **313** lines × 1135 samples (355,255 samples); total = 709,375 samples.
- Required output: 709,379 samples (`kPalFrameSamples`)
- Insert 4 extra samples at the positions in `kPalExtraSampleLines` (from `cvbs_signal_constants.h`) using **linear interpolation** between the last sample of the standard line and the first sample of the next line. Interpolation is performed in the CVBS_U10_4FSC domain after level mapping.
- Field ordering reconciliation ([design §5.2.3](vfr-to-cvbs-migration-design.md) note): the TBC format stores the odd (earlier temporal) field as field 1 with 312 lines and the even field as field 2 with 313 lines. The CVBS convention places the 313-line block first (CVBS field 1). The implementation therefore maps **TBC field 2 → CVBS field 1** (313 lines) and **TBC field 1 → CVBS field 2** (312 lines). Document this inversion explicitly with a comment citing design §5.2.3 and EBU Tech. 3280-E §1.3.
- Frame layout: `[CVBS field 1 = TBC field 2, 313 lines][CVBS field 2 = TBC field 1, 312 lines]` sequentially.

**Acceptance criteria:** Output frame has exactly `kPalFrameSamples = 709,379` samples for every PAL frame. Level conversion maps TBC blanking exactly to `kPalBlanking = 256`; TBC white to `kPalWhite = 844`. Inserted samples are linear interpolations of their neighbours. CVBS field 1 in the output is the 313-line block (sourced from TBC field 2); CVBS field 2 is the 312-line block (sourced from TBC field 1).

**Tests:** `orc-tests/core/unit/stages/tbc_source/pal_tbc_converter_test.cpp` (labels: `unit`, `sources`). Verify sample count. Verify level conversion at blanking, white, sync-tip (should map below 4 into negative range). Verify inserted sample positions and interpolated values. Verify that the 313-line TBC field (field 2) appears first in the output frame buffer.

---

### Task: PAL colour frame sequence, audio, EFM, and AC3

**Files:** `orc/plugins/stages/tbc_source/pal_tbc_converter.h/.cpp`; `orc/core/tbc_source_internal/tbc_audio_efm_handler.h/.cpp`

Implement [design §5.2.4](vfr-to-cvbs-migration-design.md): read `field_phase_id` from TBC metadata; map to `FrameDescriptor.colour_frame_index` (1–4 per EBU Tech. 3280-E §1.1.1). The mapping must be verified against the existing `FieldPhaseHint` convention in `tbc_source_internal/` and documented with a citation and a mapping table comment in the implementation file.

Update `TBCAudioEFMHandler` per [design §5.2.5](vfr-to-cvbs-migration-design.md):
- PAL audio: the `.pcm` file is at 44100 Hz. Segment into blocks of 1764 stereo int16_t pairs per frame. Expose via `get_audio_samples(FrameID)`. `audio_locked()` always returns `true` for TBC sources.
- EFM/AC3 per [design §5.2.6](vfr-to-cvbs-migration-design.md): merge two consecutive field reads into one per-frame `std::vector<uint8_t>`. Expose via `get_efm_samples(FrameID)` and `get_ac3_symbols(FrameID)`.

**Acceptance criteria:** `colour_frame_index` cycles 1→2→3→4→1 for consecutive PAL frames from a reference `.tbc.json.db`. PAL audio block is exactly 1764 stereo pairs. EFM frame vector is the concatenation of the two consecutive field vectors.

**Tests:** Unit tests for colour frame index mapping; audio segmentation (exact block size); EFM frame vector concatenation.

---

### Task: PAL TBC YC variant

**Files:** `orc/plugins/stages/tbc_source/pal_tbc_yc_converter.h/.cpp`

Parallel to the composite converter. Luma (`.y`) and chroma (`.c`) files are assembled independently using the same level mapping and frame assembly logic. At open time ([design §14.11](vfr-to-cvbs-migration-design.md)): compare `colour_frame_index` at frame 0 for luma and chroma; hard-reject with a clear error if misaligned. Implement `get_frame_luma()` and `get_frame_chroma()` from the respective assembled buffers.

**Acceptance criteria:** Misaligned Y/C files produce a hard error at open time identifying the phase mismatch. Aligned Y/C files expose luma and chroma independently. Level mapping and frame assembly are identical to the composite path.

**Tests:** Unit tests for aligned and misaligned phase ID pairs; per-channel frame assembly verification.

---

## Phase 5: NTSC and PAL_M TBC Source Stages

*Depends on the PAL TBC phase (unified tbc_source structure must exist).*

### Task: NTSC TBC converter (composite and YC)

**Files:** `orc/plugins/stages/tbc_source/ntsc_tbc_converter.h/.cpp`; `ntsc_tbc_yc_converter.h/.cpp`

Implement [design §5.3](vfr-to-cvbs-migration-design.md):
- Level mapping: `kNtscBlanking = 240`, `kNtscWhite = 800` (SMPTE 244M-2003)
- Frame assembly: 525 × 910 = 477,750 samples; orthogonal — no extra samples; field 1: 262 lines; field 2: 263 lines; remove TBC padding (both stored at 263 lines in TBC format); frame layout: `[field 1: 262 × 910][field 2: 263 × 910]`
- Colour frame sequence: SMPTE 244M-2003 §3.2; 2-frame A/B; `colour_frame_index` = 0 (A) or 1 (B)
- NTSC-J: when `.tbc.json.db` carries a non-standard black level, populate `FrameDescriptor.black_level_override` in the 10-bit domain

YC variant: Y/C alignment check identical to PAL YC (compare `colour_frame_index` at frame 0; hard-reject if misaligned).

**Acceptance criteria:** Output frame sample count is exactly 477,750. Level conversion maps TBC blanking to `kNtscBlanking = 240` and TBC white to `kNtscWhite = 800`. Colour frame index cycles 0→1→0→1.

**Tests:** `orc-tests/core/unit/stages/tbc_source/ntsc_tbc_converter_test.cpp` (labels: `unit`, `sources`). Verify sample count, level conversion, colour frame sequence, NTSC-J override, YC alignment rejection.

---

### Task: NTSC/PAL_M audio resampling via SoXR

**Files:** `orc/core/tbc_source_internal/tbc_audio_efm_handler.h/.cpp`

The NTSC/PAL_M `.pcm` file is at 44100 Hz. The frame-locked rate is 44100000/1001 Hz. Resample lazily per-frame using SoXR ([design §5.2.5](vfr-to-cvbs-migration-design.md)). The SoXR state is persistent across frames to avoid artefacts at boundaries. Segment the resampled output into blocks of 1470 stereo int16_t pairs per frame.

PAL audio (44100 Hz) is already at the locked rate — the SoXR path is bypassed entirely for PAL; no resampling invocation occurs.

**Acceptance criteria:** NTSC audio output is exactly 1470 stereo pairs per frame. SoXR state is maintained across successive `get_audio_samples()` calls to produce a continuous stream. The PAL path never calls SoXR.

**Tests:** Unit test NTSC path with a synthetic 44100 Hz PCM sine wave. Verify output sample count per frame. Verify PAL path bypasses SoXR (mock assertion).

---

### Task: PAL_M TBC converter

**Files:** `orc/plugins/stages/tbc_source/pal_m_tbc_converter.h/.cpp`

Implement [design §5.4](vfr-to-cvbs-migration-design.md):
- Level table: same as NTSC (`kNtscBlanking = 240`, `kNtscBlack = 252`, `kNtscWhite = 800`)
- Frame assembly: 525 × 909 = 477,225 samples; orthogonal; field 1: 262 lines, field 2: 263 lines
- Colour frame sequence: 4-frame cycle per ITU-R BT.1700-1 Annex 1 Part B; `colour_frame_index` = 1–4
- Audio: same SoXR resampling path as NTSC (1470 stereo pairs per frame)

**Acceptance criteria:** Output frame sample count is exactly 477,225. Colour frame index cycles 1→2→3→4. Level values match NTSC constants.

**Tests:** Unit tests mirroring NTSC structure.

---

## Phase 6: Chroma Decoder — Signal Domain and SourceField Refactoring

*Depends on Signal Foundation. Sink stages depend on this phase.*

### Task: `SourceField` refactoring to non-owning frame views

**Files:** `orc/plugins/stages/sinks/common/decoders/sourcefield.h`; `orc/plugins/stages/sinks/common/chroma_sink_stage.h/.cpp`

Replace the copy-based sample buffer in `SourceField` with non-owning pointers into the VFrameR frame buffer ([design §8.2](vfr-to-cvbs-migration-design.md)):
```cpp
struct SourceField {
  int32_t seq_no = 0;
  bool is_first_field = true;
  std::optional<int32_t> frame_phase_id;
  const int16_t* data = nullptr;
  size_t line_count = 0;
  size_t samples_per_line = 0;
  const int16_t* luma_data = nullptr;
  const int16_t* chroma_data = nullptr;
  bool is_yc = false;
  // PAL only: per-line start pointers into the non-uniformly strided frame buffer
  std::vector<const int16_t*> line_ptrs;
  int32_t getOffset() const { return is_first_field ? 0 : 1; }
};
```

Update `convertToSourceField()` per [design §8.5](vfr-to-cvbs-migration-design.md):
- Field 1: `data = vframer.get_frame(frame_id)`
- Field 2: `data = vframer.get_frame(frame_id) + field1_sample_count`
  where `field1_sample_count` = `kPalField1Lines × cumulative_field1_samples` (PAL) or `kNtscField1Lines × kNtscSamplesPerLine` (NTSC/PAL_M)

For PAL: build `line_ptrs` — a per-line pointer table ([design §8.6](vfr-to-cvbs-migration-design.md) option 1) by walking the frame buffer with cumulative offsets accounting for 1135/1136-sample lines at positions in `kPalExtraSampleLines`. All PAL decoder line access uses `line_ptrs[line_index]`, not stride arithmetic.

The VFrameR frame buffer lifetime is maintained by the `VideoFrameRepresentationPtr` held by `ChromaSinkStage` for the duration of each decode operation.

**Acceptance criteria:** Zero heap allocation for sample data inside `SourceField`. PAL `line_ptrs` has exactly 313 entries for field 1 and 312 for field 2. NTSC/PAL_M use `data + line × samples_per_line` (no `line_ptrs`).

**Tests:** `orc-tests/core/unit/stages/chroma_sink/sourcefield_test.cpp` (labels: `unit`, `sinks`). Verify pointer correctness for field 1 and field 2 with synthetic PAL and NTSC frame buffers. Verify PAL `line_ptrs` cumulative offsets at `kPalExtraSampleLines` positions. Assert no sample copies are made.

---

### Task: `PalColour` and PAL decoder constant audit

**Files:** `orc/plugins/stages/sinks/common/decoders/palcolour.h/.cpp`, `transformpal.h/.cpp`, `comb.h/.cpp`

Audit per [design §8.4](vfr-to-cvbs-migration-design.md):
1. Replace `MAX_WIDTH = 1135` with `kPalMaxSamplesPerLine = 1136` from `cvbs_signal_constants.h`. Resize all filter state arrays to accommodate 1136 samples.
2. Verify and add citations for all hardcoded burst phase angles against EBU Tech. 3280-E §1.2.
3. Replace all internal signal-processing uses of `blanking_16b_ire`, `black_16b_ire`, `white_16b_ire` with `kPalBlanking`, `kPalBlack`, `kPalWhite` from `cvbs_signal_constants.h`.
4. Fix signed arithmetic for `int16_t` samples:
   - Replace `(a + b) >> 1` with `(a + b) / 2` throughout.
   - Before any `(a + b)` where a, b are `int16_t`, promote to `int32_t`: `(static_cast<int32_t>(a) + b) / 2`.
5. Update 3D Transform PAL look-behind: `getLookBehind()` returns 1 (one frame) per [design §8.7](vfr-to-cvbs-migration-design.md).

**Acceptance criteria:** Zero `MAX_WIDTH = 1135` literals remain anywhere in the decoder files. Zero uses of `blanking_16b_ire` or equivalent TBC-domain level constants remain in PAL decoder internal signal processing. Every non-trivial constant has a specification citation.

**Tests:** Update existing PalColour unit tests to supply `int16_t` samples. Add a test for the 1136-sample line path confirming filter state arrays are not overflowed.

---

### Task: NTSC decoder constant audit and level domain update

**Files:** NTSC-specific decoder files in `orc/plugins/stages/sinks/common/decoders/`

Parallel to the PalColour audit for NTSC per [design §14.10](vfr-to-cvbs-migration-design.md). All SMPTE 244M-2003 and SMPTE 170M-2004 constants must be cited; derived from `cvbs_signal_constants.h`. Signed arithmetic corrections identical to the PAL task. Output luma range scaling reads `SourceParameters.black_level` and `SourceParameters.white_level` (not TBC-domain fields) per [design §8.3](vfr-to-cvbs-migration-design.md).

**Acceptance criteria:** No NTSC TBC-domain level constants remain in decoder internals. All SMPTE citations in place. Output luma scaling uses `SourceParameters` fields.

**Tests:** Update existing NTSC decoder unit tests to pass `int16_t` samples.

---

### Task: `ChromaSinkStage` VFrameR integration and level context separation

**Files:** `orc/plugins/stages/sinks/common/chroma_sink_stage.h/.cpp`

Update `ChromaSinkStage` to accept `VideoFrameRepresentationPtr` ([design §8.5](vfr-to-cvbs-migration-design.md)). `Decoder::configure()` receives updated `SourceParameters` with frame geometry and spec-defined signal levels.

Implement the level-context separation from [design §8.3](vfr-to-cvbs-migration-design.md):
- Decoder internal processing (sync detection, burst gating, phase measurement): uses `kPalBlanking`, `kNtscBlanking` etc. from `cvbs_signal_constants.h` directly.
- Output luma range scaling (ComponentFrame output domain): reads `SourceParameters.black_level` and `SourceParameters.white_level`.

**Acceptance criteria:** `ChromaSinkStage` compiles without including `video_field_representation.h`. The two level contexts are used in the correct code paths; output scaling reads `SourceParameters`; internal processing reads spec constants.

**Tests:** Integration test with synthetic PAL and NTSC frame buffers. Verify that changing `SourceParameters.black_level` changes the output luma range without affecting burst gating.

---

## Phase 7: Wrapper-Pattern Transform Stage Updates

*Depends on Signal Foundation (VFrameRepresentationWrapper). Each stage in this phase is independent of the others.*

### Task: `field_invert`, `mask_line`, and `video_params`

**Files:** `orc/plugins/stages/field_invert/`; `orc/plugins/stages/mask_line/`; `orc/plugins/stages/video_params/`

**`field_invert`** → intra-frame field block swap: the output VFrameR presents field line blocks in reverse order (field 2 first, field 1 second) via an index redirect inside `VideoFrameRepresentationWrapper`. No sample data is copied. Rename stage to `frame_field_swap`.

**`mask_line`**: update line address from field-relative notation to 0-based frame-flat integer. Update stage parameter documentation and any GUI label that displays the line number. For PAL: zeroing a 1136-sample line must zero all 1136 samples (do not assume fixed 1135-sample width).

**`video_params`** per [design §11.6](vfr-to-cvbs-migration-design.md): wrap VFrameR; override specified `SourceParameters` fields; set `active_area_cropping_applied = true` when any crop parameter is set; set `has_nonstandard_values = true` when `black_level` or `white_level` is overridden. Sentinel value for unset parameters is -1.

**Acceptance criteria:** `frame_field_swap` performs no sample copy (verify with a mock that `get_frame_copy` is not called). `mask_line` zeroes the correct samples including PAL 1136-sample lines. `video_params` sets `has_nonstandard_values = true` exactly when a level override is applied; `active_area_cropping_applied = true` exactly when a crop parameter is set.

**Tests:** `orc-tests/core/unit/stages/` — one suite per stage with `MockVideoFrameRepresentation`. Label: `unit`, `transforms`.

---

### Task: `dropout_correct` and `source_align`

**Files:** `orc/plugins/stages/dropout_correct/`; `orc/plugins/stages/source_align/`

**`dropout_correct`**: update to `VideoFrameRepresentationWrapper`. Replacement-line search moves from `FieldID ± 1` arithmetic to `FrameID` + frame-line range arithmetic. Use `dropout_util::frame_sample_to_field_line()` for coordinate mapping when needed. Input and output dropout hints use `DropoutRun`. Correction operates on the full frame buffer regardless of `active_area_cropping_applied`; crop settings must not restrict which sample ranges are corrected. Document this decision with a comment citing [design §11.6.6](vfr-to-cvbs-migration-design.md).

**`source_align`**: update to `VideoFrameRepresentationWrapper`. VBI frame number extraction moves from field-indexed to frame-indexed access. Field-order enforcement is redefined per [design §14.12](vfr-to-cvbs-migration-design.md): the stage verifies that `FrameDescriptor.colour_frame_index` is consistent across all aligned sources rather than enforcing field-pair ordering.

**Acceptance criteria:** `dropout_correct` replacement-line lookup uses `FrameID` and frame-line indices. `source_align` uses frame-indexed VBI extraction with no field-level navigation.

**Tests:** Label: `unit`, `transforms`. `dropout_correct`: verify correction applies to the correct frame-flat sample range using `MockVideoFrameRepresentation`. `source_align`: verify frame-indexed VBI extraction.

---

### Task: `dropout_map` and `dropout_analysis_sink`

**Files:** `orc/plugins/stages/dropout_map/`; `orc/plugins/stages/dropout_analysis_sink/`

**`dropout_map`**: user-authored dropout specifications remain in field-line-sample form (to avoid breaking existing project files). Convert internally to `DropoutRun` on load using `dropout_util::field_line_to_frame_sample()`. Expose `get_dropout_hints(FrameID)` returning `DropoutRun` vectors. PAL non-orthogonal lines must be handled correctly by the conversion utility.

**`dropout_analysis_sink`**: update iteration from field-based to frame-based. Accumulate `DropoutRun` statistics (sample_start, sample_count, severity) per frame per [design §15.3](vfr-to-cvbs-migration-design.md).

**Acceptance criteria:** `dropout_map` converts PAL field-line-sample coordinates to frame-flat correctly for lines before and after each of the four non-orthogonal positions. `dropout_analysis_sink` accumulates statistics with no field iteration logic.

**Tests:** Label: `unit`, `transforms`, `sinks`. Round-trip conversion tests for PAL (including non-orthogonal lines), NTSC, PAL_M. Per-frame statistics accumulation test.

---

## Phase 8: Pipeline Orchestration Stages

*Depends on Signal Foundation. `FrameMapStage` and `FramePhaseCorrectorStage` require `FrameDescriptor.colour_frame_index` (available after TBC source phases).*

### Task: `FrameMapStage` (renamed from `field_map`)

**Files:** rename `orc/plugins/stages/field_map/` → `orc/plugins/stages/frame_map/`

Migrate `FieldMapStage` to `FrameMapStage` per [design §11.2](vfr-to-cvbs-migration-design.md):
- Manual range specification: the range string addresses 0-based `FrameID` values. Build a `FrameID → FrameID` lookup table at configuration time; `get_frame()` resolves through this table without sample copying.
- **Duplicate frame removal** (`remove_duplicates = true`): compare consecutive `colour_frame_index` values; when two consecutive frames match, remove the second from the output sequence. Emit observation `frame_map.frames_removed`.
- **Gap padding** (`pad_gaps = true`, `pad_strategy = nearest | black`): when a break in `colour_frame_index` sequence is detected (player skip), insert synthetic padding frames with `is_padding_frame = true` and synthesised `colour_frame_index` values. Emit observation `frame_map.frames_padded` and `frame_map.gap_positions`.
- Audio: when `audio_locked() == false`, pass audio through unchanged and emit an observation warning. When `audio_locked() == true`, manipulate per-frame audio blocks in lockstep with frame manipulation.

**Acceptance criteria:** Two consecutive frames with matching `colour_frame_index` produce one output frame when `remove_duplicates = true`. Inserted padding frames carry `is_padding_frame = true` and correct synthesised colour sequence indices. Free-running audio observation is emitted when frame manipulation is applied with `audio_locked() == false`.

**Tests:** `orc-tests/core/unit/stages/frame_map/` (labels: `unit`, `transforms`). Cover: manual range reorder; duplicate removal; gap padding with both strategies; audio_locked=false warning.

---

### Task: `FramePhaseCorrectorStage` (new stage)

**Files:** create `orc/plugins/stages/frame_phase_corrector/frame_phase_corrector_stage.h/.cpp`

Implement per [design §11.1.3](vfr-to-cvbs-migration-design.md):

**Intra-frame field swap correction** (`correct_field_swap = true`): for each frame, measure burst phase of the two field blocks; if phases indicate field 2 is temporally first, present the output frame with field line blocks exchanged. This is an index redirect in `VideoFrameRepresentationWrapper` — no sample data is copied. Update output `FrameDescriptor.colour_frame_index` to the corrected value.

**Colour frame sequence verification** (`verify_phase_sequence = true`): walk the sequence; on break (index does not follow the expected standard progression): record observation; mark the break-point frame with `colour_frame_index = -1`; continue processing.

Observations emitted: `frame_phase_corrector.field_swaps_corrected`, `frame_phase_corrector.phase_breaks_detected`, `frame_phase_corrector.phase_breaks_marked`.

Parameters: `correct_field_swap` (bool, default true), `verify_phase_sequence` (bool, default true).

**Acceptance criteria:** Field swap correction performs no sample copy. A break in the sequence marks exactly one frame with `colour_frame_index = -1`. Observations are emitted for every corrected swap and detected break.

**Tests:** `orc-tests/core/unit/stages/frame_phase_corrector/` (labels: `unit`, `transforms`). Cover: no-swap pass-through; swap-corrected; PAL 4-frame break; NTSC 2-frame break.

---

### Task: `stacker` update

**Files:** `orc/plugins/stages/stacker/`

Update to `VideoFrameRepresentationWrapper`. Colour frame alignment per [design §11.4](vfr-to-cvbs-migration-design.md): stack frames at the same `colour_frame_index`; fall back to temporal alignment when `colour_frame_index == -1`. Skip frames where `is_padding_frame == true`.

Audio: when `audio_locked() == true`, average (or select from reference input) per-frame audio blocks in lockstep. When `audio_locked() == false`, pass audio from the first (reference) input VFrameR unchanged and emit an observation warning.

**Acceptance criteria:** Frames with matching `colour_frame_index` are aligned for stacking. Padding frames produce no sample accumulation. Free-running audio warning observation is emitted.

**Tests:** Label: `unit`, `transforms`. Colour alignment test; padding frame skip; audio_locked contract.

---

## Phase 9: Sink Stages

*Depends on Signal Foundation and Chroma Decoder phase.*

### Task: `ld_sink` update

**Files:** `orc/plugins/stages/ld_sink/ld_sink_stage.h/.cpp`

Implement [design §9](vfr-to-cvbs-migration-design.md):
- Accept `VideoFrameRepresentationPtr` as input.
- **Inverse level mapping** ([design §9.2](vfr-to-cvbs-migration-design.md)): determine the target TBC output levels (`tbc_white`, `tbc_blanking`) from the existing sink parameters or ld-decode format conventions (read from the existing implementation to determine the exact values — typically 54,400 for PAL white). Apply: `tbc_sample = round((cvbs_10bit - kCvbsBlanking) × (tbc_white - tbc_blanking) / (kCvbsWhite - kCvbsBlanking) + tbc_blanking)`.
- **Frame→field splitting** ([design §9.3](vfr-to-cvbs-migration-design.md)): PAL — extract field 1 (313 lines) and field 2 (312 lines); remove the 4 extra samples from 1136-sample lines to get uniform 1135-sample lines; add TBC padding (both fields padded to 313 lines). NTSC — extract field 1 (262 lines) and field 2 (263 lines); pad both to 263 lines.
- **Dropout conversion** ([design §9.4](vfr-to-cvbs-migration-design.md)): `DropoutRun` → per-field per-line via `dropout_util::frame_sample_to_field_line()`.
- **Audio** ([design §9.5](vfr-to-cvbs-migration-design.md)): PAL — write `.pcm` directly (44100 Hz). NTSC/PAL_M — resample from 44100000/1001 Hz to 44100 Hz using SoXR. Free-running — write WAV stream directly.

**Acceptance criteria:** PAL output TBC fields have exactly 1135 samples per line (all 4 extra samples removed). NTSC output fields have exactly 910 samples per line. NTSC `.pcm` output is at 44100 Hz.

**Tests:** `orc-tests/core/unit/stages/ld_sink/` (labels: `unit`, `sinks`). Cover inverse level mapping round-trip accuracy; PAL extra-sample removal; field padding; dropout coordinate conversion.

---

### Task: `CVBSSinkStage` (new stage)

**Files:** create `orc/plugins/stages/cvbs_sink/cvbs_sink_stage.h/.cpp`

Implement [design §10](vfr-to-cvbs-migration-design.md). Write:
- `<basename>.composite` (or `.y`/`.c` for YC): raw `int16_t` little-endian samples from `get_frame()` / `get_frame_luma()` / `get_frame_chroma()`.
- `<basename>.meta` SQLite: `sample_encoding_preset = 'CVBS_U10_4FSC'`; `signal_state_preset = 'STANDARD_TBC_LOCKED'` (hardcoded — this is a pipeline invariant, not a user parameter); `number_of_sequential_frames`; `audio_locked` derived from `audio_locked()` and `has_audio()`.
- `<basename>_audio_00.wav`: when `has_audio() == true`; written at the source's native locked/free-running rate.
- `<basename>.dropouts.meta`: when dropout hints are present, per the [dropout extension format](cvbs-file-format-specification/docs/extensions/dropout-extension-format.md).
- `<basename>.efm` + `<basename>.efm.meta`: when `has_efm() == true`, per the [EFM extension format](cvbs-file-format-specification/docs/extensions/efm-extension-format.md).
- `<basename>.ac3` + `<basename>.ac3.meta`: when `has_ac3_rf() == true`, per the [AC3 extension format](cvbs-file-format-specification/docs/extensions/ac3-extension-format.md).

Parameters: `output_path` (string), `signal_type` (`composite` / `yc`), `capture_notes` (string, optional; written to `.meta` if non-empty). `signal_state_preset` is not a user parameter — always written as `STANDARD_TBC_LOCKED`.

**Acceptance criteria:** `.meta` always contains `signal_state_preset = 'STANDARD_TBC_LOCKED'`. Absent extensions produce no sidecar files. A CVBS file written by this stage can be round-tripped back through the CVBS source stage (open, read frame count, verify sample count). `capture_notes` is written to `.meta` when provided and absent when not set.

**Tests:** Unit tests for metadata table contents; audio_locked propagation; sidecar absence contract.

---

### Task: Audio, EFM, and AC3 sinks

**Files:** `orc/plugins/stages/audio_sink/`; `orc/plugins/stages/efm_sink/`; `orc/plugins/stages/raw_efm_sink/`; `orc/plugins/stages/ac3rf_sink/`

Per [design §11.5 and §15.4](vfr-to-cvbs-migration-design.md):
- **`audio_sink`**: iterate by `FrameID`; when `audio_locked() == true` and system is NTSC/PAL_M, resample from 44100000/1001 Hz to 44100 Hz via SoXR before writing; when `audio_locked() == false`, write the free-running 44100 Hz WAV stream without resampling or frame iteration.
- **`efm_sink`** and **`raw_efm_sink`**: iterate by `FrameID`; use `get_efm_samples(FrameID)` per the EFM extension format.
- **`ac3rf_sink`**: iterate by `FrameID`; use `get_ac3_symbols(FrameID)` per the AC3 extension format.

**Acceptance criteria:** NTSC/PAL_M locked audio output WAV is at 44100 Hz. Free-running audio bypass path does not call SoXR. EFM/AC3 output is frame-ordered.

**Tests:** Label: `unit`, `sinks`. Audio routing test; NTSC SoXR path invocation verified by mock; EFM/AC3 frame iteration.

---

### Task: Video output sinks and analysis sinks

**Files:** `orc/plugins/stages/sinks/ffmpeg_video_sink/`; `orc/plugins/stages/sinks/raw_video_sink/`; `orc/plugins/stages/sinks/daphne_vbi_sink/`; `orc/plugins/stages/burst_level_analysis_sink/`; `orc/plugins/stages/snr_analysis_sink/`; `orc/plugins/stages/cc_sink/`; remove `orc/plugins/stages/hackdac_sink/`

- **`ffmpeg_video_sink`**: inherits `ChromaSinkStage` (updated in Chroma Decoder phase); add NTSC/PAL_M audio resampling to 44100 Hz (SoXR) before FFmpeg embed; update CC line coordinates using `broadcast_line_to_frame_line()` from the line numbering utility. Derive output display aspect ratio from `SourceParameters.active_video_start`, `active_video_end`, `first_active_frame_line`, and `last_active_frame_line` per BT.601-5 §2 (PAL: 59/54, NTSC: 10/11); do not hardcode these values.
- **`raw_video_sink`**: inherits `ChromaSinkStage`; no audio concerns beyond the base.
- **`daphne_vbi_sink`**: update field iteration → frame iteration; VBI line coordinates via `broadcast_line_to_frame_line()`.
- **`burst_level_analysis_sink`**: frame iteration; burst amplitude references from `cvbs_signal_constants.h` (not `SourceParameters`) per [design §15.5](vfr-to-cvbs-migration-design.md).
- **`snr_analysis_sink`**: level references from `cvbs_signal_constants.h` per [design §11.5](vfr-to-cvbs-migration-design.md).
- **`cc_sink`**: frame iteration; VBI line coordinates via `broadcast_line_to_frame_line()`.
- **`hackdac_sink`**: delete the entire stage directory; update the plugin registry to remove its entry.

All four analysis/VBI stages must document explicitly, with a comment citing [design §11.6.6](vfr-to-cvbs-migration-design.md), whether they respect or ignore `active_area_cropping_applied`: `cc_sink` and `daphne_vbi_sink` must **ignore** the crop and always read from spec-defined VBI line coordinates; `burst_level_analysis_sink` and `snr_analysis_sink` must **respect** the crop and restrict measurement to the declared active region.

**Acceptance criteria:** `hackdac_sink` plugin ID is absent from the registry. `daphne_vbi_sink` iterates by `FrameID`. `cc_sink` uses `broadcast_line_to_frame_line()`. `burst_level_analysis_sink` uses spec constants for level calibration. Every analysis sink carries a comment on crop independence per [design §11.6.6](vfr-to-cvbs-migration-design.md).

**Tests:** Label: `unit`, `sinks`. VBI broadcast line numbering conversion; CC sink frame iteration; burst level spec constant usage (verify no `SourceParameters` level access in SNR/burst analysis); confirm `cc_sink` reads the correct VBI line even when `active_area_cropping_applied = true` and crop excludes that line.

---

## Phase 10: GUI Viewing and Analysis Tools

*Depends on Navigation and Rendering Infrastructure phase.*

### Task: `FrameScopeDialog` (renamed from `LineScopeDialog`)

**Files:** rename `orc/gui/linescopedialog.h/.cpp` → `orc/gui/framescopedialog.h/.cpp`

Implement [design §7.2.2](vfr-to-cvbs-migration-design.md):
- Navigation: `(field_index, line_number)` → `(FrameID frame_id, size_t frame_line)`.
- Sample type: `std::vector<uint16_t>` → `std::vector<int16_t>`.
- Millivolt conversion — remove the hardcoded `7 mV/IRE` and `7.143 mV/IRE` constants; replace with:
  ```cpp
  // ITU-R BT.1700-1 / SMPTE 170M-2004 §11.4
  double active_mv = (system == VideoSystem::PAL) ? 700.0 : 714.3;
  double mv = static_cast<double>(sample - blanking_level) / (white_level - blanking_level) * active_mv;
  ```
- Reference level markers: horizontal lines at all five normative levels from [design §3.2](vfr-to-cvbs-migration-design.md), labelled with standard name and mV value.
- PAL variable line length: display the correct sample count per line (1135 or 1136); do not assume a fixed width.
- Y-axis: extend below 0 mV (to sync tip and below) and above 100 IRE (to peak and beyond); no arbitrary clamping of the display range.
- Line numbering mode selector: drop-down or radio group with four modes using `LineNumberingMode` from the navigation infrastructure phase. The internal state `(frame_id, frame_line)` does not change when the mode is switched; only the displayed label changes. Store the selected mode as a persistent GUI preference per video system.

**Acceptance criteria:** No hardcoded mV-per-IRE constants remain. Y-axis does not clamp int16 samples outside [4, 1019]. Switching the numbering mode does not alter the selected frame line.

**Tests:** Tier-1 `gui-logic` tests (label: `gui-logic`) for `cvbs_sample_to_mv` math and all four `LineNumberingMode` label conversions. Tier-3 `gui-widget` smoke test (label: `gui-widget`) for dialog construction and mode selector widget existence.

---

### Task: `FrameTimingDialog` and `qualitymetricsdialog`

**Files:** rename `orc/gui/fieldtimingdialog.h/.cpp` → `orc/gui/frametimingdialog.h/.cpp`; same for associated widget files; update `orc/gui/qualitymetricsdialog.h/.cpp`

**`FrameTimingDialog`** per [design §7.3.2](vfr-to-cvbs-migration-design.md):
- Display `colour_frame_index` from `FrameDescriptor`: PAL shows position within the 4-frame cycle (1–4); NTSC shows A or B.
- Display both fields' line counts within the frame.
- Frame rate and video system (PAL / NTSC / PAL_M).
- Line references use `LineNumberingMode` from the navigation infrastructure phase (shared preference with `FrameScopeDialog`).

**`qualitymetricsdialog`** per [design §14.8](vfr-to-cvbs-migration-design.md): update to display frame-based metrics. Level readings previously in 16-bit TBC units must now display in the CVBS_U10_4FSC 10-bit domain or as spec-derived mV/IRE using the same conversion as `FrameScopeDialog`.

**Acceptance criteria:** `FrameTimingDialog` shows `colour_frame_index` from `FrameDescriptor`. Quality metrics show 10-bit values; no TBC 16-bit values remain in any displayed field.

**Tests:** Tier-2 `gui-model` test (label: `gui-model`) for `FrameDescriptor` display logic in `FrameTimingDialog`. Tier-3 smoke test (label: `gui-widget`) for both dialogs.

---

### Task: Vectorscope constant audit and BT.601 PAL reference vectors

**Files:** `orc/core/analysis/vectorscope/vectorscope_analysis.h/.cpp`; `orc/gui/preview/vectorscope_geometry.h`; `orc/gui/preview/vectorscope_dialog.h`

Implement [design §7.4](vfr-to-cvbs-migration-design.md):
- Audit every numeric constant in `vectorscope_geometry.h`; replace with derivations from `cvbs_signal_constants.h` or analytically computed values; add specification citations for each constant.
- PAL demodulation constants per EBU Tech. 3280-E §1.2: sampling at 45°/135°/225°/315° relative to the +U axis; PAL V-axis sign alternation every line.
- NTSC demodulation per SMPTE 244M-2003 §4.1.2: I-axis reference alignment.
- PAL colour bar reference vectors: derive from BT.601 matrix and 75% amplitude / 100% saturation colour bars per ITU-R BT.601-5 Table 2 (BT.601 — see resolved design decision above). The derivation matrix and each step of the calculation must be present as a comment with citations.
- NTSC reference vectors: SMPTE 170M-2004 primaries.
- `extractFromCompositeRepresentation()`: update to accept `VideoFrameRepresentation` and `FrameID`; process both fields; handle PAL V-axis alternation at the frame level.

**Acceptance criteria:** Zero untraced constants remain in `vectorscope_geometry.h`. PAL demodulation phases match EBU Tech. 3280-E §1.2 exactly. A BT.601 citation is present for every PAL reference vector computation. The display auto-scales when input samples carry values outside `[sync_tip, peak]` per [design §3.5](vfr-to-cvbs-migration-design.md); normative reference markers remain at their correct positions regardless of the current scale.

**Tests:** Unit tests for PAL demodulation phases at known sample positions; PAL reference vector coordinates against analytically computed BT.601 values; display range expansion when headroom samples (below sync tip or above peak) are present in a synthetic input frame.

---

## Phase 11: Project Schema, SDK ABI, and Version Bump

*Depends on all source stage phases for correct stage name validation at project load.*

### Task: Project format version 2.0

**Files:** `orc/core/include/project.h`; `orc/core/project.cpp` (load at line ~474; serialise at line ~675)

Implement [design §12](vfr-to-cvbs-migration-design.md):
- Serialise `version: "2.0"` in all new project files.
- On load: if `version` is not `"2.0"`, hard-reject before any stage is instantiated:
  ```
  Project format version '1.0' is not supported by Decode-Orc 2.x.
  Please recreate the project using Decode-Orc 2.0 or later.
  ```
  Do not hint at a migration command that does not exist (see resolved decisions).
- Enforce `video_format` and `source_type` as normative declarations at all four levels per [design §12.3](vfr-to-cvbs-migration-design.md):
  1. **Project creation** (GUI): `video_format` and `source_type` are required fields; no pipeline can be built until both are set; both are written to YAML at creation and are read-only thereafter.
  2. **Stage addition**: the stage picker only shows source stages whose video system and signal type both match the project.
  3. **Project load validation**: every source stage node is validated against `video_format` and `source_type`; a mismatch hard-rejects the project with an explicit message identifying the offending stage.
  4. **Runtime assertion**: `SourceParameters.system` and source stage signal type are compared against project declarations at pipeline start; a mismatch halts execution.
- Update source stage names in project YAML per [design §12.2](vfr-to-cvbs-migration-design.md).

**Acceptance criteria:** Opening a v1.x `.orc-project` file produces the rejection message without crashing. A project with `video_format: PAL` containing an NTSC source stage is rejected at load time with a message identifying the offending node.

**Tests:** `orc-tests/core/unit/project/` (label: `unit`). Cover: v1.x rejection; v2.0 accept; video_format mismatch rejection; source_type mismatch rejection.

---

### Task: SDK ABI version bump and documentation update

**Files:** `orc/sdk/include/orc/plugin/orc_plugin_abi.h`; `docs/technical/plugin-architecture.md`; `docs/technical/plugin-sdk.md`

Per [design §13.2](vfr-to-cvbs-migration-design.md) and AGENTS.md §9:
- Bump `kStagePluginHostAbiVersion` and `kStagePluginApiVersion`.
- Update version compatibility tables in both SDK documentation files.
- Update `IStageServices` interface documentation for any new VFrameR-related methods added in earlier phases.

**Acceptance criteria:** SDK enforcement gates pass: `ctest --test-dir build -L sdk --output-on-failure`. Version tables in both documentation files are updated. v1.x plugins will not load (ABI version mismatch prevents this automatically).

**Tests:** Run `ctest --test-dir build -R "StagePluginLoader" --output-on-failure` and `ctest --test-dir build -L sdk --output-on-failure`.

---

### Task: User-facing documentation review and update

**Files:** `docs/user-guide/`; `docs/stages/`; in-app help strings in `orc/gui/`; `CHANGELOG.md` (or equivalent release notes file)

Per [design §1.2](vfr-to-cvbs-migration-design.md), review and revise all user-facing documentation before the v2.0 release tag is applied:

- Replace all field-based terminology with frame-based equivalents per the terminology table in [design §2](vfr-to-cvbs-migration-design.md): `FieldID` → `FrameID`, field scope → frame scope, field map → frame map, field timing → frame timing, and so on throughout every guide and help string.
- Document the removed `hackdac_sink` stage and the five retired legacy TBC source plugin IDs (`pal_comp_source`, `pal_yc_source`, `ntsc_comp_source`, `ntsc_yc_source`, `pal_m_tbc_comp_source`).
- Document renamed dialogs and stages: `LineScopeDialog` → `FrameScopeDialog`; `FieldTimingDialog` → `FrameTimingDialog`; `field_invert` → `frame_field_swap`; `field_map` → `frame_map`.
- Document v2.0 project format requirements: `video_format` and `source_type` are required at project creation and are read-only thereafter; v1.x project files are hard-rejected with the specified error message.
- Document the new `FramePhaseCorrectorStage` (parameters, observations, recommended pipeline position) and the updated `FrameMapStage` parameters (`remove_duplicates`, `pad_gaps`, `pad_strategy`).
- Update in-application help strings in `orc/gui/` for all renamed dialogs and any parameter descriptions that reference field-level concepts.
- Add a v2.0 entry to `CHANGELOG.md` noting the breaking change, all removed stages, all renamed stages, and the project file format change.

**Acceptance criteria:** No user-facing text refers to `FieldID`, field scope, field map, `field_invert`, `hackdac_sink`, or any other removed or renamed entity by its old name. The v2.0 project format change and v1.x rejection are documented. `CHANGELOG.md` has a v2.0 entry. This task must be complete before the v2.0 release tag is applied.

**Tests:** No automated tests. Review is manual; a documentation review checklist must be signed off before tagging.

---

### Task: Application version bump to 2.0.0

**Files:** `CMakeLists.txt` (top-level `project(orc VERSION 2.0.0)`)

Update the version number. Verify propagation: `build/generated/version.h` reports `2.0.0`; the About dialog shows 2.0.0; Flatpak manifest, macOS DMG, and Windows MSI packaging workflows produce artifacts labelled `2.0.0`.

**Acceptance criteria:** `version.h` reports `2.0.0`. This is the final task in the migration.

**Tests:** None beyond the build producing the correct version.h value.
