# EFM Decoder – orc-cli Golden Hash Baselines

These are the reference SHA256 hashes for the two canonical EFM decode tests, produced
by `orc-cli` after the full Phase 1–9 integration.  Any regression in the EFM decode
pipeline should cause at least one of these hashes to change.

---

## Test 1 – Audio decode (`roger_rabbit`)

### Input

| File | Size (bytes) | SHA256 |
|---|---|---|
| `roger_rabbit.efm` | 1,302,496,460 | `95658ab7e6558e78a716c4d086e7617550c254439a1e23a4d3cd5fd557939be0` |

### Project file (`test-projects/roger_rabbit.orcprj`)

```yaml
# ORC Project File
# Version: 1.0

project:
  name: roger_rabbit
  version: 1.0
  video_format: PAL
  source_format: Composite
dag:
  nodes:
    - id: 1
      stage: PAL_Comp_Source
      node_type: SOURCE
      display_name: PAL Composite Source
      user_label: PAL Composite Source
      x: 50
      y: 50
      parameters:
        db_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/audio/roger_rabbit.tbc.db
        efm_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/audio/roger_rabbit.efm
        input_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/audio/roger_rabbit.tbc
        pcm_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/audio/roger_rabbit.pcm
    - id: 3
      stage: EFMSink
      node_type: SINK
      display_name: EFM Decoder Sink
      user_label: EFM Decoder Sink
      x: 303
      y: 46
      parameters:
        audacity_labels:
          type: bool
          value: false
        decode_mode:
          type: string
          value: audio
        no_audio_concealment:
          type: bool
          value: false
        no_timecodes:
          type: bool
          value: false
        no_wav_header:
          type: bool
          value: false
        output_metadata:
          type: bool
          value: false
        output_path:
          type: string
          value: /home/sdi/tmp/efm/roger_rabbit.wav
        report:
          type: bool
          value: true
        zero_pad:
          type: bool
          value: false
  edges:
    - from: 1
```

### Command

```bash
orc-cli test-projects/roger_rabbit.orcprj --process --log-level warn
```

### Output

| File | Size (bytes) | SHA256 |
|---|---|---|
| `roger_rabbit.wav` | 258,800,012 | `069a092453be6058e5a047984407ca37c3204e9f7a89a386135c59283fc92fe6` |

### Quick verify

```bash
sha256sum /home/sdi/tmp/efm/roger_rabbit.wav
# Expected: 069a092453be6058e5a047984407ca37c3204e9f7a89a386135c59283fc92fe6
```

---

## Test 2 – Data decode (`DS2_comS1`)

### Input

| File | Size (bytes) | SHA256 |
|---|---|---|
| `DS2_comS1.efm` | 2,116,591,505 | `a841ef2350b7ff2923ab1184bdc711b50fb65ae8552d9ac6d6ecf7ab495759ce` |

### Project file (`test-projects/DS2_comS1.orcprj`)

```yaml
# ORC Project File
# Version: 1.0

project:
  name: DS2_comS1
  version: 1.0
  video_format: PAL
  source_format: Composite
dag:
  nodes:
    - id: 1
      stage: PAL_Comp_Source
      node_type: SOURCE
      display_name: PAL Composite Source
      user_label: PAL Composite Source
      x: 50
      y: 50
      parameters:
        efm_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/data/DS2_comS1.efm
        input_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/data/DS2_comS1.tbc
        pcm_path:
          type: string
          value: /home/sdi/Coding/efm-decoder/tests/data/DS2_comS1.pcm
    - id: 3
      stage: EFMSink
      node_type: SINK
      display_name: EFM Decoder Sink
      user_label: EFM Decoder Sink
      x: 275
      y: 50
      parameters:
        audacity_labels:
          type: bool
          value: false
        decode_mode:
          type: string
          value: data
        no_audio_concealment:
          type: bool
          value: false
        no_timecodes:
          type: bool
          value: false
        no_wav_header:
          type: bool
          value: false
        output_metadata:
          type: bool
          value: true
        output_path:
          type: string
          value: /home/sdi/tmp/efm/ds2_data.bin
        report:
          type: bool
          value: true
        zero_pad:
          type: bool
          value: false
  edges:
    - from: 1
```

### Command

```bash
orc-cli test-projects/DS2_comS1.orcprj --process --log-level warn
```

### Output

| File | Size (bytes) | SHA256 |
|---|---|---|
| `ds2_data.bin`     | 276,717,568 | `f3a09dd8eb886afdcfaa3ffb56e85c2817137e1d689da250555e37410945997b` |
| `ds2_data.bin.bsm` |      30,408 | `ec0802421acd415b403ecdce59ab332356c8b328f234a4f8dd597e0a5d0db5e7` |

