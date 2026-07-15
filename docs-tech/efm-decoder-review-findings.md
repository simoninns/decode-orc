# EFM Decoder Review — Findings

Review of `orc/plugins/stages/common/efm-decode/` (efm-lib, decoders, writers, `efm_processor`) and the
consuming stages `efm_sink`, `efm_audio_decode`, `raw_efm_sink`, against:

- IEC 60908-1999 (`docs-tech/analogue-video-specifications/docs/efm/IEC-60908-1999/IEC-60908-1999.md`)
- ECMA-130 (`docs-tech/analogue-video-specifications/docs/efm/ECMA-130/ECMA-130.md`)
- The ezpwd Reed-Solomon headers actually used by the build

Review date: 2026-07-14, branch `20260714-efm` (HEAD `4291a65b`). Line numbers reference that revision.

Verification method: four independent review passes (spec compliance, Q-channel, error correction,
efficiency). The EFM code table, sector scramble table, and EDC CRC table were regenerated
programmatically from the spec text and compared against the source; the CIRC delay/permutation
structure was checked byte-by-byte against ECMA-130 Figure C.4. Findings reported by more than one
independent pass are marked *cross-confirmed*. Severity: **Critical** = decode aborts/crashes or data
is lost on real in-spec media; **Major** = materially wrong output, degraded correction, or unbounded
resource use; **Minor** = incorrect but low-impact, or clearly suboptimal; **Info** = cosmetic,
documentation, or dead code.

---

## Summary against the review questions

1. **Is it correct and compliant to IEC 60908?** The bit-level foundations are excellent: the EFM
   code table (all 256 entries), frame layout, subcode CRC, CIRC delay/deinterleave/parity-inversion
   structure, RS parameters, sector scrambler, and EDC were all machine-verified as spec-faithful.
   The substantive non-compliances are behavioural: a hard 60-minute absolute-time ceiling that
   aborts any longer disc (C-1), track 99 remapped to track 1 (C-2), and mode 2 sector payloads
   truncated to 2048 bytes (C-5).
2. **Does it handle all Q-modes correctly?** Mode 1 program-area decoding is correct. Mode 0 is
   misdecoded as a valid mode-1 lead-in (Q-1). Mode 2 parses the UPC correctly but truncates it to
   32 bits (Q-3) and fabricates track 1 / time 00:00:00 (Q-2). Mode 3 (ISRC) is never decoded and
   the mode-3 branch in the correction stage is unreachable (Q-4). Mode 4 (LaserDisc) is handled with
   the established mode-1-shaped interpretation, matching ld-decode practice. Lead-in POINT/TOC and
   INDEX/pause semantics are not decoded at all (Q-6).
3. **Is the error correction logical, correct and optimal?** The structure is correct, but the C2
   decoder discards legitimate 3- and 4-erasure corrections (E-1), roughly halving CIRC's designed
   burst tolerance — this is the single biggest correction-quality defect. RSPC runs one pass with
   no P↔Q iteration and no erasure-flag maintenance (E-3, E-4). Audio concealment interpolates
   single samples only and hard-mutes bursts without ramps (E-6).
4. **Obvious improvements?** Fix the `std::runtime_error` escapes that bypass the
   `efm::EfmDecodeError` stage-boundary contract (R-1), convert data-driven aborts into graceful
   degradation (R-2), and bound the two buffer-growth pathologies (R-3, R-4).
5. **Is it efficient?** The algorithms are sound (O(1) ring-buffer delay lines, table-driven EFM/CRC,
   `std::search` sync detection), but value semantics dominate the cost: whole-section deep copies at
   every stage boundary (P-1), 2352-byte sector copies to read single bytes (P-2), and per-sample
   vector allocation in audio correction (P-3). The consuming stages also buffer the entire capture
   in RAM despite a working streaming API (P-4).

**Recommended fix order:** R-1 → C-1 → E-1 → R-3/R-4 → Q-5 → P-1/P-2/P-3 → E-3/E-4 → Q-1..Q-4 → the rest.

---

## 1. Spec compliance (IEC 60908 / ECMA-130)

### C-1 — All time/address classes cap the disc at 60 minutes — **Critical** *(cross-confirmed ×3)*

**Location:** `efm-lib/section_metadata.cpp:55-111` (`SectionTime`: throw at `frames >= 270000`,
`minutes >= 60` clamped to 59); `efm-lib/sector.cpp:22-78` (`SectorAddress`, identical);
`efm-lib/subcode.cpp:266-285` (parsed minutes clamped to 59 and section marked "repaired").
**Spec:** IEC 60908 §17.5.1 — AMIN/MIN are two BCD digits (00–99). Nothing limits the program area
to 60 minutes; real discs commonly run 74–80+ minutes.

The limit comment ("60 minutes per hour so the maximum number of frames is 75 * 60 * 60 = 270000")
does not correspond to any spec text. Failure chain: at absolute time 60:00:00 the parsed minutes are
clamped to 59 and the section flagged repaired; `F3FrameToF2Section::outputSection`
(`dec_f3frametof2section.cpp:301-314`) invalidates repaired sections whose time jumps; after >10
consecutive invalid sections `F2SectionCorrection::processInternalBuffer` throws — **every decode of
a disc longer than one hour aborts at the 60-minute mark**. Sector header addresses have the same
ceiling.

**Fix:** Raise the limit to 99:59:74 → `< 450000` frames; clamp minutes at 99 in `setTime` and in the
`validateAndClampTimeValue` call for absolute minutes in `subcode.cpp`; apply the same change to
`SectorAddress`. Audit the `m_absoluteStartTime(59, 59, 74)` "max time" sentinels
(`dec_f2sectioncorrection.cpp:35`, `dec_data24toaudio.cpp:19`).

### C-2 — Track number 99 remapped to track 1 — **Minor** *(cross-confirmed ×2)*

**Location:** `efm-lib/section_metadata.cpp:198-205, 230-237`.
**Spec:** IEC 60908 §17.5.1 — "01-99: Track numbers, BCD encoded".

