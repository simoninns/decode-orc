# SourceParameters Deprecated Field Removal Plan

## Background

`orc/view-types/orc_source_parameters.h` carries a `// DEPRECATED` block of 13
fields that were kept alive during the VFR→CVBS migration (Phases 1–11) to
avoid breaking every consumer at once.  Now that the new CVBS-domain fields
(`frame_width_nominal`, `frame_height`, `blanking_level`, `black_level`,
`white_level`, `first_active_frame_line`, `last_active_frame_line`) are in place
and the old VFR source stages have been deleted, those deprecated fields can be
systematically removed.

The deprecated fields and their current consumer counts are:

| Field | Type | Consumer files |
|---|---|---|
| `is_first_field_first` | `bool` | 0 — can be deleted immediately |
| `field_width` | `int32_t` | ~18 |
| `field_height` | `int32_t` | ~18 |
| `number_of_sequential_fields` | `int32_t` | 5 |
| `colour_burst_start` | `int32_t` | ~10 |
| `colour_burst_end` | `int32_t` | ~10 |
| `first_active_field_line` | `int32_t` | ~7 |
| `last_active_field_line` | `int32_t` | ~7 |
| `blanking_16b_ire` | `int32_t` | ~6 |
| `black_16b_ire` | `int32_t` | ~20 |
| `white_16b_ire` | `int32_t` | ~20 |
| `sample_rate` | `double` | ~10 |
| `fsc` | `double` | ~5 |
| `is_subcarrier_locked` | `bool` | 3 |

## Canonical replacements

| Deprecated field | Replace with |
|---|---|
| `field_width` | `frame_width_nominal` (already in struct) |
| `field_height` | `calculate_padded_field_height(system)` (`common_types.h`) |
| `number_of_sequential_fields` | `number_of_sequential_frames * 2` |
| `colour_burst_start` | new spec constant `kPalColourBurstStart` / `kNtscColourBurstStart` |
| `colour_burst_end` | new spec constant `kPalColourBurstEnd` / `kNtscColourBurstEnd` |
| `first_active_field_line` | `first_active_frame_line / 2` |
| `last_active_field_line` | `last_active_frame_line / 2` |
| `blanking_16b_ire` | `kTbcBlanking` (already in `cvbs_signal_constants.h`) |
| `black_16b_ire` | `kTbcBlanking` (transitional) → `black_level` mapped to TBC domain (final) |
| `white_16b_ire` | `kTbcWhite` (transitional) → `white_level` mapped to TBC domain (final) |
| `sample_rate` | `sample_rate_from_system(system)` helper (to be added) |
| `fsc` | `fsc_from_system(system)` helper (to be added) |
| `is_subcarrier_locked` | move to `TBCSourceMetadata` struct, not `SourceParameters` |

---

## Phase 1 — Remove `is_first_field_first` (trivial)

**Effort**: Tiny — one-line struct deletion, no consumers.

### Work items

1. Delete `bool is_first_field_first = true;` from `orc_source_parameters.h`.

### Verification

Build must pass cleanly.  `grep -r is_first_field_first` must return zero hits
outside the git history.

---

## Phase 2 — Field geometry: `field_width` and `field_height`

**Effort**: Medium-large — mechanical search-and-replace across the chroma
decoder, GUI, preview, and metadata layers, but no algorithmic changes.

### Context

`field_width` equals `frame_width_nominal` — both represent samples per line.
`field_height` equals `calculate_padded_field_height(system)` — the longer of
the two interlaced fields (313 PAL, 263 NTSC/PAL\_M), which is the uniform
padded storage height used by `ld-decode` and carried through to the chroma
decoder.

The mapping `frame_height = field_height * 2 - 1` used extensively in the
chroma decoder becomes `calculate_padded_field_height(system) * 2 - 1`.

### Files to update

**Chroma decoder pipeline** (`orc/plugins/stages/sinks/common/decoders/`):
- `componentframe.cpp` — `field_width`, `field_height`
- `palcolour.cpp` — `field_width`, `field_height`
- `comb.cpp` — `field_width`, `field_height`
- `transformpal2d.cpp` — `field_width`, `field_height`
- `transformpal3d.cpp` — `field_width`, `field_height`
- `outputwriter.cpp` — `field_width`, `field_height`

