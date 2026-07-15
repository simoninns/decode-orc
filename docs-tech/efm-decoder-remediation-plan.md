# EFM Decoder — Remediation Plan for Outstanding Review Items

Follow-up to [`efm-decoder-review-findings.md`](./efm-decoder-review-findings.md). A verification pass
on branch `20260714-efm` (HEAD `a067aaab`) confirmed that **every Critical and Major finding that
produces wrong output or aborts the decode has been fixed**, and the two `**Done:**`-marked items
(Q-8 pre-emphasis, E-7 warm-up/tail flush) are present in code.

This plan covers only the **partial** and **not-done** items that remain. Each task cites the current
file location and a concrete acceptance criterion. Phases are ordered by risk × value: correctness
first, then the one remaining memory defect, then hot-path performance, then the larger spec-gap, then
cleanup.

Status legend from verification: ⚠️ partial · ❌ not done.

---

## Phase 1 — Correctness completions (small, high value)

Self-contained fixes that were explicitly named in the findings and either lose data or leave a real
bug. Each is a handful of lines.

### 1.1 — C-1: fix the missed start-time sentinel ⚠️
- **File:** [`decoders/dec_data24toaudio.cpp:19`](../orc/plugins/stages/common/efm-decode/decoders/dec_data24toaudio.cpp#L19)
- **Problem:** `m_startTime(SectionTime(59, 59, 74))` is a min-finder seed; for a disc whose earliest
  section lies between 60:00:00 and 99:59:74 the seed never updates, so reported start/total time is
  wrong. Every other time class was raised to 99:59:74 — this sentinel was overlooked.
- **Fix:** change the seed to `SectionTime(99, 59, 74)`.
- **Accept:** a synthetic capture starting > 60 min reports the correct start/total time.

### 1.2 — C-5: size the emitted sector payload by mode ⚠️
- **File:** [`decoders/dec_rawsectortosector.cpp:391`](../orc/plugins/stages/common/efm-decode/decoders/dec_rawsectortosector.cpp#L391)
  (mirror the error/padded slices immediately below)
- **Problem:** every accepted sector emits exactly bytes `16..16+2048` regardless of mode. Mode-2
  sectors lose their last 288 user-data bytes; today only an `ORC_LOG_WARN` marks the loss.
- **Fix:** emit `2048` for mode 1, `2336` (bytes 16..2351) for mode 2, and either zero-verify or emit
  for mode 0. Slice `data`, `errorData`, and `paddedData` to the same mode-dependent length. Prefer a
  named constant per mode over inline literals (see Phase 5 magic-number cleanup).
- **Accept:** a mode-2 sector round-trips all 2336 user bytes; mode-1 output length unchanged.

### 1.3 — Q-7: minimise |reconstructed − expected| for mode-2/3 absolute time ⚠️
- **File:** `decoders/dec_f2sectioncorrection.cpp` (the mode-2/3 absolute-time reconstruction that
  combines `expectedAbsoluteTime` MM:SS with the section's raw AFRAME)
- **Problem:** MM:SS is taken from the expected time and the frame from AFRAME. If a section was lost
  just before a mode-2/3 block that crosses a second boundary, the result is off by ±1 s of frames →
  dropped as out-of-order or up to 74 phantom missing sections fabricated. (The empty-buffer seed half
  of Q-7 is already fixed.)
- **Fix:** evaluate the candidate reconstruction at expected MM:SS − 1 s, MM:SS, and MM:SS + 1 s;
  choose the one minimising `|reconstructed − expected|` in frames.
- **Accept:** a unit test placing a mode-2 block one frame after a second boundary with a preceding
  gap reconstructs to the correct absolute time (no phantom-gap fabrication).

### 1.4 — R-2: downgrade the "valid EDC + unexpected mode" throw to a per-sector discard ⚠️
- **File:** [`decoders/dec_rawsectortosector.cpp:297`](../orc/plugins/stages/common/efm-decode/decoders/dec_rawsectortosector.cpp#L297)
- **Problem:** when the original EDC passed but `data()[15]` is an out-of-range mode, the code throws
  `EfmDecodeError` ("... even though sector data was valid - bug?"). A 32-bit EDC still admits a rare
  false-valid, so this is a disc condition, not an invariant — it should not abort the whole decode.
- **Fix:** replace the throw with a per-sector discard (`m_invalidModeSectors++` and drop), matching
  the failed-EDC invalid-mode path already present.
- **Accept:** a crafted sector with valid-looking EDC and mode byte 0x07 is discarded, decode
  continues.

### 1.5 — R-5(d): guard the undershoot `secondSyncIndex == -1` path; remove dead branch ⚠️
- **File:** `decoders/dec_tvaluestochannel.cpp` (`handleUndershoot`)
- **Problem:** `secondSyncIndex` can be `-1`; it is then used unguarded — `m_internalBuffer.begin() +
  secondSyncIndex` yields `begin() - 1` (OOB, traps under the hardened libc++), and
  `m_discardedTValues += secondSyncIndex` adds −1. Separately, the "wait for more data" branch testing
  `size() <= 382` is unreachable (the caller only enters when `size > 382`).
- **Fix:** early-return / treat as no-second-sync when `secondSyncIndex == -1`; delete the dead
  `<= 382` branch (or convert it into the real recovery path if one is intended).
- **Accept:** fuzzing malformed t-value runs through `handleUndershoot` no longer traps; no behaviour
  change on well-formed input.

### 1.6 — E-8(e): fix RSPC codeword statistics ⚠️
- **File:** `efm-lib/rspc.cpp` (the "Got X correct out of 52/86" debug logs and `successfulCorrections`
  counter); surface via `rspc.h`
- **Problem:** `successfulCorrections` counts clean (`fixed == 0`) and genuinely-corrected codewords
  identically, so the statistic conflates "already valid" with "repaired"; no codeword-level RSPC
  stats are exposed to `showStatistics`.
- **Fix:** separate clean vs corrected counts; expose getters so the processor can report real RSPC
  correction activity.
- **Accept:** `showStatistics` distinguishes clean from corrected P/Q codewords.

---

## Phase 2 — Memory: stream the consuming stages (Major) ❌

### 2.1 — P-4: push per-frame samples straight into `pushChunk`
- **Files:**
  [`efm_sink/efm_sink_stage_deps.cpp:54-125`](../orc/plugins/stages/efm_sink/efm_sink_stage_deps.cpp#L54),
  [`efm_audio_decode/efm_audio_decode_stage_deps.cpp:72-102`](../orc/plugins/stages/efm_audio_decode/efm_audio_decode_stage_deps.cpp#L72)
- **Problem:** both stages `reserve(total_tvalues)`, concatenate **all** frames' t-values into one
  buffer, then re-chunk at 1024 bytes — buffering the entire multi-GB capture in RAM. `raw_efm_sink`
  already uses the streaming `beginStream`/`pushChunk`/`finishStream` API correctly.
- **Fix:** iterate frames and push `get_efm_samples(fid)` directly into `pushChunk()` (accumulate into
  a bounded ~1024-byte staging buffer only to preserve chunk sizing). For `efm_audio_decode`,
  additionally stream the cache→widen→resample→sync-cache conversion in bounded windows rather than
  holding raw PCM + int32 + per-frame blocks + flattened output simultaneously.
- **Accept:** peak RSS on a large capture is bounded and roughly independent of capture length;
  decoded output byte-identical to the buffered path. Model the change on the existing `raw_efm_sink`
  usage.
- **Note:** touches the cancel/progress plumbing — keep the existing progress-modal callbacks working.

---

## Phase 3 — Hot-path performance remnants (Minor, aggregate wins)

These are the leftovers from the perf findings whose *hot paths* were already fixed; do them together
so the pipeline's remaining value-semantics cost is cleared in one sweep.

### 3.1 — P-1: broaden move-on-pop; consider fixed-size Frame buffers ⚠️
- Move-on-pop + rvalue `pushSection` was added to ~3 decoders. Apply the same pattern to the
  remaining pop/push pairs: `F1SectionToData24Section::popSection`, `ChannelToF3Frame::popFrame`,
  `TvaluesToChannel::popFrame`, `F3FrameToF2Section::popSection`, and the matching lvalue call sites
  in `efm_processor.cpp` (~lines 306, 317, 375, 384, 415).
- Longer-term (optional, larger): make `Frame`'s three byte vectors fixed `std::array` (sizes are
  compile-time 24/32) to remove per-frame heap traffic entirely.
- **Accept:** no section/frame deep-copy at any stage boundary; profile shows reduced allocations.

### 3.2 — P-3: retire the per-frame Audio copy and dead split accessors ⚠️
- The per-sample allocations are gone, but `dec_audiocorrection.cpp` still makes a per-frame `data()`
  copy, and `audio.cpp`'s `dataLeft()/dataRight()/errorDataLeft()/errorDataRight()` (now unused on the
  decode path) still allocate per call.
- **Fix:** give `Audio` `const&` interleaved accessors indexed as `data[2*i]/data[2*i+1]`; delete the
  unused split accessors (git preserves history).

### 3.3 — P-6: scratch arrays + `std::move` in deinterleave ⚠️
- RS codecs are now built once (done). Remaining: `reedsolomon.cpp` still allocates per-call
  `tmpData`/`erasures`/`position`; `interleave.cpp` builds three output vectors per frame and
  **copy-assigns** them back (`inputData = outputData;`).
- **Fix:** fixed `std::array<uint8_t, 32/28/24>` scratch members; `std::move` the outputs at the end
  of `deinterleave`.

### 3.4 — P-7: buffer-consume via `erase`; drop writer per-frame copies ⚠️
- `dec_tvaluestochannel.cpp` consumes its buffer via `m_internalBuffer = vector(begin+n, end)`
  (alloc + tail copy) in ~8 sites — replace with `erase(begin, begin+n)` (single memmove); append
  directly in `pushFrame` and delete the one-element `m_inputBuffer` queue.
- `writer_wav.cpp:52` / `writer_raw.cpp:58` still do `Audio audio = audioSection.frame(index);`
  (copies a const-ref return) — take `const Audio&` and assemble one section-sized write buffer.

---

## Phase 4 — Spec completeness: lead-in TOC (Minor spec gap) ✅

### 4.1 — Q-6: decode lead-in POINT/PMIN/PSEC/PFRAME as TOC — **Done**
- **Files:** `efm-lib/subcode.cpp`, `efm-lib/section_metadata.h`,
  `decoders/dec_f2sectioncorrection.{h,cpp}`, `efm_processor.cpp` (report Part B).
- **Problem:** in lead-in, byte 2 is POINT and bytes 7–9 are PMIN/PSEC/PFRAME (TOC entries incl.
  A0/A1/A2); previously they were stored as absolute time, which repeats/jumps between TOC items so
  `waitForInputToSettle` could never settle inside a lead-in. The decoder worked only because captures
  start in the program area.
- **Fix (implemented):**
  - `subcode.cpp` now special-cases the lead-in Q-mode 1/4 branch: it decodes POINT (kept raw so
    A0/A1/A2 are distinguishable) and PMIN/PSEC/PFRAME into new `SectionMetadata` TOC fields
    (`setLeadinToc` / `point()`/`pMin()`/`pSec()`/`pFrame()`), and uses the running MIN/SEC/FRAME
    (bytes 3–5) as both section and absolute time so the lead-in has a monotonic timeline.
  - `F2SectionCorrection` harvests the TOC in `pushSection` (`recordTocEntry`) before the timeline
    logic runs, then **skips lead-in sections** in `waitForInputToSettle`/`waitingForSection` so the
    timeline settles on the first contiguous run of program-area/lead-out sections (unchanged behaviour
    for program-area captures; no lead-in silence is emitted). A0/A1/A2 populate first-track /
    last-track / disc-type / lead-out-start; POINT 01-99 build the per-track start table (de-duplicated).
  - TOC exposed via accessors (`hasToc()`, `tocFirstTrack()`, `tocLastTrack()`, `tocDiscType()`,
    `tocLeadOutStart()`, `tocTrackNumbers()`, `tocTrackStartTimes()`, `leadinSections()`) and rendered
    in report Part B and `showStatistics`.
- **Accept:** verified with a standalone harness (real `Subcode` + `F2SectionCorrection`): a capture
  beginning in the lead-in settles on the program area and decodes, emits program-area sections only,
  and exposes the correct TOC (first/last track, lead-out start, per-track starts); a program-only
  capture still settles with `hasToc() == false`.

---

## Phase 5 — Cleanup & hygiene (Minor / Info) ✅

Low-risk tidy-ups; batched into one commit. **All items done** (2026-07-15), including the optional
5.2/5.3 hardening. `orc-efm-decode` builds clean (warnings-as-errors + clang-tidy) and all 50 EFM
stage/contract tests pass.

### 5.1 — P-11: dead code and build hygiene remnants — **Done**
Previously done: `-w` → targeted suppressions, `SPDLOG_ACTIVE_LEVEL`, deleted `file_io.h`/`section_io.h`,
removed several unused members and the `Data24::setData` no-op loop. Completed here:
- **`-O2` in Debug made deliberate:** `common/efm-decode/CMakeLists.txt` documents the perf rationale
  and adds a `-DORC_EFM_DEBUG_UNOPTIMISED=ON` cache option that drops the forced `-O2` for a genuinely
  debuggable Debug build.
- **Deleted** the commented-out `QDataStream` blocks in `section.cpp`, `frame.cpp`,
  `section_metadata.cpp`.
- **Deleted** the unused declarations: `F3FrameToF2Section::m_inputBuffer`,
  `TvaluesToChannel::m_frameData` / `m_tvalues`, the `Tvalues` class (`tvalues.h`/`tvalues.cpp` removed),
  `F2Section::showData`, `Sector::showData`.
- **Doc nits:** rewrote the misleading "interleaved" padding comment (it inserts a contiguous block)
  and fixed the wrong-function log names in `SectionMetadata::setTrackNumber`. (The `efm.cpp` "10-bit"
  and `reedsolomon.h` ezpwd-label nits were already correct in the tree.)
- **Magic numbers extracted:** new `efm-lib/efm_constants.h` (namespace `efm`) defines 588, the
  550/600 acceptance band, 98, 2352, `0x0B`, 382 and the 1000-byte false-positive threshold with
  IEC 60908 / ECMA-130 references. Inline literals across the decoders (and the duplicated per-file
  `kFramesPerSection = 98` constants) now route through it.

### 5.2 — P-12: RS codec duplication / thread-safety — **Done**
The byte-identical duplicate configs were collapsed: `c1rs`/`c2rs` → one `CircRS` (RS(255,251),
4 parity) serving both the (32,28) C1 and (28,24) C2 shortened codes; `qrs`/`prs` → one `RspcRS`
(RS(255,253), 2 parity) serving both the Q(45,43) and P(26,24) codes. The deliberate shared-file-scope
single-instance design (documented thread-safety) is retained.

### 5.3 — Q-10(b): CRC-bit trial-flip in `repairData` — **Done**
`Subcode::repairData` now trial-flips all 96 Q-channel bits (was 0–79), so a single-bit error that
landed in the 16 stored-CRC bits (~17 % of single-bit errors) is now recoverable. The downstream
±10-frame plausibility check still guards against false repairs.

---

## Suggested sequencing

1. **Phase 1** in one commit — small, correctness-critical, each independently testable.
2. **Phase 2** next — the only remaining Major defect; verify peak-memory bound on a large capture.
3. **Phase 3** as a performance sweep — measure allocations before/after.
4. **Phase 4** scheduled separately (largest, needs lead-in test material).
5. **Phase 5** batched cleanup, any time.

Items intentionally **not** scheduled (accepted as-is by the review): C-4 legacy 4-channel mapping
(kept deliberately), P-9 single-threaded pipeline (informational), and the `Q-10(b)` CRC repair
unless yield warrants it.