Both `setSectionType` and `setTrackNumber` treat `m_trackNumber > 98` as invalid and force it to 1.
A legitimate TNO 99 becomes track 1, which then also trips the WAV metadata writer's
"track number decreased" guard for the rest of the disc. Inconsistent with `subcode.cpp:383`, which
correctly uses `> 99`.

**Fix:** Change both `> 98` comparisons to `> 99`.

### C-3 — P-channel majority vote counts only 80 of 96 bits — **Minor** *(cross-confirmed ×3)*

**Location:** `efm-lib/subcode.cpp:48-67`.
**Spec:** IEC 60908 §17.3/17.4 — a subcode block carries 96 P bits (98 symbols minus S0/S1).

`pChannelData` is the already-extracted 12-byte (96-bit) P bitfield — sync was stripped during
extraction — but the counting loop starts at `index = 2`, skipping the first 16 P bits (the
"skip S0/S1" offset applied a second time). The vote counts 80 bits against a `96 / 2` threshold,
biasing the flag toward false.

**Fix:** Start the loop at `index = 0`.

### C-4 — Control nybbles 8–11 decoded as 4-channel audio; 12–15 rejected — **Info** *(cross-confirmed ×2)*

**Location:** `efm-lib/subcode.cpp:133-135, 208-235`.
**Spec:** IEC 60908-1999 §17.5 NOTE 2 — "1XXX: Broadcasting use" (the 1999 edition no longer defines
a 4-channel encoding; that is earlier Red Book / ECMA-130).

The current mapping follows the older convention (defensible, and arguably more useful for legacy
discs), but it doesn't match the referenced 1999 edition, and control values 12–15 discard
CRC-valid sections as corrupt. Practical impact ~nil.

**Fix:** Either align with the 1999 text (treat 1XXX as "broadcasting use", content class unknown) or
keep the legacy mapping and update the comment to cite the earlier edition deliberately.

### C-5 — Mode 2 sectors truncated to 2048 bytes; mode 0 content unchecked — **Minor**

**Location:** `decoders/dec_rawsectortosector.cpp:90-91 (comment), 287-300, 330-344`.
**Spec:** ECMA-130 §14.2 — mode 2: bytes 16–2351 are user data (2336 bytes); mode 0: bytes 16–2351
shall be (00).

Every accepted sector emits only bytes 16..16+2048. Mode 2 sectors lose their last 288 user-data
bytes; mode 0 sectors pass through without the all-zero check. The code acknowledges the gap
("Note: Mode 0 and Mode 2 support missing").

**Fix:** Size the emitted payload by mode (2048 / 2336 / zero-verified), or explicitly reject
non-mode-1 sectors rather than emitting silently truncated data.

### C-6 — Lead-in sector headers (0xA0 convention) not handled — **Info**

**Location:** `decoders/dec_rawsectortosector.cpp:308-311`.
**Spec:** ECMA-130 §14.2(a).

Lead-in-area data sector headers use MIN + 0xA0 encoding; `bcdToInt(0xA3)` yields 103, later clamped.
Low impact (digital-data lead-in tracks are rare). Handle or explicitly discard.

### Verified compliant (coverage)

- **EFM code table** (`efm-lib/efm.cpp`): all 256 entries programmatically identical to ECMA-130
  Annex D; run-length rule holds for every entry; no duplicates. Subcode syncs S0/S1 match §19.1.
- **T-value expansion and frame sync**: T-n → `1` + (n−1) zeros, range 3..11 per IEC §13; the 24-bit
  sync header detected as the T11+T11 pair.
- **588-bit frame layout** (`dec_channeltof3frame.cpp`): sync 0–23, merging bits skipped, control
  symbol at bit 27, 32 × (14+3) data symbols from bit 44 — matches ECMA-130 §19.4 / IEC §14.
- **98 frames/section; S0/S1 excluded from subcode**, P/Q bit positions, MSB-first packing.
- **CIRC decoder structure** verified against ECMA-130 Figure C.4 per byte: odd-position 1-frame
  delay, parity inversion on positions 12–15/28–31 (per C.9), C1 (32,28), deinterleave delays
  108−4k, C2 (28,24) with Q parity mid-frame, 24-byte de-permutation (all 24 mappings), final
  2-frame delay group. GF(2⁸) poly 0x11D, FCR=0, PRIM=1 match IEC §16.2/Figure 11; ezpwd shortened-
  code and erasure-index semantics verified against the shipped headers.
- **F1 even/odd byte-pair swap** (ECMA-130 §16) and audio sample assembly (L=even, R=odd; 16-bit
  sample flagged if either byte flagged).
- **Sector path**: 12-byte sync `00 FF×10 00`; unscramble table regenerated from the Annex B LFSR
  (x¹⁵+x+1, seed 0x0001) — 0 mismatches, applied to bytes 12–2351 only; EDC CRC table regenerated
  from P(x) = (x¹⁶+x¹⁵+x²+1)(x¹⁶+x²+x+1) — 0 mismatches, coverage 0–2063 for mode 1; RSPC P/Q vector
  geometry (incl. the mod-1118 diagonal and MSB/LSB planes) verified against Annex A.
- **Q-channel CRC**: x¹⁶+x¹²+x⁵+1 over 80 bits, parity inverted on disc, correct comparison.
- **Q mode 1 field layout**, lead-out TNO 0xAA exception, lead-in TNO 00, BCD helpers; mode 2
  13-nibble UPC extraction layout.

---

## 2. Q-mode handling

### Q-1 — Q-mode 0 misdecoded as a valid mode-1 lead-in section — **Major** *(cross-confirmed ×2)*

**Location:** `efm-lib/subcode.cpp:114-119`.
**Spec:** IEC 60908 §17.5.4 — mode 0 "shall contain, if used, only the CONTROL and CRC bits, all
other bits are zero". The NOTE says mode 0 *may replace mode-1* on non-CD information channels — it
does not say to decode it *as* mode 1.