**Parameter safety / sink common** (`orc/plugins/stages/sinks/common/`):
- `video_parameter_safety.h` — many uses of `field_width` / `field_height`;
  replace guards and clamps with derived values from `system` and
  `frame_width_nominal`.

**Stage plugins**:
- `tbc_source/tbc_source_stage.cpp` — sets `tvp.field_width` /
  `tvp.field2_height` / `tvp.field1_height`; read from new fields instead.

**Core**:
- `preview_helpers.cpp` — `field_width`, `field_height`
- `metadata/tbc_metadata.cpp` — reads `field_width`, `field_height` from DB;
  change to derive from `system` column after read.
- `metadata/tbc_metadata_writer.cpp` — writes `field_width`, `field_height`;
  drop the DB columns (requires schema migration, see below).
- `metadata/tbc_metadata_json_reader.cpp` — reads `fieldWidth`, `fieldHeight`
  from JSON; continue reading for back-compat but do not propagate to struct.

**GUI**:
- `gui/fieldtimingwidget.cpp` — replace `video_params_->field_width` /
  `video_params_->field_height` with `frame_width_nominal` /
  `calculate_padded_field_height(system)`.
- `gui/mainwindow.cpp` — same substitution.
- `gui/render_coordinator.cpp` — passes `field_height` as a parameter; change
  call site.

**Presenter / view models**:
- `presenters/src/hints_view_models.cpp` — populate `field_width` /
  `field_height` from new fields.
- `presenters/include/hints_view_models.h` — replace `field_width` /
  `field_height` with `line_width` / `padded_field_height` (rename the view
  model members too so the GUI reflects the correct semantics).

### Database schema note

`tbc_metadata_writer.cpp` currently writes `field_width` and `field_height` to
columns 8 and 9 of the `video_parameters` table.  Two options:

- **Keep columns, ignore on read** — continue writing the values (derived from
  `frame_width_nominal` and `system`) so existing `.db` files remain readable.
  This is the lower-risk path.
- **Drop columns** — requires a schema version bump and a migration statement in
  the reader.  Preferred long-term but out of scope for this phase.

Recommendation: keep the DB columns but compute their values from the canonical
fields, removing the struct fields.

---

## Phase 3 — `sample_rate` and `fsc` from `VideoSystem`

**Effort**: Medium — add two helper functions, then search-and-replace.

### Context

Both values are determined entirely by the video system:

| System | `sample_rate` | `fsc` |
|---|---|---|
| PAL | `kPalSampleRate` (17 734 475.0) | `kPalFsc` (4 433 618.75) |
| NTSC | `kNtscSampleRate` | `kNtscFsc` |
| PAL\_M | `kPalMSampleRate` | `kPalMFsc` |

Reading them from per-disc metadata and re-storing them in `SourceParameters`
adds round-trip noise for no benefit — the chroma decoder can compute the
correct filter coefficients directly from `VideoSystem`.

### Work items

1. Add to `cvbs_signal_constants.h`:
   ```cpp
   inline double sample_rate_from_system(VideoSystem sys) {
     switch (sys) {
       case VideoSystem::PAL:   return kPalSampleRate;
       case VideoSystem::PAL_M: return kPalMSampleRate;
       default:                 return kNtscSampleRate;
     }
   }
   inline double fsc_from_system(VideoSystem sys) {
     switch (sys) {
       case VideoSystem::PAL:   return kPalFsc;
       case VideoSystem::PAL_M: return kPalMFsc;
       default:                 return kNtscFsc;
     }
   }
   ```

2. Replace `params.sample_rate` / `params.fsc` reads in:
   - `sinks/common/decoders/palcolour.cpp`
   - `sinks/common/decoders/comb.cpp`
   - `sinks/common/video_parameter_safety.h`
   - `core/analysis/vectorscope/vectorscope_analysis.cpp`
   - `core/observers/biphase_observer.cpp`
   - `core/observers/fm_code_observer.cpp`
   - `gui/framescopedialog.cpp`
   - `gui/hintsdialog.cpp`
   - `presenters/src/hints_view_models.cpp`