The `.bsm` (bad sector map) file is written automatically when `output_metadata: true`.
It records which sectors required error-correction padding; its hash is therefore part of
the golden baseline.

### Quick verify

```bash
sha256sum /home/sdi/tmp/efm/ds2_data.bin /home/sdi/tmp/efm/ds2_data.bin.bsm
# Expected:
# f3a09dd8eb886afdcfaa3ffb56e85c2817137e1d689da250555e37410945997b  /home/sdi/tmp/efm/ds2_data.bin
# ec0802421acd415b403ecdce59ab332356c8b328f234a4f8dd597e0a5d0db5e7  /home/sdi/tmp/efm/ds2_data.bin.bsm
```

---

## Run both tests and verify

```bash
mkdir -p /home/sdi/tmp/efm

orc-cli test-projects/roger_rabbit.orcprj --process --log-level warn
orc-cli test-projects/DS2_comS1.orcprj   --process --log-level warn

sha256sum /home/sdi/tmp/efm/roger_rabbit.wav /home/sdi/tmp/efm/ds2_data.bin /home/sdi/tmp/efm/ds2_data.bin.bsm
```

Expected output:

```
069a092453be6058e5a047984407ca37c3204e9f7a89a386135c59283fc92fe6  /home/sdi/tmp/efm/roger_rabbit.wav
f3a09dd8eb886afdcfaa3ffb56e85c2817137e1d689da250555e37410945997b  /home/sdi/tmp/efm/ds2_data.bin
ec0802421acd415b403ecdce59ab332356c8b328f234a4f8dd597e0a5d0db5e7  /home/sdi/tmp/efm/ds2_data.bin.bsm
```

---

## Notes

- These hashes were established on 2026-03-10 after the Phase 1–9 EFM decoder integration.
- The data hash (`f3a09dd8...`) was cross-validated against the standalone C++17 `efm-decoder`
  binary compiled from the same source tree; both produce byte-for-byte identical output.
- The audio hash (`069a0924...`) was cross-validated against the Qt `ld-efm-decoder`
  pipeline (roger_rabbit.efm processes cleanly through the Qt tools without the
  Qt5→Qt6 assertion issue that affects data-disc EFM).
- `output_metadata: true` in the data project causes `orc-cli` to write a companion
  `.bin.bsm` bad-sector-map file alongside the binary output.  The `.bsm` hash is
  included in the golden baseline and was cross-validated against the Qt
  `ld-efm-decoder` reference run (`DS2_comS1.bin.bsm`).

---

## Performance history

Standalone `ld-decode-tools` baseline (reference; sum of `real` wall times across all stages,
documented in full in `docs/efm-golden-sample-baseline.md`):

| Test | Baseline wall time |
|---|---|
| `roger_rabbit` (audio) | ~1m26.7s |
| `DS2_comS1` (data)     | ~1m44.97s |

### Commit `8fad4bf` — initial orc-cli integration (2026-03-10)

No EFM-specific optimisations; STL template code compiled at `-O0`.

| Test | `real` | `user` | `sys` | vs baseline |
|---|---|---|---|---|
| `roger_rabbit` (audio) | 7m02.271s | 6m59.511s | 0m01.110s | ~4.9× slower |
| `DS2_comS1` (data)     | 8m20.223s | 8m16.320s | 0m01.513s | ~4.8× slower |

### Commit `0a72ff1` — EFM performance optimisations (2026-03-11)

Three fixes applied (see `docs/performance-testing.md` for root-cause analysis):

1. EFM decoder vendored sources recompiled at `-O2` even in Debug builds
   (`set_source_files_properties … COMPILE_FLAGS "-w -O2"`).
2. `DelayLine::push()` O(N) `erase(begin())` FIFO replaced with an O(1) ring buffer.
3. `SPDLOG_ACTIVE_LEVEL` lowered from unconditional `TRACE` to `INFO` (Debug) /
   `WARN` (Release) to eliminate runtime `should_log()` overhead in hot paths.

Hashes confirmed identical to baseline — no regressions.

| Test | `real` | `user` | `sys` | vs baseline | vs `8fad4bf` |
|---|---|---|---|---|---|
| `roger_rabbit` (audio) | 1m19.986s | 1m18.687s | 0m00.916s | ~0.92× (faster) | ~5.3× faster |
| `DS2_comS1` (data)     | 1m40.674s | 1m38.704s | 0m01.383s | ~0.97× (faster) | ~5.0× faster |