`case 0x0:` falls through to `QMode1` (the comment inverts the spec). All-zero DATA-Q then yields
TNO=0 → the section is classified `LeadIn` at absolute time 00:00:00 and marked **valid**.
A mid-program mode-0 block (explicitly relevant to non-CD channels, i.e. LaserDisc) reaches
`F2SectionCorrection::waitingForSection` with time 00:00:00 < expected → dropped as out-of-order
(`dec_f2sectioncorrection.cpp:414-426`) and the resulting hole is filled with a 98-frame all-error
dummy — **genuine audio is discarded**. Runs of mode-0 blocks during spin-up also stall
`waitForInputToSettle` (constant 00:00:00 never increments).

**Fix:** Handle mode 0 distinctly: keep the control bits, mark the metadata invalid (or a dedicated
`QMode0`) so the section is reconstructed by interpolation like a CRC-failed section, preserving its
audio payload. Fix the comment.

### Q-2 — Q-mode 2 sections fabricate track 1 / time 00:00:00 — **Major**

**Location:** `efm-lib/subcode.cpp:313-315`; consumer `decoders/dec_f2sectioncorrection.cpp:244-274`.
**Spec:** IEC 60908 §17.5.2 — mode 2 carries only the catalogue number + AFRAME; TNO/INDEX/TIME are
not encoded. Real discs interleave mode 2/3 blocks inside tracks at ≥1/100 duty.

At parse time the code sets `setSectionType(UserData, 1)` and `setSectionTime(0, 0, 0)`. The
correction stage repairs only the *absolute* time; track number stays 1 and track-relative time stays
0. Confirmed consequences: track statistics misattribute the section to track 1 and drag track 1's
start time to 00:00:00 (`dec_f2sectioncorrection.cpp:660-700`); gap interpolation seeded from a
mode-2 section propagates `trackNumber=1` / near-zero times (`:530-535`); in
`writer_wav_metadata.cpp:77-110, 204-208` a mode-2 section can seed a bogus "track 1" entry when it
is the first section written, and overwrites `m_prevSectionTime` with 00:00:00, corrupting the
previous track's end time.

**Fix:** In `dec_f2sectioncorrection`, have mode-2/3 sections inherit track number, index, and
track-relative time from the surrounding mode-1 timeline (previous valid section + 1) instead of
fabricating values at parse time.

### Q-3 — UPC/EAN catalogue number truncated to `uint32_t` — **Major** *(cross-confirmed ×2)*

**Location:** `efm-lib/subcode.cpp:294-303` (builds a correct `uint64_t`);
`efm-lib/section_metadata.h:172` (`setUpcEanCode(uint32_t)`, `uint32_t m_upcEanCode`);
serialised 32-bit at `efm-lib/section_io.h:122`.
**Spec:** IEC 60908 §17.5.2 — 13-digit BCD catalogue number.

The correctly assembled 13-digit value silently narrows to 32 bits; virtually every real EAN-13
(> 4,294,967,295) is stored modulo 2³², and leading zeros are unrepresentable.

**Fix:** Store as `uint64_t` or, better, a 13-character string (preserves leading zeros). The same
applies to `m_isrcCode` (see Q-4). Update `section_io.h` accordingly.

### Q-4 — Q-mode 3 (ISRC) never decoded; correction-stage branch is dead code — **Major**

**Location:** `efm-lib/subcode.cpp:323-330`; dead branch `decoders/dec_f2sectioncorrection.cpp:249-273`.
**Spec:** IEC 60908 §17.5.3 — ISRC: I1–I5 as 6-bit characters, I6–I12 as BCD, ZERO, AFRAME.

`fromData` logs "not yet decoded", sets the section invalid, and returns. Therefore no ISRC is ever
extracted (no 6-bit character decode exists; `m_isrcCode` is `uint32_t`, too small for 60 bits
anyway), the `qMode() == QMode3` branch in `dec_f2sectioncorrection` can never be true (mode-3
sections always arrive invalid), and the `m_qmode3Sections` statistic always reports 0. Net behaviour
is safe — the section's audio is reconstructed via the invalid-gap path — but the AFRAME it carries is
thrown away rather than used for continuity, and the dead branch misleads readers.

**Fix:** Parse mode 3 like mode 2: extract AFRAME, keep the section valid with expected-time
reconciliation, decode I1–I5 via the §17.5.3 6-bit table and I6–I12 as BCD into a string field. At
minimum, remove the unreachable branch and fix the statistics.

### Q-5 — Interpolated track times for missing sections run backwards — **Major** *(verified directly)*

**Location:** `decoders/dec_f2sectioncorrection.cpp:337-357`.
**Spec:** IEC 60908 §17.5.1 — running time increases during audio.

For a gap of N sections before a real section with track time T, the section inserted at absolute
time `expected + i` gets track time `T − (i + 1)`: track times **decrease** as absolute time
increases, then jump. Example (N=3, T=1000): inserted times are 999, 998, 997. This corrupts track
start/end statistics and the WAV metadata writer's bookkeeping across the gap.

**Fix:** `int32_t newFrames = f2Section.metadata.sectionTime().frames() - (missingSections - i);`

### Q-6 — INDEX/POINT byte never decoded: no TOC, no pause detection, lead-in mishandled — **Minor** (spec gap)

**Location:** `efm-lib/subcode.cpp:240-287` (byte `qChannelData[2]` never read);
`decoders/dec_f2sectioncorrection.cpp:105-198`.
**Spec:** IEC 60908 §17.5.1 — in lead-in, byte 2 is POINT and bytes 7–9 are PMIN/PSEC/PFRAME (TOC
entries incl. A0/A1/A2); in the program area byte 2 is INDEX (00 = pause, time counts down).

Consequences: no TOC is built; pauses are indistinguishable from audio (so gap interpolation counts
up where the disc counts down — `writer_wav_metadata.cpp:216-223` documents the resulting wrong
track-1 start time); lead-in PMIN/PSEC/PFRAME are stored as absolute time, which repeats/jumps
between TOC items, so `waitForInputToSettle` (exact `+1` over 5 sections,
`dec_f2sectioncorrection.cpp:112-114`) can never settle inside a lead-in — the decoder works only
because captures start in the program area.