3. Update `tbc_metadata_writer.cpp` — continue writing `sample_rate` to the DB
   for back-compat but compute from `system`; `fsc` column can be dropped.

4. Update `tbc_metadata_json_reader.cpp` — read `sampleRate` from JSON for
   validation only; do not propagate to `SourceParameters`.

5. Update `tbc_metadata.cpp` — derive `sample_rate` from `system` after reading.

6. Delete `double sample_rate` and `double fsc` from `orc_source_parameters.h`.

---

## Phase 4 — Active-area field-line coordinates

**Effort**: Small — three write sites, five read sites, all mechanical.

### Context

`first_active_field_line` and `last_active_field_line` are field-relative
(0-based within one field).  Their frame-flat equivalents
`first_active_frame_line` and `last_active_frame_line` are already present in
`SourceParameters` and are set by `tbc_source` and `cvbs_source`.

The relationship is:
```
first_active_field_line = first_active_frame_line / 2  (integer division)
last_active_field_line  = last_active_frame_line  / 2
```

### Work items

1. Delete the two write sites in `tbc_source_stage.cpp` (lines 324–325) and
   `cvbs_source_stage.cpp` (lines 485–486) — the frame-flat values already
   propagated.

2. At read sites, replace `params.first_active_field_line` with
   `params.first_active_frame_line / 2` and similarly for last:
   - `sinks/common/video_parameter_safety.h`
   - `core/observers/burst_level_observer.cpp`
   - `sinks/common/chroma_sink_stage.cpp`
   - `core/metadata/tbc_metadata.cpp`

3. Update `tbc_metadata_json_reader.cpp` — continue reading `firstActiveFieldLine`
   / `lastActiveFieldLine` from the JSON for back-compat, but map immediately to
   `first_active_frame_line` / `last_active_frame_line` (multiply by 2).

4. Delete `first_active_field_line` and `last_active_field_line` from the struct.

---

## Phase 5 — Colour burst sample range

**Effort**: Medium — add spec constants, then update all consumers.

### Context

`colour_burst_start` and `colour_burst_end` are sample offsets within a TBC
line that locate the colour burst.  In the new CVBS\_U10\_4FSC pipeline these
are fixed by the video standard:

| System | Burst start | Burst end |
|---|---|---|
| PAL | 98 | 138 |
| NTSC / PAL\_M | 72 | 108 |

*(Values taken from EBU Tech. 3280-E Table 1 and SMPTE 244M-2003 Table 1.)*

The per-disc values stored in `.tbc.json.db` can deviate by a few samples for
damaged or non-standard tapes, but the spec-defined ranges are sufficient for
the dropout-correction masking and observer windowing that currently consume
these fields.

### Work items

1. Add to `cvbs_signal_constants.h`:
   ```cpp
   constexpr int32_t kPalColourBurstStart  = 98;
   constexpr int32_t kPalColourBurstEnd    = 138;
   constexpr int32_t kNtscColourBurstStart = 72;
   constexpr int32_t kNtscColourBurstEnd   = 108;
   // PAL_M uses same values as NTSC.
   ```

2. Add helper:
   ```cpp
   inline std::pair<int32_t,int32_t> colour_burst_range(VideoSystem sys) {
     if (sys == VideoSystem::PAL)
       return {kPalColourBurstStart, kPalColourBurstEnd};
     return {kNtscColourBurstStart, kNtscColourBurstEnd};
   }
   ```

3. Replace reads in:
   - `sinks/common/video_parameter_safety.h`
   - `sinks/common/decoders/palcolour.cpp`
   - `sinks/common/decoders/comb.cpp`
   - `plugins/stages/dropout_correct/dropout_correct_stage.cpp`
   - `core/observers/closed_caption_observer.cpp`
   - `core/observers/burst_level_observer.cpp`
   - `gui/mainwindow.cpp`
   - `presenters/src/hints_view_models.cpp`

