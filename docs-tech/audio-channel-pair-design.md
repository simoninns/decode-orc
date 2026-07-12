# Channel-Pair Audio Design (SMPTE 272M Compliance)

Design for migrating decode-orc's multi-track audio pipeline to the updated
CVBS file format specification, which now mandates SMPTE 272M-1994-compliant
audio: 48 kHz synchronous (frame-locked) 24-bit stereo channel pairs only.

Supersedes the audio model of
[multi-track-audio-design.md](multi-track-audio-design.md) (implemented
against CVBS spec v1.2.0). The stage/DAG topology, transform set, and quick
project wiring from that design are retained; the audio data model changes.

---

## 1. Specification Delta and Current State

The CVBS file format specification
([docs/index.md](cvbs-file-format-specification/docs/index.md), **v1.3.0**,
sub-module pinned at the tagged commit) rebased its audio section on
[SMPTE 272M-1994](https://github.com/simoninns/analogue-video-specifications/blob/main/docs/video_formats/SMPTE-272M-1994/SMPTE-272M-1994.md):

| Aspect | v1.2.0 (implemented) | New spec (required) |
|--------|----------------------|---------------------|
| Unit | 16 stereo *tracks* | 8 stereo *channel pairs* (16 SMPTE 272M channels) |
| File naming | `_audio_00.wav` … `_audio_15.wav` | `_audio_0.wav` … `_audio_7.wav` |
| Timing | per-track frame-locked **or** free-running | synchronous (frame-locked) **only** |
| Sample rate | locked: 44100 (PAL) / 44100000⁄1001 (NTSC, PAL-M); free-running: 44100 | **48000 Hz exact**, all systems |
| Pairs per frame | PAL 1764, NTSC/PAL-M 1470 (constant) | PAL **1920** (constant); NTSC/PAL-M **8008⁄5** — 1602/1601 over a 5-frame audio frame sequence |
| Bit depth | 16-bit | **24-bit** signed LE |
| Metadata | `audio_track(track_number 0–15, description, audio_locked)` | `audio_channel_pair(channel_pair 0–7, description)` — no lock column |
| `user_version` | 9 | 10 |
| WAV header rate | 44100 / 44056 | 48000 (exact for all systems) |

Decisions made with the project owner:

| Decision | Choice |
|----------|--------|
| Pipeline sample carrier | `int32_t` holding 24-bit values end-to-end; host ABI bump 6 → 7 |
| Free-running audio | Removed from the pipeline model entirely — all audio is frame-locked 48 kHz 24-bit; producers resample at the point of production (including EFM decode) |
| Terminology | Full SDK rename: track → channel pair |
| TBC sidecar | Always resampled 44.1 → 48 kHz on ingest (the `.pcm` written by ld-decode is 16-bit 44100 Hz); the `pcm_audio_timing` / `lock_audio` parameters are removed |

Because 48000 ÷ (30000⁄1001) = 8008⁄5 exactly, the NTSC/PAL-M sample rate is
an exact integer 48000 Hz. The 44056 header hack, the
`sample_rate_mode` export parameter, and the latent video_sink 44100-vs-44056
NTSC drift all disappear with this migration.

The structural consequences inside the pipeline:

- The dual per-frame/stream accessor API collapses to per-frame only. All
  time-domain free-running remapping code (frame_map window stitching,
  source_align stream shift, stacker free-running pass-through/discard,
  audio_sink stream write path) is deleted.
- Per-frame pair counts are no longer constant for NTSC/PAL-M; every consumer
  of `locked_audio_pairs_per_frame()` must become cadence-aware.
- Every stage's `int16_t` audio surface (VFR accessors, resampler
  `SOXR_INT16_I`, WAV 4-bytes-per-pair math, FFmpeg `S16` feeds) migrates to
  the 24-bit `int32_t` carrier.

---

## 2. Core Model

### 2.1 Channel-pair invariants

Every audio channel pair, everywhere in the pipeline, is:

- **Stereo**, interleaved `int32_t` pairs (L, R, L, R, …) carrying 24-bit
  two's-complement values in the range −8388608 … 8388607. 16-bit source
  material is shifted left 8 bits on ingest (exactly reversible). Producers
  and transforms must saturate to the 24-bit range.