**Fix:** Decode byte 2. Represent INDEX (0 = pause) in metadata; in lead-in, treat POINT/PMIN/PSEC/
PFRAME as TOC data instead of ATIME (or skip lead-in explicitly); base settling on MIN/SEC/FRAME,
which does increment in lead-in.

### Q-7 — Mode-2/3 absolute-time reconstruction can misplace a section by up to 74 frames — **Minor**

**Location:** `decoders/dec_f2sectioncorrection.cpp:253-259`; unnormalised empty-buffer seed path
`:205-220`.

MM:SS is taken from `expectedAbsoluteTime` and the frame number from the disc's AFRAME. If a section
was physically lost just before a mode-2 block that crosses a second boundary, the reconstruction is
off by ±1 second worth of frames — the section is either dropped as out-of-order or up to 74 phantom
"missing" sections are fabricated. The empty-buffer path additionally seeds the buffer with a
`00:00:frame` mode-2 section with no normalisation at all, poisoning `getExpectedAbsoluteTime()`
(see R-3 for the memory consequence).

**Fix:** Choose the MM:SS that minimises |reconstructed − expected| (test expected ±1 s); never seed
an empty buffer from a mode-2/3 section (or normalise first).

### Q-8 — Pre-emphasis flag extracted but never consumed — **Minor**

**Location:** set in `efm-lib/subcode.cpp:165-238`; no consumer of `hasPreemphasis()` outside
serialisation (repo-wide search); audio path `efm_audio_decode_stage.cpp:109-129` applies no
de-emphasis.
**Spec:** IEC 60908 §17.5 CONTROL — "00X1: 2 audio channels with pre-emphasis 50/15 µs".

PCM from pre-emphasised discs/LD digital soundtracks is written and played back without 50/15 µs
de-emphasis and without surfacing the flag (no report line, no WAV metadata entry). The flag can
change per track.

**Fix:** At minimum report the flag per track; ideally apply (or offer) a 50/15 µs de-emphasis
biquad in the audio path when set.

### Q-9 — `Subcode::toData()` is dead code with multiple latent bugs — **Info**

**Location:** `efm-lib/subcode.cpp:420-585` (no callers, verified by search).

If ever used: lead-out sections throw (internal `trackNumber==0` trips the `!= LeadIn` guard, so TNO
0xAA is never emitted); QMode2/3/4 payloads are encoded with the mode-1 layout under the wrong ADR;
INDEX is hardcoded to 01; lead-in TOC is not encodable.

**Fix:** Delete it (git preserves history), or fix before any subcode-regeneration use.

### Q-10 — Small subcode items — **Info**

- `subcode.cpp:377-396`: the post-parse sanity check logs "Track number 0 is only valid for lead-in"
  for **every lead-out** section (lead-out is internally track 0, never 0xAA), and the 0xAA branch is
  unreachable. Debug noise; rework or remove.
- `subcode.cpp:687-703` (`repairData`): only bits 0–79 are trial-flipped; single-bit errors in the 16
  CRC bits (~17 % of single-bit errors) are never repaired (harmless — those sections fall back to
  interpolation). False-repair risk is adequately mitigated by the ±10-frame plausibility check in
  `dec_f3frametof2section.cpp:301-314`.
- `subcode.cpp:630, 641, 664`: `isCrcValid`/`getQChannelCrc` take vectors **by value** and copy again
  to drop the CRC bytes; `repairData` calls this up to 80× per damaged section. Pass `const&` and
  compute over `size()-2`.
- `subcode.cpp:83`: `modeNybble >= 0x0` is tautological on an unsigned value.

---

## 3. Error correction

### E-1 — C2 clamp discards legitimate 3- and 4-erasure corrections — **Major** *(cross-confirmed ×2, verified directly)*

**Location:** `efm-lib/reedsolomon.cpp:138-151` (`c2Decode`).
**Spec:** IEC 60908 §16.3 / ECMA-130 Annex C — C2 is a (28,24) RS code, distance 5, corrects up to
4 erasures. Erasure correction at known positions is the mechanism CIRC's cross-interleave exists to
feed.

```cpp
if (erasures.size() > 4) { ... }                     // admits up to 4 erasures…
int result = c2rs.decode(tmpData, erasures, &position);
if (result > 2) result = -1;                         // …then rejects any decode that changed >2 symbols
```

ezpwd's `decode` returns the number of symbols actually changed (verified against the shipped
`ezpwd/rs_base` headers). After a C1 burst failure all 28 outputs are flagged and the delay lines
spread them across 28 C2 words, so 3–4 genuinely-corrupt erasures per C2 word is the *normal* burst
case — and its guaranteed-valid correction is thrown away, with all 24 outputs flagged bad. Net
effect: CIRC's designed burst tolerance (~4 erasures/word) is roughly halved. Secondary: when the
result is clamped to −1, `inputData = std::move(tmpData)` still propagates the decoder-modified
bytes rather than the received bytes.

**Fix:** Accept erasure-dominated corrections up to full capacity: with `s` supplied erasures and `e`
located errors, accept when `2e + s ≤ 4` (compare ezpwd's corrected-position output against the
supplied erasure list to compute `e`). If zero-margin s=4 miscorrection is a concern, accept s≤4 with
e=0 and s≤2 with e≤1. On rejection, restore the received bytes.

### E-2 — C1 strategy: refuses >2 erasures, rejects the valid s=2,e=1 case — **Minor**

**Location:** `efm-lib/reedsolomon.cpp:71-82`.

C1 erasure flags come from the EFM demodulator (invalid 14-bit symbols — reliable erasures), yet 3–4
flagged symbols are passed straight to C2 without attempting the in-capacity erasure decode, and the
same `result > 2 → -1` clamp discards the legal 2-erasure + 1-error combination (2e+s = 4). Note the
decoder *does* accept unvalidated t=2 error corrections at C1, the miscorrection-prone case that
conservative CIRC strategies restrict — the current mix is "aggressive on errors, timid on erasures".