4. Update `tbc_metadata_json_reader.cpp` and `tbc_metadata.cpp` — continue
   reading from JSON / DB for validation / display, but do not propagate to
   `SourceParameters`.

5. Delete `colour_burst_start` and `colour_burst_end` from the struct.

---

## Phase 6 — Sequential field count

**Effort**: Small — three code sites plus a metadata consideration.

### Context

`number_of_sequential_fields` = `number_of_sequential_frames * 2`.  The frame
count is already present as `number_of_sequential_frames` in `SourceParameters`.

The `.db` file schema stores `number_of_sequential_fields` as a column that the
metadata reader validates against the actual field-record count.  The `ld_sink`
currently writes it by assigning to the deprecated field before calling
`write_video_parameters`.

### Work items

1. Change `tbc_metadata_writer.h/.cpp` to accept field count as a separate
   `int32_t field_count` parameter to `write_video_parameters`, independent of
   `SourceParameters`.  The DB column is retained for schema compatibility.

2. Update `ld_sink_stage_deps.cpp` to pass `expected_field_count` directly to
   the new overload instead of writing to the deprecated struct field.

3. Update `tbc_source_stage.cpp` — replace the read
   `tvp.number_of_fields = sp->number_of_sequential_fields` with
   `sp->number_of_sequential_frames * 2`.

4. Update `tbc_metadata.cpp` and `tbc_metadata_json_reader.cpp` — continue
   reading the field count for DB integrity checking, but derive it from the
   frame count if possible; do not propagate to `SourceParameters`.

5. Delete `number_of_sequential_fields` from the struct.

---

## Phase 7 — `is_subcarrier_locked`

**Effort**: Tiny — move to metadata, not SourceParameters.

### Context

`is_subcarrier_locked` is a per-disc flag that comes from `.tbc.json.db` and is
used only by the metadata writer (for round-trip fidelity to the legacy format).
It has no meaning in the CVBS pipeline and should not be on `SourceParameters`.

### Work items

1. Add `bool is_subcarrier_locked` to a new `TBCFileMetadata` struct (or to
   `TBCSourceMetadata` if one already exists) in the metadata layer.

2. Thread `is_subcarrier_locked` from `tbc_metadata_json_reader` /
   `tbc_metadata` through to `tbc_metadata_writer` via this new struct instead
   of through `SourceParameters`.

3. Remove the field from `SourceParameters`.

---

## Phase 8 — 16-bit IRE signal levels (most complex)

**Effort**: Large — touches the entire chroma decoder pipeline, preview helpers,
all signal-level observers, the vectorscope, and the GUI.

### Context

`black_16b_ire` and `white_16b_ire` are TBC 16-bit domain signal levels
(blanking ≈ 16384, white ≈ 54400).  They are used in two distinct ways:

**A — Y-range normalisation in the chroma decoder** (`comb`, `palcolour`,
`outputwriter`, `framecanvas`, `transformpal2d/3d`, `ffmpeg_output_backend`,
`preview_helpers`, `colour_preview_conversion`):
```cpp
double yRange   = white_16b_ire - black_16b_ire;   // ≈ 38016
double yOffset  = black_16b_ire;                    // ≈ 16384
double yNorm    = (sample - yOffset) / yRange;
```
These decoders work entirely in the 16-bit TBC sample domain; the
`ComponentFrame` / `y_plane` data they produce is still in that domain.

**B — IRE display and observer thresholds** (`burst_level_observer`,
`black_psnr_observer`, `white_snr_observer`, `white_flag_observer`,
`fm_code_observer`, `biphase_observer`, `vectorscope_dialog`):
```cpp
double ire_per_unit = 100.0 / (white_16b_ire - black_16b_ire);
```

**Two migration paths exist:**

### Path A — Map CVBS levels to TBC domain (lower-risk, transitional)

Use `kTbcBlanking` and `kTbcWhite` constants (already in
`cvbs_signal_constants.h`) as fixed replacements.  The chroma decoder pipeline
does not change its internal domain; non-standard signal levels are lost (they
are already normalized at the CVBS stage).