- **48000 Hz, frame-locked (synchronous)** per SMPTE 272M §3.15. There is no
  free-running regime. Any producer whose native material is not 48 kHz
  synchronous (TBC sidecar, EFM decode, imported WAV) resamples with SoXR HQ
  at the point of production.
- Identified by **channel pair index** `0 … audio_channel_pair_count() - 1`.
  Pair order is stable through the DAG; pair-adding stages append. Pipeline
  pair *p* maps to container file `_audio_<p>.wav` and SMPTE 272M channels
  2*p*+1 / 2*p*+2.
- Capped at **8 pairs** (`kMaxAudioChannelPairs = 8`), enforced at every
  pair-adding stage so a DAG that validates also exports.

### 2.2 The audio frame sequence (cadence)

Per-frame pair counts follow SMPTE 272M §3.7/§3.8/§14.3:

- **PAL**: 1920 pairs per frame, constant (sequence length 1).
- **NTSC / PAL-M**: 5-frame sequence totalling 8008 pairs; frame *n* carries
  1602 pairs when *n* mod 5 ∈ {0, 2, 4} and 1601 when *n* mod 5 ∈ {1, 3}.
  Cumulative in-sequence offsets: 0, 1602, 3203, 4805, 6406.

The existing rational helper (`round(n × rate / frame_rate)` with 64-bit
arithmetic) evaluated at 48000 Hz reproduces this table exactly, so the
cumulative-offset function is retained as the single authority for
frame→pair-offset mapping:

```cpp
// orc/sdk/include/orc/stage/audio_channel_pair.h (replaces audio_track.h)

inline constexpr size_t kMaxAudioChannelPairs = 8;

// SMPTE 272M-1994 §1.2: 48 kHz, clock locked (synchronous) to video.
inline constexpr uint32_t kAudioSampleRateHz = 48000;

// SMPTE 272M-1994 §1.3/§3.10: 24-bit audio (level C operation).
inline constexpr uint32_t kAudioBitDepth = 24;

enum class AudioOrigin : uint8_t {
  ANALOGUE, HIFI, EFM, IMPORTED, DERIVED, UNKNOWN,
};

struct AudioChannelPairDescriptor {
  std::string name;  // human-readable, e.g. "Analogue", "EFM digital audio"
  AudioOrigin origin = AudioOrigin::UNKNOWN;
};

// Cumulative stereo-pair offset of the START of frame |frame_index|:
//   PAL: 1920 × n.   NTSC/PAL-M: round(n × 8008 / 5)
//   (SMPTE 272M-1994 §14.3 Table 1: 1602/1601/1602/1601/1602).
uint64_t audio_pair_offset(uint64_t frame_index, VideoSystem system);

// Pairs in frame |frame_index| = offset(n + 1) - offset(n):
//   PAL 1920 constant; NTSC/PAL-M 1602 or 1601 by audio frame number.
uint32_t audio_pairs_in_frame(uint64_t frame_index, VideoSystem system);
```

Removed from the SDK: `kMaxAudioTracks`, `AudioSampleRate`,
`AudioTrackDescriptor.locked/sample_rate`, `kFreeRunningAudioRate`,
`locked_audio_sample_rate()`, `locked_audio_pairs_per_frame()`,
`audio_stream_pair_offset()` (subsumed by `audio_pair_offset`).

### 2.3 VFR contract changes (ABI 7)

The six audio accessors in `video_frame_representation.h` are **replaced**
(no deprecated shims) by three:

```cpp
// Number of stereo audio channel pairs (0 = no audio). Max
// kMaxAudioChannelPairs. Pipeline pair p maps to container channel pair p.
virtual size_t audio_channel_pair_count() const { return 0; }

// Descriptor for channel pair |pair|; nullopt when out of range.
virtual std::optional<AudioChannelPairDescriptor>
get_audio_channel_pair_descriptor(size_t pair) const { return std::nullopt; }

// Interleaved 24-bit-in-int32 stereo pairs for frame |id| — exactly
// audio_pairs_in_frame(id, system) pairs (silence-filled by the producer
// where source material is short). Empty when |pair| is out of range.
virtual std::vector<int32_t> get_audio_samples(size_t pair, FrameID id) const {
  return {};
}
```

`has_audio()` is retained as a non-virtual convenience. The per-frame sample
count needs no accessor: it is fully determined by the video system and frame
index via `audio_pairs_in_frame()` — the count is a model invariant, not a
per-representation property.