**Fix:** Attempt erasure decoding for s ≤ 4 at C1; accept combinations satisfying 2e + s ≤ 4.
Document the chosen strategy (§5.3.7) against CIRC decoding practice.

### E-3 — RSPC: single P/Q pass, no iteration, no convergence check — **Major** *(verified directly)*

**Location:** `decoders/dec_rawsectortosector.cpp:189-220`; `efm-lib/rspc.cpp`.
**Spec:** ECMA-130 §14.3–14.6, Annex A; standard product-code decoding practice.

Exactly one Q pass then one P pass runs, then EDC decides. RSPC is a product code: P corrections
create newly-correctable Q words and vice versa; standard CD-ROM decoders iterate 2–4 rounds or until
EDC passes. Correction capability is well below what the code structure supports.

**Fix:** Loop `{Q pass; P pass;}` until EDC passes, a pass makes zero corrections, or an iteration
cap (~4) is reached. Requires E-4's flag maintenance to be fully effective.

### E-4 — RSPC never updates erasure flags; parity-byte erasures ignored on the Q side — **Minor** *(cross-confirmed ×2)*

**Location:** `efm-lib/rspc.cpp:65-98, 155-176`; `decoders/dec_rawsectortosector.cpp:193-200, 339-344`.

Four related defects: (a) after a successful codeword decode, corrected bytes are copied back but
`errorData` is never cleared, so the P pass consumes stale erasures and a "corrected, valid" sector
still reports its now-correct bytes as errors downstream; (b) failed codewords never *set* flags;
(c) `if (erasures > 2) erasures.clear()` then blind error-only decoding on a distance-3 code has a
substantial miscorrection probability, and miscorrected bytes are written back unflagged (EDC
prevents a false-valid sector but the damage can defeat the P pass); (d) the Q pass registers
erasures only for the 43 data symbols — a CIRC-flagged corrupt Q-parity byte (`qField[43]`,
`qField[44]`) is treated as trusted, wasting margin (the P pass handles its parity correctly), and
the P pass copies back only data symbols, discarding corrected P-parity bytes that Q protects.

**Fix:** On success, clear flags for corrected positions and write back all symbols including
parity; on failure, set flags; skip (or EDC-validate) blind decodes of words with >2 erasures;
register Q-parity erasure positions 43/44.

### E-5 — EDC-failed sectors replaced with all-zero dummies — **Minor** (archival policy)

**Location:** `decoders/dec_rawsectortosector.cpp:220-239, 348-351`;
`decoders/dec_sectorcorrection.cpp:87-96`.

A sector that still fails EDC after RSPC is discarded entirely; `SectorCorrection` fills the address
gap with 2048 zero bytes flagged all-error. For an archival decoder, emitting the best-effort
partially-corrected bytes with accurate per-byte flags (`dataValid(false)`) is strictly more useful
than zeros.

**Fix:** Emit the failed sector's data with per-byte flags and let the sink decide.

### E-6 — Audio concealment: single-sample interpolation only; hard mute without ramp; edge sections uncorrected — **Minor**

**Location:** `decoders/dec_audiocorrection.cpp:40 (TODO), 119-151, 191-223, 274-282`.
**Spec:** IEC 60908 does not mandate concealment; player practice is interpolation over short gaps
and ramped muting for bursts.

A flagged sample is interpolated only when *both* immediate neighbours are clean; any run of ≥2
flagged samples is hard-silenced to 0 with no fade, producing audible clicks at burst boundaries.
The first section of a stream and the last (via `flush()`) are emitted uncorrected (acknowledged by
the TODO). Verified correct: the 3-section neighbour window has no off-by-one; channel assignment is
consistent; concealed samples are flagged `concealed=1, error=0`.

**Fix:** Interpolate linearly across n-sample gaps between the flanking clean samples; add a short
(~2–4 sample) mute ramp; prime/flush the window so edge sections are corrected.

### E-7 — CIRC warm-up frames emitted as fully valid; pipeline tail never flushed — **Minor**

**Location:** `decoders/dec_f2sectiontof1section.cpp:115-169`; `efm_processor.cpp:128-152`.

While the delay lines fill, substituted output frames are zero-filled with `errorData` **and**
`paddedData` all 0 — fabricated zeros marked as genuine data (should be padded=1). Symmetrically, at
end of stream the ~108 frames still inside the delay lines are never flushed, silently dropping the
capture tail (normally hidden by lead-out, but real for truncated captures).

**Fix:** Mark warm-up substitution frames as padded; add an end-of-stream flush that pushes padding
frames through the CIRC chain to recover the tail. **Done:** warm-up substitutes are now marked
`padded=1` (`dec_f2sectiontof1section.cpp`), and `F2SectionToF1Section::flush()` drains the ~111-frame
delay-line tail (max latency = delayLine1 1 + delayLineM 108 + delayLine2 2), rounded up to whole
sections and called from `EfmProcessor::finishStream()` before the audio-correction flush.

### E-8 — Statistics inaccuracies — **Minor** *(cross-confirmed ×2 for the first item's family)*

- `m_uncorrectableSections` (`dec_f2sectioncorrection.cpp:26, 723`) is reported but never
  incremented — the uncorrectable path throws instead (R-1/R-2). Always 0.
- `dec_tvaluestochannel.cpp:430-432`: long/short frame statistics inverted (`< frameSize` counted as
  "long", `>` as "short") — opposite of the convention at lines 179-181.
- `RawSectorToSector` mode counters: a sector that *fails* correction still increments
  `m_mode1Sectors` (`dec_rawsectortosector.cpp:229`) while a successfully RSPC-corrected sector
  increments no mode counter.
- `dec_f1sectiontodata24section.cpp:130-167`: "Total bytes" double-counts corrupt bytes; the final
  percentage divides by a possibly-zero `validBytes`, and the multiplication truncates the 64-bit
  counters through a `uint32_t`.
- `Rspc` "Got X correct out of 52" conflates clean codewords with corrected ones; no RSPC statistics
  are surfaced in `showStatistics`.

**Fix:** Straightforward in each case; standardise cumulative counters on `uint64_t` (see P-10).

---

## 4. Robustness and error handling

