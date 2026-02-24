# EFM Decoder Sink – Phase 0 Implementation Summary

## Status: ✓ COMPLETE

Phase 0 — Source acquisition and pinning — has been successfully implemented and the decode-orc project builds successfully offline from in-tree decoder sources.

---

## Deliverables

### 1. One-time Bootstrap Import ✓
- **Source:** https://github.com/simoninns/efm-decoder
- **Pinned Commit:** `07eb89f9f58f1ef5432fb9cc68a5873f37a45b80`
- **Bootstrap Date:** 2026-02-24
- **Artifact Location:** `orc/core/stages/efm_decoder/pipeline/` and `orc/core/stages/efm_decoder/config/`

The standalone decoder source has been imported into the repository tree and organized under stage-local pipeline/config boundaries. All imported code is now part of decode-orc's primary implementation.

### 2. Provenance Recording ✓
- **Provenance Document:** [EFM_DECODER_PROVENANCE.md](./EFM_DECODER_PROVENANCE.md)
  - Records origin repository and exact bootstrap commit
  - Documents integration boundary and adapter pattern
  - Clarifies ownership transition (decode-orc becomes canonical)
  - Describes maintenance assumptions going forward

### 3. In-Tree Stage Structure ✓
- **Stage Location:** `orc/core/stages/efm_decoder/`
- **Folder Layout:**
  - `pipeline/` – Imported decoder pipeline implementation and stage internals
  - `config/` – Parameter mapping and validation (Phase 3)
  - `report/` – Statistics and report formatting (Phase 5)
  - `efm_decoder_sink_stage.h/.cpp` – Main stage class (Phase 0)

### 4. Build and Registry Wiring ✓
- **CMakeLists.txt** updated to compile new stage source
- **stage_init.cpp** updated with force-link declaration and call
- **Stage Registry:** `EFMDecoderSinkStage` now discoverable and instantiable
- **Class Hierarchy:** Properly inherits from `DAGStage`, `ParameterizedStage`, `TriggerableStage`

### 5. Temporary Bootstrap Cleanup ✓
- `/tmp/efm-decoder-bootstrap` removed
- No `.gitmodules` entry added (temporary bootstrap only, not persistent submodule)
- Repository remains self-contained with no external live dependencies

### 6. Build Verification ✓
- Project builds successfully with `-DCMAKE_BUILD_TYPE=Debug`
- No compilation errors
- No external network dependencies (all code in-tree)
- Executable binaries (`orc-cli`, `orc-gui`) link cleanly

---

## Exit Criteria Met

| Criterion | Status | Evidence |
|-----------|--------|----------|
| Team can build offline from in-tree sources | ✓ | Build completes successfully; decoder code is in-tree under `pipeline/` + `config/` within stage tree |
| Imported source provenance is explicit and auditable | ✓ | EFM_DECODER_PROVENANCE.md records commit `07eb89f9f5...` and integration notes |
| No planned ongoing upstream sync | ✓ | Maintenance notes document ownership transfer; decoder is canonical in decode-orc |
| No residual `efm-decoder` project copy | ✓ | Bootstrap directory removed; only in-tree stage remains |
| Stage discoverable from registry | ✓ | Force-link wiring in place; `EFMDecoderSinkStage` instantiable |
| Project builds cleanly | ✓ | All compilation and linking succeed |

---

## Phase Scope

Phase 0 establishes the foundational in-tree structure and provenance only. The following will be implemented in later phases:

- **Phase 1** – Parameter schema finalization and design freeze
- **Phase 2** – Full stage node metadata and test harness
- **Phase 3** – Parameter translation (CLI semantics to stage config)
- **Phase 4** – Decode orchestration (VFR input → decoder pipeline → output files)
- **Phase 5** – Reporting and diagnostics integration
- **Phase 6** – Validation matrix and parity checks
- **Phase 7** – User-facing documentation

---

## Key Implementation Files

**Source Code:**
- `orc/core/stages/efm_decoder/efm_decoder_sink_stage.h` – Stage header (Phase 0 skeleton)
- `orc/core/stages/efm_decoder/efm_decoder_sink_stage.cpp` – Stage implementation (Phase 0 skeleton)
- `orc/core/stages/efm_decoder/pipeline/` – Imported decoder pipeline code (stage-local)
- `orc/core/stages/efm_decoder/config/decoder_config.h` – Imported decoder configuration contract

**Build Configuration:**
- `orc/core/CMakeLists.txt` – Build wiring (updated for efm_decoder)
- `orc/core/stage_init.cpp` – Registry force-link (updated with EFMDecoderSinkStage)

**Documentation:**
- `docs/EFM_DECODER_PROVENANCE.md` – Source attribution and maintenance notes
- `docs/EFM_DECODER_SINK_PLAN.md` – Full implementation plan (original)

---

## Build Verification

```sh
cd /home/sdi/Coding/decode-orc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Both `build/bin/orc-cli` and `build/bin/orc-gui` are available and link cleanly.

---

**SPDX-License-Identifier:** GPL-3.0-or-later  
**SPDX-FileCopyrightText:** 2025-2026 Simon Inns  
**Implementation Date:** 2026-02-24