`VideoFrameRepresentationWrapper` forwards all three accessors. The
pass-through contract comment is updated: a stage that remaps frame IDs must
override `get_audio_samples` (per §4.2); the stream-accessor clause is
deleted. EFM (`get_efm_samples`) and AC3-RF accessors are unchanged — they
carry undecoded signal data, not audio.

Per AGENTS.md §9, `kStagePluginHostAbiVersion` bumps 6 → 7 and
`docs/technical/plugin-sdk.md` / `plugin-architecture.md` (version tables,
header list: `audio_track.h` → `audio_channel_pair.h`) plus the allowlist in
`cmake/check_plugin_private_includes.sh` are updated in the same change.

### 2.4 Shared resampler

`orc/plugins/stages/common/audio-resample/` is reworked as the single
ingest-conversion authority, used by tbc_source, cvbs_source (legacy-free
after Phase 2, retained for import), efm_audio_decode, and audio_import:

- `int32_t` I/O (`SOXR_INT32_I`), stereo interleaved, SoXR HQ,
  duration- and sync-preserving.
- `resample_to_synchronous(raw, in_rate_hz, system, frame_count)` — converts
  any-rate input to 48000 Hz and segments it into cadence-sized per-frame
  blocks (`audio_pairs_in_frame`), zero-padding or truncating to exactly
  `audio_pair_offset(frame_count)` total pairs.
- The 44.1 kHz-era constants (`kNtscLockedRateHz`, `kFreeRunningRateHz`) and
  `lock_and_segment()` are removed.

---

## 3. Stage Impact

### 3.1 tbc_source