### R-1 — `std::runtime_error` thrown inside the pipeline bypasses the EFM stage-boundary handler — **Critical** *(cross-confirmed ×3)*

**Location (throw sites):** `dec_f2sectioncorrection.cpp:462, 521, 614`;
`dec_channeltof3frame.cpp:161, 191, 198`; `dec_tvaluestochannel.cpp:455`;
`dec_f3frametof2section.cpp:279`.
**Catch sites:** `efm_sink/efm_sink_stage_deps.cpp:114` and
`efm_audio_decode/efm_audio_decode_stage_deps.cpp:110` catch **only** `efm::EfmDecodeError`.

Project rule (`efm_exception.h`): the EFM decoder must throw `efm::EfmDecodeError`, caught at the
stage boundary, because a worker-thread escape kills the GUI. Eight sites throw plain
`std::runtime_error` instead, and several are reachable from **corrupt input**, not just invariant
violations: an out-of-range T-value survives to `ChannelToF3Frame::tvaluesToData` (nothing clamps on
this path — the clamping helper `Tvalues::tvaluesToBitString` exists but is unused), and ≥11
consecutive Q-invalid sections (~0.15 s of damaged subcode) or inconsistent timestamps around a gap
trigger the two `dec_f2sectioncorrection` throws. `efm_sink` happens to survive via a generic outer
catch; the `efm_audio_decode` lazy-decode path (`ensure_decoded` inside `std::call_once`) has no
such backstop and also skips the stage-catch cleanup (cache-file removal).

**Fix:** Convert all eight throws to `efm::EfmDecodeError`; widen the deps-level catches to
`std::exception` as a backstop. For the T-value site, prefer clamping + flagging the symbol as an
erasure over throwing (dirty-capture data, not an invariant).

### R-2 — Data-driven conditions abort the whole decode instead of degrading — **Major**

**Location:** `dec_f2sectioncorrection.cpp:521` ("gap too large"), `:614` ("uncorrectable error in
internal buffer"); `dec_rawsectortosector.cpp:274-285` (valid EDC + unexpected mode byte throws
`EfmDecodeError`, comment says "bug?").

">10 consecutive Q-CRC-invalid sections" and "gap length ≠ time difference" are disc conditions
(damage, mastering discontinuities, lead-in→program time reset), not programming errors; a 16-bit
CRC also passes ~1/65536 corrupt Q blocks, making a CRC-valid-but-wrong timestamp bracketing a bad
run entirely plausible. Each currently aborts the entire decode.

**Fix:** Replace with a resync path: flush what is provable, emit flagged/padded sections, re-enter
the settle state. Downgrade the invalid-mode case to a per-sector discard.

### R-3 — Unbounded missing-section fill: one bad timestamp can allocate hundreds of MB — **Major** *(cross-confirmed ×3)*

**Location:** `decoders/dec_f2sectioncorrection.cpp:280-413`.

`missingSections = actual − expected` is used directly as a loop bound inserting fully-populated
98-frame sections (~10 KB each). A CRC-colliding corrupt timestamp (16-bit CRC) or the Q-7
empty-buffer seed jumping toward 59:59:74 fabricates up to ~270,000 sections ≈ multi-GB of buffered
frames — memory exhaustion on hostile or badly damaged input. Compounding it, `outputSections()`
pops exactly **one** section per input push, so a backlog created by any large gap never drains
until `flush()`, and `processInternalBuffer()` rescans the whole deque per push — O(N) per section,
quadratic over gap-heavy captures.

**Fix:** Cap the fill (treat gaps beyond a few seconds as a hard resync, or require two consecutive
consistent timestamps before honouring a jump); drain the output buffer fully (keep only a
`m_maximumGapSize + 2` lookahead window); start the invalid-section scan at the first unprocessed
index.

### R-4 — Rejected false-positive sector sync is never skipped: permanent stall + unbounded growth — **Major** *(verified directly)*

**Location:** `decoders/dec_data24torawsector.cpp:347-378`.

`waitingForSync()` erases only the bytes *before* a found sync pattern. When the pattern is then
rejected as a false positive (`errorByteCount > 1000 && paddingByteCount > 1000`), nothing is
removed — the bogus sync stays at position 0, every subsequent call re-finds it, re-counts the same
2352 bytes, and re-rejects. Sync is never re-acquired downstream of that point and
`m_sectorData`/`m_sectorErrorData`/`m_sectorPaddedData` grow unboundedly for the remainder of the
stream.

**Fix:** On rejection, erase at least the 12 sync bytes (or search from `syncPatternPosition + 1`
for the next candidate) before returning to `WaitingForSync`.

### R-5 — Latent out-of-bounds indexing (traps under this build's hardened libc++) — **Minor**

- `efm-lib/audio.cpp:33-48, 106-122` (`setDataLeftRight`/`setErrorDataLeftRight`, currently
  **unused**): loops `i = 0,2,…,10` index 6-element channel vectors → OOB at i≥6, and even in-bounds
  the interleave is wrong (should be `dataLeft[i/2]`). Fix or delete.
- `efm-lib/audio.cpp:170-224` (`countErrors*`, `showData`): index `m_audioErrorData[i]` up to
  `frameSize()` without checking the vector is non-empty — a default-constructed `Audio` traps.
- `decoders/dec_audiocorrection.cpp:43`: `m_inputBuffer.front()` with no empty check (safe today
  only because the single caller pushes first).
- `decoders/dec_tvaluestochannel.cpp:229-252` (`handleUndershoot`): a `-1` second-sync index would
  reach `countBits(..., -1, ...)`; currently unreachable but unguarded. Related: the intended
  "wait for more data" branch at `:333-338` is dead (the state machine guarantees `size > 382`), so
  that recovery path never runs.

---

## 5. Efficiency

### P-1 — Whole-section deep copies at every stage boundary — **Critical (perf)** *(verified directly)*

**Location:** all decoders; e.g. `dec_f2sectioncorrection.cpp:54-58` (`popSection`:
`F2Section section = m_outputBuffer.front(); m_outputBuffer.pop();`), the mirrored
`pushSection(const X&)` copies, and third copies via `front()`/`pop_front()` in `processQueue()`;
driver `efm_processor.cpp:284-311`.

