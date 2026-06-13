# Changelog

## v2.0.0

### Breaking changes

**Project file format v2.0 (incompatible with v1.x)**

Project files now carry `version: "2.0"`. Files saved by Decode-Orc 1.x are hard-rejected at load time with the message:

> Project format version '1.x' is not supported by Decode-Orc 2.x.
> Please recreate the project using Decode-Orc 2.0 or later.

There is no automated migration path. Projects must be recreated in Decode-Orc 2.0.

**Signal representation: CVBS_U10_4FSC**

All internal video data is now carried in the CVBS_U10_4FSC 10-bit domain. The 16-bit IRE-based domain used in v1.x has been removed. This change affects every processing stage, every signal-level constant, and the stage parameter domain for level overrides.

**Plugin ABI and API version bump**

Plugins built against Decode-Orc 1.x (ABI version 3, API version 1) will not load in 2.0. The new versions are ABI 4 / API 2. All plugins must be rebuilt against the v2.0 SDK.

**Removed stages**

| Stage ID | Reason |
|----------|--------|
| `hackdac_sink` | Hardware-specific stage removed from the core distribution |
| `pal_comp_source` | Retired; replaced by unified `tbc_source` |
| `pal_yc_source` | Retired; replaced by unified `tbc_source` |
| `ntsc_comp_source` | Retired; replaced by unified `tbc_source` |
| `ntsc_yc_source` | Retired; replaced by unified `tbc_source` |
| `pal_m_tbc_comp_source` | Retired; replaced by unified `tbc_source` |

**Renamed stages**

| Old stage ID | New stage ID |
|-------------|--------------|
| `field_map` | `frame_map` |
| `field_invert` | `frame_field_swap` |

**Renamed GUI dialogues**

| Old name | New name |
|----------|----------|
| Line-scope | Frame-scope |
| Field-timing | Frame-timing |

---

### New features

**`tbc_source` — unified TBC source stage**

A single `tbc_source` plugin replaces the five previous system-specific TBC source stages. The video system and signal type (composite or Y/C) are detected automatically from the `.tbc.json.db` metadata at open time. Display name is set to e.g. `PAL TBC Composite` or `NTSC TBC YC`. Supports PAL, NTSC, and PAL-M.

**`cvbs_source` — CVBS file source stage**

Reads CVBS composite payloads from `.composite` files. Normalises `CVBS_U10_4FSC`, `CVBS_U16_4FSC`, `CVBS_TPG21_4FSC`, and `CVBS_S16_FSC` encodings automatically. Requires `STANDARD_TBC_LOCKED` signal state; rejects non-standard files before any frame data is returned. Loads dropout, audio, EFM, and AC3 sidecars automatically when present.

**`cvbs_sink` — CVBS file sink stage**

Writes CVBS_U10_4FSC frame data to the CVBS file-format family. Always writes `signal_state_preset = 'STANDARD_TBC_LOCKED'` and `sample_encoding_preset = 'CVBS_U10_4FSC'`. Round-trips cleanly with `cvbs_source`. Writes dropout, audio, EFM, and AC3 sidecars when the upstream source provides them.

**`frame_phase_corrector` — new transform stage**

Detects and corrects intra-frame field-swap artefacts by measuring colour burst phase in each field block. Also verifies PAL 4-frame / NTSC 2-frame colour-frame sequence continuity and marks breaks with `colour_frame_index = -1`. No sample data is copied during correction.

**`frame_map` enhancements**

The renamed `frame_map` (formerly `field_map`) adds two new capabilities:

- `remove_duplicates`: removes the second of any two consecutive frames with matching `colour_frame_index` values.
- `pad_gaps` / `pad_strategy`: inserts synthetic padding frames when a player-skip break in the colour-frame sequence is detected.

**Project video_format and source_format enforcement**

Projects now declare a `video_format` (PAL / NTSC / PAL-M) and `source_format` (Composite / YC) at creation time. These fields are read-only after creation. The stage picker shows only compatible source stages. Source stages in a loaded project file that conflict with the declared format are rejected at load time with a message identifying the offending stage node.

**Frame-scope dialogue**

Replaces the Line-scope. New capabilities in v2.0:

- Four line numbering modes (frame-flat 0-based, frame sequential 1-based, field-relative, broadcast interlaced) with a persistent per-video-system preference.
- Y-axis extends below 0 mV (to sync tip) and above 100 IRE (to peak) with no arbitrary clamping.
- All five normative reference level markers (sync tip, blanking, black, white, peak) with mV labels derived from ITU-R BT.1700-1 / SMPTE 170M-2004.
- Correct sample count per line for PAL (1135 or 1136 at non-orthogonal positions).

**Frame-timing dialogue**

Replaces the Field-timing dialogue. Now displays `colour_frame_index` from `FrameDescriptor` (PAL 1–4, NTSC A/B), field line counts per frame, and padding-frame indicators from `frame_map`.

---

### Internal changes (affects plugin authors)

- `VideoFrameRepresentation` replaces `VideoFieldRepresentation` as the primary frame-data interface.
- `FrameID` / `FrameIDRange` replace `FieldID` / `FieldIDRange` everywhere.
- `DropoutRun` (frame-flat sample offset + count + severity) replaces `DropoutRegion` (field/line/sample).
- `SourceParameters` carries frame-geometry and 10-bit signal-level fields; old 16-bit IRE fields removed.
- `FrameDescriptor` carries `colour_frame_index`, `black_level_override`, `is_padding_frame`.
- `dropout_util` provides conversion between frame-flat sample offsets and field/line/sample coordinates.