- The `.pcm` sidecar (16-bit 44100 Hz stereo, as written by ld-decode) is
  always converted: samples widened to 24-bit (<< 8) and resampled
  44100 → 48000 via the shared resampler, lazily on first audio access
  (`std::once_flag`, preserving the issue #209 deferred-load behaviour) —
  now for **all** systems including PAL.
- Parameters `pcm_audio_timing` and `lock_audio` are **removed** — there is
  only one permitted pipeline audio form. `instructions.md` updated.
- The parsed-but-unused `PcmAudioParameters` metadata (`sample_rate`, `bits`,
  `is_signed`, `is_little_endian`) is wired into the read path: when present
  it is validated (error observation on unsupported layouts) instead of
  silently assuming signed-LE 16-bit 44100.
- Descriptor: `{name: "Analogue", origin: ANALOGUE}` as channel pair 0.

### 3.2 cvbs_source

- Enumerates `_audio_0.wav` … `_audio_7.wav` (single-digit suffix),
  preserving container pair numbers as pipeline indices.
- Reads the `audio_channel_pair` table for descriptions; missing rows for
  existing files (spec violation) produce a warning observation and a
  derived `"Channel pair N"` name. Origin is always `UNKNOWN`. No backwards
  compatibility with pre-current-spec metadata.
- Real WAV header validation replaces the current size-only math: PCM tag,
  2 channels, `nSamplesPerSec == 48000`, 24-bit; mismatches are errors.
  Pair math becomes 6 bytes per stereo pair (2 × 3-byte samples), unpacked
  to `int32_t`. Expected data length is `audio_pair_offset(frame_count)`
  pairs; unequal lengths across pair files produce a warning observation.
- The `lock_audio` parameter is **removed** (container audio is always
  synchronous). Stream serving paths are deleted; per-frame reads seek to
  `audio_pair_offset(id) × 6` bytes.

### 3.3 efm_audio_decode

- Decode machinery is unchanged (shared `efm-decode` library, lazy once-only
  whole-stream decode). The decoded CD audio (44100 Hz 16-bit) is then
  widened and resampled 44100 → 48000 via the shared resampler and written
  to the scratch cache as raw `int32_t` cadence-aligned frames; per-frame
  serving seeks by `audio_pair_offset()`.
- Appends `{name: "EFM digital audio", origin: EFM}`; 8-pair cap enforced.
- Bit-exact (un-resampled) CD audio remains available via `efm_sink`, which
  is unchanged.

### 3.4 audio_import (renamed from audio_track_import)

- Accepts RIFF/WAVE PCM, stereo, 16- or 24-bit, at any SoXR-supported header
  rate; converts on ingest (widen + resample) to the pipeline form. The
  `lock_mode` parameter and the locked/free-running auto-detection are
  **removed** — a 48000 Hz input whose length already equals
  `audio_pair_offset(frame_count)` passes through the resampler unchanged.
- Appends `{origin: IMPORTED}`; 8-pair cap enforced. Stage ID, class names,
  and `instructions.md` adopt the channel-pair terminology.

### 3.5 audio_channel_map / audio_align

- `audio_channel_map`: `int32_t` pair-indexed accessors. The operation set is
  SMPTE 272M §6.4-conformant (mono = chosen channel on the left, right zeroed;
  never duplicated across both): `delete` (remove the pair; later pairs shift
  down one index), `left_to_mono` / `right_to_mono` (extract a channel as mono
  in place → `[L, 0]` / `[R, 0]`), and `copy_left_to_target` /
  `copy_right_to_target` (read the source pair, write the mono channel to a
  target pair — `new` appends, an existing index overwrites — source untouched;
  the 8-pair cap is enforced on append). The earlier `split_dual_mono` /
  `left_to_both` / `right_to_both` / `swap_channels` set is removed. To split a
  dual-mono pair: `copy_left_to_target(new)`, `copy_right_to_target(new)`,
  `delete` the original. `channel_pair` (source) and `target_pair` (with a
  `new` entry, shown only for the copy ops via `depends_on`) are string
  dropdowns; the GUI narrows both to the pairs present on the node's input
  (`RenderPresenter::getAudioChannelPairNames`, carrying descriptions rendered
  as "index - name"). A `set_description` bool ("Add description", shown for
  every op except `delete`) gates a free-text `description` parameter (shown via
  `depends_on set_description == "true"`): off keeps the produced pair's existing
  name, on overrides it — this is how bilingual programmes get "English
  language" / "French language" names.
- `audio_align`: offsets convert at exactly 48 pairs/ms; window assembly
  uses cumulative `audio_pair_offset()` arithmetic instead of a constant
  per-frame stride (NTSC cadence). The free-running origin-shift path is
  deleted. Per-frame pair counts are unchanged by the shift, so output stays
  spec-conformant.

### 3.6 Frame-manipulating stages (frame_map, source_align, stacker)

Only one audio regime remains, remapped by frame ID as today, with one new
rule for the NTSC/PAL-M cadence:

- Output frame at timeline index *p* must serve exactly
  `audio_pairs_in_frame(p)` pairs; the mapped source frame *m* natively
  carries `audio_pairs_in_frame(m)` pairs. When *p* ≢ *m* (mod 5) the counts
  can differ by one pair: the window is truncated by one trailing pair or
  padded with one trailing silence pair. Mappings that preserve the cadence
  phase (offsets ≡ 0 mod 5, and all PAL mappings) are sample-exact.
- Padding/missing frames get cadence-sized silence
  (`audio_pairs_in_frame(p)`), replacing the constant-size silence blocks.
- stacker: `audio_stacking` (mean/median/disabled) now applies to **every**
  channel pair present in all inputs (same frame ID across sources — counts
  always agree); pairs not common to all inputs pass through from the best
  source. The free-running discard/warning machinery is deleted. EFM t-value
  stacking is unchanged, and `efm_audio_decode` still belongs downstream of
  the stacker.
- All free-running stream-map/stitching code in frame_map and source_align
  is deleted.

### 3.7 audio_sink

- Writes the selected channel pair (`channel_pair` parameter, renamed from
  `track`) as a stereo 24-bit 48000 Hz WAV. `sample_rate_mode`
  (`locked_44056` / `free_running_44100`) and the free-running/resampled
  write paths are **removed** — 48000 is an exact integer rate for every
  system.

### 3.8 video_sink (FFmpeg backend)

- `audio_channel_pairs` parameter (renamed from `audio_tracks`) selects pairs
  to embed; one output stream per pair with `title` metadata, as today.
- Every stream is declared **48000 Hz** — exact for all systems, eliminating
  the 44056 approximation and the 44100-vs-44056 NTSC drift defect. The
  free-running cursor/`audio_stream_pair_offset` feed logic is deleted;
  per-frame gather uses `get_audio_samples` with cadence-sized silence
  fallback (`audio_pairs_in_frame`).
- Sample feed converts from the int32 carrier: AAC → `FLTP` (÷ 8388608.0),
  FLAC → `S32` (<< 8, `bits_per_raw_sample = 24`), PCM → `PCM_S24LE` via
  `S32` (<< 8). The `fillAudioFrameFromInterleavedS16` helper becomes the
  int32 equivalent. Codec-per-video-codec mapping is unchanged;
  `audio_gain_db` applies in the 24-bit domain with saturation.

### 3.9 cvbs_sink

- Writes every pipeline channel pair to `_audio_<p>.wav` (single digit),
  24-bit LE stereo at 48000 Hz, header patched with final size; total pairs
  per file is exactly `audio_pair_offset(frame_count)`, so all pair files
  are equal-length by construction (spec requirement).
- Writes the `audio_channel_pair` metadata table (`channel_pair`,
  `description` from the descriptor name) under `PRAGMA user_version = 10`.
  The `audio_track` DDL and `audio_locked` binding are removed.
- Since every pipeline pair is already synchronous 48 kHz 24-bit, the sink
  performs no conversion.

### 3.10 Presentation layer

Channel-pair indices remain **0-based** in all stage parameters, matching the
`_audio_<p>.wav` naming (the 1-based UI convention applies to frames/lines
only). No presenter or GUI surface currently exposes audio descriptors, so no
GUI changes are required; the quick-project wiring (efm_audio_decode
insertion) is unchanged.

---

## 4. CVBS Specification Version

The specification change is released as **v1.3.0** of the
`cvbs-file-format-specification` repository, and the
`docs-tech/cvbs-file-format-specification` sub-module is pinned at the
tagged commit (`d801241`). All container work below cites that version.

---

## 5. Testing

Per AGENTS.md §4 (unit-first, mocked, deterministic):

- **SDK/contract**: `audio_pair_offset` / `audio_pairs_in_frame` exactness
  against SMPTE 272M §14.3 Table 1 (PAL 1920 constant; NTSC offsets
  0/1602/3203/4805/6406, 8008 per sequence, no cumulative drift over long
  ranges); wrapper forwarding for the three accessors; a mock VFR with N
  pairs exercised through every remapping wrapper verifying the cadence
  ±1-pair rule at phase-breaking mappings. Labels: `unit`, `contracts`.
- **Resampler**: 44100→48000 duration/DC preservation, cadence segmentation
  for both systems, 16→24-bit widening exactness. Labels: `unit`.
- **Sources**: cvbs_source single-digit enumeration, WAV header validation
  failures, `audio_channel_pair` table read, missing-row warning; tbc_source
  always-resample path, `PcmAudioParameters` validation, lazy-once decode.
  Labels: `unit`, `sources`.
- **Transforms**: efm_audio_decode resample-to-cache with mocked deps;
  channel-map int32 routing; align cadence window assembly; import
  conversion and cap. Labels: `unit`, `transforms`.
- **Sinks**: audio_sink 48 kHz/24-bit header and pair selection; video_sink
  per-pair 48000 rate and int32 sample-format feeds via mocked backend deps;
  cvbs_sink naming/DDL/user_version/equal-length emission via mocked writer
  deps. Labels: `unit`, `sinks`.
- **Functional**: the multi-track round-trip
  (`multi_track_audio_roundtrip_test`) rewritten as a channel-pair
  round-trip (cvbs_sink → cvbs_source) verifying descriptors and
  sample-exact int32 payloads for PAL and NTSC cadence. EFM decode
  correctness stays covered by the existing functional EFM golden baselines
  (unchanged: `efm_sink` output is untouched).
- SDK gates (`-L sdk`) and `MVPArchitectureCheck` must pass in every phase.

---

## 6. Implementation Phases

### Phase 1 — SDK channel-pair model and pipeline port (ABI 7)

1. Replace `orc/sdk/include/orc/stage/audio_track.h` with
   `audio_channel_pair.h` (§2.2): `kMaxAudioChannelPairs = 8`,
   `kAudioSampleRateHz = 48000`, `kAudioBitDepth = 24`, `AudioOrigin`,
   `AudioChannelPairDescriptor`, `audio_pair_offset()`,
   `audio_pairs_in_frame()`.
   *Acceptance*: header compiles standalone; unit tests assert the SMPTE
   272M §14.3 cadence table and no cumulative drift.
2. Replace the VFR audio accessor surface with the three pair-indexed
   int32 accessors (§2.3); update wrapper forwarding, the pass-through
   contract comment, and the shared mocks.
   *Acceptance*: contract suite covers defaults, forwarding, and out-of-range
   behaviour for all three accessors.
3. Bump `kStagePluginHostAbiVersion` to 7 (`orc_plugin_abi.h` value + macro +
   in-header history note); update `plugin-sdk.md`, `plugin-architecture.md`
   (version tables, header list), and the
   `cmake/check_plugin_private_includes.sh` allowlist.
   *Acceptance*: ABI static_asserts and loader version-rejection tests pass;
   docs updated in the same change.
4. Rework the shared `audio-resample` library (§2.4): int32/SoXR I/O,
   `resample_to_synchronous()`, cadence segmentation; delete the 44.1 kHz
   constants and `lock_and_segment()`.
   *Acceptance*: resampler unit tests for both systems' cadences, duration
   preservation, and 16→24-bit widening.
5. Port the frame-manipulating stages (frame_map, source_align, stacker) to
   the locked-only cadence model (§3.6): cadence-sized silence, the ±1-pair
   phase rule, all-pairs stacking; delete every free-running stream path.
   *Acceptance*: per-stage suites cover contiguous and phase-breaking
   mappings on NTSC and constant-cadence PAL.
6. Port the audio transforms: audio_channel_map (int32),
   audio_align (cadence window assembly, 48 pairs/ms),
   audio_track_import → `audio_import` (§3.4), efm_audio_decode
   resample-to-cache (§3.3); update each stage's `instructions.md`.
   *Acceptance*: per-stage mocked suites incl. cap-exceeded, out-of-range
   pair, edge silence, and once-only decode.
7. Port the remaining producers/consumers at carrier level so the tree
   builds and all suites pass: tbc_source always-resample ingest with
   parameter removal and `PcmAudioParameters` validation (§3.1);
   compile-level ports of cvbs_source/cvbs_sink/audio_sink/video_sink to the
   new accessors (full container/sink conformance follows in Phases 2–3).
   *Acceptance*: full build green; all unit suites, `-L sdk`, and MVP gates
   pass; tbc_source suite covers the resample path and metadata validation.

### Phase 2 — CVBS container conformance

The spec is already released (v1.3.0) and the sub-module pin already
references the tagged commit (§4); no spec-repository work remains.

1. cvbs_source (§3.2): single-digit enumeration, `audio_channel_pair` table
   read with missing-row warning, real WAV header validation, 6-byte pair
   reads, `lock_audio` removal, stream-path deletion; `instructions.md`.
   *Acceptance*: mocked-deps unit tests for enumeration, descriptors,
   validation failures, cadence-offset reads, and equal-length warnings.
2. cvbs_sink (§3.9): single-digit naming, 24-bit WAV writer,
   `audio_channel_pair` DDL, `user_version = 10`, equal-length emission;
   `instructions.md`.
   *Acceptance*: deps-level unit tests for header bytes, naming, DDL,
   user_version, and row content (currently untested code — new suite).
3. Rewrite the functional round-trip as the channel-pair round-trip (§5)
   covering PAL and NTSC cadence.
   *Acceptance*: functional test passes; descriptors and samples round-trip
   exactly.

### Phase 3 — Terminal sinks and closeout

1. audio_sink (§3.7): `channel_pair` parameter, 48 kHz/24-bit WAV writer,
   removal of `sample_rate_mode` and both conversion paths;
   `instructions.md`.
   *Acceptance*: deps unit tests assert header rate/bit depth and pair
   selection; parameter descriptor tests updated.
2. video_sink (§3.8): `audio_channel_pairs` parameter, uniform 48000 Hz
   stream declaration, int32 sample-format feeds per codec, cadence silence
   windows, deletion of the free-running cursor logic; `instructions.md`.
   *Acceptance*: mocked backend unit tests for per-pair rate, sample-format
   conversion values (FLTP/S32 scaling), and gain saturation.
3. Add a superseded-by notice to
   [multi-track-audio-design.md](multi-track-audio-design.md) §1–2 pointing
   here, and sweep remaining comments/docs for 44100/44056/1470/1764 and
   track terminology.
   *Acceptance*: repo-wide grep shows no stale locked-rate constants or
   track-model references outside historical design docs.