One `F2Section` copy is 98 frames × 3 vectors ≈ ~300 heap allocations; with ~6 section boundaries,
each decoded section incurs thousands of avoidable allocations. This is the largest CPU sink outside
the RS decoders.

**Fix:** `X section = std::move(buffer.front()); buffer.pop();`; add rvalue/by-value `pushSection`
overloads and `std::move` at the call sites in `EfmProcessor`. Longer term, make `Frame`'s three
byte vectors fixed `std::array`s (sizes are compile-time constants 24/32), eliminating per-frame
heap traffic entirely.

### P-2 — `RawSector`/`Sector` accessors return by value: ~20 full 2352-byte copies per sector — **Critical (perf, data path)** *(verified directly)*

**Location:** `efm-lib/sector.cpp:129-133, 200-204`; consumers throughout
`decoders/dec_rawsectortosector.cpp` (single-byte reads like `rawSector.errorData()[15]`, the EDC
word assembled via four separate `data()` calls, `crc32(rawSector.data(), 2064)`, …).

**Fix:** Return `const std::vector<uint8_t>&` (as `Frame::data()` already does); delete the
defensive local copies at `dec_rawsectortosector.cpp:332-336`.

### P-3 — Audio correction allocates fresh vectors per sample — **Critical (perf, audio path)**

**Location:** `decoders/dec_audiocorrection.cpp:59-79, 98-118, 166-189`;
`efm-lib/audio.cpp:62-90, 137-167`.

`Audio::dataLeft()/dataRight()/errorData*()` each build and return a new 6-element vector; the
per-sample correction loop calls them repeatedly on the same frame (~70+ allocations per frame,
~7,000 per section), plus per-frame `Audio` copies and un-reserved `push_back` output vectors.

**Fix:** Give `Audio` `const&` accessors to the interleaved data and index `data[2*i] / data[2*i+1]`
directly; hoist references out of the loops; reserve or use `std::array<int16_t, 12>` scratch
buffers.

### P-4 — Consuming stages buffer the entire multi-GB capture in RAM despite the streaming API — **Major (memory)**

**Location:** `efm_sink/efm_sink_stage_deps.cpp:47-66`;
`efm_audio_decode/efm_audio_decode_stage_deps.cpp:72-77`;
`efm_audio_decode/efm_audio_decode_stage.cpp:113-130`.

`EfmProcessor` provides `beginStream/pushChunk/finishStream` precisely to avoid a temporary buffer,
and `raw_efm_sink` uses it correctly — but `efm_sink` and `efm_audio_decode` first concatenate **all**
frames' t-values into one buffer, then re-chunk it at 1024 bytes. The audio conversion additionally
holds raw PCM + widened int32 + per-frame blocks + flattened output simultaneously (~4–5× decoded
size).

**Fix:** Push `get_efm_samples(fid)` straight into `pushChunk()` per frame; stream the
cache→widen→resample→sync-cache conversion in bounded windows.

### P-5 — Hot-path logging: `RawSector::showData()` formats ~49 hex lines per sector even when trace is compiled out — **Major (data path)**

**Location:** `efm-lib/sector.cpp:139-178`; called unconditionally at `efm_processor.cpp:419`.

The strings are built *before* `ORC_LOG_TRACE`, which is a no-op at this library's compiled log
level — 100 % wasted formatting per sector. `Data24::showData`/`F1Frame::showData` already do this
correctly with a `should_log(trace)` early-out.

**Fix:** Add the same guard to `RawSector::showData()`/`Sector::showData()` (and the unguarded
INFO-level `F2Frame::showData()`), or drop the unconditional call.

### P-6 — Per-frame temporaries in the CIRC core and deinterleaver — **Minor** (aggregate: runs 7,350×/s of audio)

**Location:** `efm-lib/reedsolomon.cpp:62-69, 128-135` (per-call `tmpData`/`erasures`/`position`
vectors); `efm-lib/interleave.cpp:30-111` (three output vectors allocated per frame, copied back by
assignment instead of `std::move`); `efm-lib/rspc.cpp:36, 119` (`QRS`/`PRS` codec objects — Galois
tables — constructed inside every call, plus a fresh `Rspc` per corrupt sector at
`dec_rawsectortosector.cpp:189`).

**Fix:** Fixed `std::array<uint8_t, 32/28/24>` scratch members; `std::move` at the end of
`deinterleave`; construct the RS codecs once (see also P-12 on their thread-safety).

### P-7 — Small allocation/reserve items — **Minor**

- `dec_channeltof3frame.cpp:152-153`: `outputData.reserve((tValues.size() + 7) / 8)` under-reserves
  ~4× (each T-value emits 3–11 *bits*), causing 2–3 reallocations per frame. Reserve ~80 bytes.
- `dec_tvaluestochannel.cpp` (8 sites, e.g. `:62-66, 128-129`): buffer consumption via
  `m_internalBuffer = std::vector<uint8_t>(begin+n, end)` — one allocation + tail copy per frame
  where `erase(begin, begin+n)` is a single memmove. Also `pushFrame` copies each chunk into a
  one-element `std::queue` that `processStateMachine` immediately pops — append directly and delete
  `m_inputBuffer`.
- `writers/writer_wav.cpp:51-56`, `writer_raw.cpp:57-67`: per-frame `Audio` copies + by-value
  `data()` feeding 24-byte `ofstream::write`s; `writer_wav_metadata.cpp:115` declares a per-frame
  `audioData` local that is never used. Use `const&` and assemble one section-sized buffer per write.
- Missing reserves: `dataValues`/`errorValues` (32) in `dec_channeltof3frame.cpp:108-121`,
  `subcodeData` (98) in `dec_f3frametof2section.cpp:285-288`.

### P-8 — EFM 14→8 lookup: hash map where a flat table fits — **Minor**

**Location:** `efm-lib/efm.cpp:87-103`.