Steps:
1. In `tbc_source_stage.cpp`, replace `tvp.blanking_16b = sp->blanking_16b_ire`
   and `tvp.white_16b = sp->white_16b_ire` with `kTbcBlanking` / `kTbcWhite`.
2. In `chroma_sink_stage.cpp`, replace reads of `videoParams.black_16b_ire` /
   `videoParams.white_16b_ire` with `kTbcBlanking` / `kTbcWhite`.
3. In all observer files, replace the IRE-range computation with
   `100.0 / (kTbcWhite - kTbcBlanking)`.
4. In preview and colour conversion helpers, replace local `blackIRE` /
   `whiteIRE` variables with the constants.
5. In `vectorscope_dialog.cpp`, replace reads from `SourceParameters` with the
   constants; keep the dialog's display accurate.
6. In `tbc_metadata_writer.cpp`, write `kTbcBlanking` / `kTbcWhite` for DB
   round-trip; in the reader, skip populating the deprecated fields.
7. Delete the three fields from `SourceParameters`.

This path can be done without changing the chroma decoder's internal
representation.  Non-standard signal levels (NTSC-J, washed-out sources) will
produce identical output to before because they were already normalised to
CVBS constants at the `tbc_source` or `cvbs_source` ingestion boundary.

### Path B — Convert the chroma decoder to CVBS 10-bit domain (preferred long-term)

The `ComponentFrame` / `y_plane` outputs would be redefined as CVBS 10-bit
values.  All downstream normalisation uses `black_level` / `white_level` (10-bit
domain) from `SourceParameters`.

Steps (build on Path A):
1. Complete Path A first.
2. Change the chroma decoder's internal accumulation buffers to work in 10-bit
   scale.  Replace `yRange ≈ 38016` with `white_level - black_level ≈ 562` (PAL)
   and scale the filter coefficients accordingly.
3. Update `outputwriter.cpp`, `framecanvas.cpp`, `ffmpeg_output_backend.cpp` to
   expect 10-bit Y range.
4. Update `colour_preview_conversion.cpp` and `preview_helpers.cpp` to use
   `black_level` / `white_level`.
5. Update all IRE-range computations in the observer layer to use
   `100.0 / (white_level - black_level)`.

Path B produces correct output for non-standard signal levels and aligns the
entire pipeline on the CVBS domain, at the cost of a larger numerical refactor.

**Recommendation**: Implement Path A first to remove the deprecated fields, then
follow with Path B as a standalone chroma-decoder modernisation task.

---

## Sequencing and dependencies

```
Phase 1  (is_first_field_first)   — no dependencies
Phase 2  (field_width/height)     — no dependencies; largest churn
Phase 3  (sample_rate, fsc)       — no dependencies
Phase 4  (active field lines)     — no dependencies
Phase 5  (colour burst range)     — no dependencies
Phase 6  (sequential field count) — depends on Phase 2 (field_height removed first)
Phase 7  (is_subcarrier_locked)   — no dependencies
Phase 8A (16b IRE — transitional) — no dependencies; can run in parallel with 2–7
Phase 8B (16b IRE — full CVBS)    — depends on Phase 8A
```

Phases 1–7 (excluding Phase 8) can each be done as an independent PR.  Phase 8A
should be done after Phases 2 and 3 are merged so the constants infrastructure
is in place.

---

## Definition of done

The `// DEPRECATED` block in `orc_source_parameters.h` is deleted in its
entirety.  Running `grep -r 'field_width\|field_height\|number_of_sequential_fields\|colour_burst_start\|colour_burst_end\|first_active_field_line\|last_active_field_line\|blanking_16b_ire\|black_16b_ire\|white_16b_ire\|sample_rate\|fsc\b\|is_subcarrier_locked\|is_first_field_first' orc/ --include="*.cpp" --include="*.h"` returns zero hits outside `tbc_metadata_json_reader.cpp` (which retains back-compat reads from the legacy JSON format).

All existing tests continue to pass.  The MVP architecture check (`ctest -R MVPArchitectureCheck`) passes.