The `std::unordered_map<uint16_t, uint16_t>` lookup is O(1) but pays hash + bucket-chase ~240K
times/s of audio, and the map is rebuilt per `Efm` instance. A `std::array<uint16_t, 16384>` reverse
table (32 KB, indexed load, buildable once) is strictly better.

### P-9 — Single-threaded pipeline — **Info** (improvement opportunity)

All 11 decoder stages run sequentially on the caller's thread (`efm_sink_stage.cpp:227` runs the
whole decode inside `trigger()`). The push/pop boundaries make a two-stage split (bit-level front end
vs section-level back end) with a bounded SPSC queue straightforward, roughly halving wall time on
large captures. Do the free wins (P-1..P-3) first; P-12 must be resolved before any parallelism.

### P-10 — Statistics counter widths inconsistent; overflow on long captures — **Minor**

**Location:** `dec_tvaluestochannel.h:45-47` (`uint32_t` t-value counters wrap after ~95 min of
audio — an 80-minute CD barely fits); `dec_audiocorrection.h:39-41` (`uint32_t` sample counters wrap
after ~13.5 h); `dec_f1sectiontodata24section.cpp:130` (64-bit counters truncated through a
`uint32_t` product, divide-by-zero risk at `:167`). Other decoders already use 64-bit.

**Fix:** Standardise cumulative statistics on `uint64_t`; guard the division.

### P-11 — Dead code and build hygiene — **Minor / Info**

Confirmed unused by repo-wide search: `Audio::setDataLeftRight`/`setErrorDataLeftRight` (buggy — see
R-5), the `Tvalues` class on the decode path (its clamping is exactly what R-1's T-value site needs),
`F3FrameToF2Section::m_inputBuffer`, `F2SectionCorrection::m_window`,
`AudioCorrection::m_firstSectionFlag` and the declared-but-undefined `convertToAudacityTimestamp`,
`TvaluesToChannel::m_frameData` and `m_tvalues`, `file_io.h`, `section_io.h`, `F2Section::showData`,
`Sector::showData`, large commented-out `QDataStream` blocks in `frame.cpp`/`section.cpp`/
`section_metadata.cpp`, and the no-op zero-fill loop in `Data24::setData` (`frame.cpp:167-172`,
which also silently accepts >24 bytes, unlike `Frame::setData`).

Build hygiene (`common/efm-decode/CMakeLists.txt:11-14`): the library compiles with `-w` (all
warnings suppressed — hiding the signed/unsigned mixes) and forces `-O2` in Debug; unlike
`orc-core`/`orc-gui`, no `SPDLOG_ACTIVE_LEVEL` is set, so every `ORC_LOG_DEBUG`/`ORC_LOG_TRACE` in
the EFM pipeline is compiled out in **all** configurations — `--log-level debug` can never show EFM
internals (currently also masking the hot-loop debug format costs). Replace `-w` with targeted
suppressions and make the log-level choice deliberate.

Magic numbers (project rule §5.3.5): 588, 98, 2352, 382, `0x0B`, the 550/600 window, and the
1000/1000 false-positive thresholds recur inline across decoders; a shared `efm_constants.h` with
IEC/ECMA section references (§5.3.6) would help. Documentation nits: `efm.cpp:14-15` says "10-bit
EFM code" (it is 14-bit); `reedsolomon.h:15-16` mislabels the ezpwd macro parameters (`POLY, INIT,
FCR, AGR` — actually `POLY, FCR, PRIM, DUAL`; the values are correct, but the wrong labels invite a
future mis-edit of the safety-critical FCR); `dec_f3frametof2section.cpp:202-216` claims undershoot
padding is "interleaved" but inserts a contiguous block; `section_metadata.cpp:208-238` duplicates
validation with log messages naming the wrong function.

`efm_processor.cpp:249-302` (`drainPipeline`): ~10 `high_resolution_clock::now()` pairs per
1024-t-value chunk even when queues are empty, and the timing buckets are attributed one stage late
(e.g. "Channel to F3" actually times the T-values state machine pop). Guard with a single readiness
check and re-scope the buckets if the report is meant to be trusted.

### P-12 — Global mutable RS codec objects; undocumented thread-safety — **Info** (Major if pipelines run concurrently)

**Location:** `efm-lib/reedsolomon.cpp:19-34` (`c1rs`/`c2rs`, file-scope mutable globals with
byte-identical template parameters); `rspc.cpp:19-29` (`QRS`/`PRS`, same pattern).

Every `EfmProcessor` in the process shares these. `efm_sink` and the lazy `efm_audio_decode` decode
are independent stages that could plausibly run simultaneously; thread-safety then rests on
undocumented ezpwd internals. Project rule §5.3.3 requires documented thread-safety guarantees.

**Fix:** Collapse the duplicate configurations to one type; make the codecs members of
`ReedSolomon`/`Rspc` (cheap relative to a decode) or document ezpwd `decode()` const-safety.

---

## Things done well (for balance)

- The delay lines are O(1) ring buffers with data, error, and padded flags stored in the same slot —
  flag/data alignment through the CIRC chain is correct **by construction**.
- All bit-level tables are machine-verifiably correct: EFM code table, subcode syncs, scramble
  table, EDC table, RSPC geometry, CIRC permutations and delays, RS field parameters.
- The bit path is table-driven throughout (no linear scans); sync detection uses `std::search` over
  t-values; sections `reserve(98)`; the front-end state machines bound their buffers.
- A genuine streaming API exists with a carefully ordered flush sequence and documented rationale,
  and `raw_efm_sink` demonstrates its correct use.
- Decode-failure messages are user-actionable ("no frame sync at all" / "no valid timecodes" / "no
  contiguous run" with concrete advice), backed by dedicated diagnostic counters.
- The Q-CRC-failure reconstruction design (interpolate only when gap length equals the time
  difference, cap at 10, re-invalidate implausible repairs) is a sound core approach, and the
  track-boundary projection added in `3b8a3d84` correctly avoids the negative-time crash.
- No `std::exit` anywhere in the decoder tree; most guard paths already use `efm::EfmDecodeError`.
