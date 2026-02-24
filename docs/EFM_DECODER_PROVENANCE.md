# EFM Decoder – Source Provenance and Integration Notes

## Bootstrap Source

This decoder implementation was bootstrapped from the standalone `efm-decoder` project:

- **Repository:** https://github.com/simoninns/efm-decoder
- **Bootstrap Commit:** `07eb89f9f58f1ef5432fb9cc68a5873f37a45b80`
- **Bootstrap Date:** 2026-02-24
- **Bring-in Method:** One-time in-tree import, reorganized under stage pipeline/config boundaries

## Decoder Code Location

All imported decoder code from the standalone project is organized under the stage folders:
- `orc/core/stages/efm_decoder/pipeline/core/` – Core decoder pipeline stages (sync, correction, error handling, audio synthesis, etc.)
- `orc/core/stages/efm_decoder/pipeline/stages/` – Mode-specific decoder stages (shared/audio/data/bridge)
- `orc/core/stages/efm_decoder/config/decoder_config.h` – Decoder configuration structures
- `orc/core/stages/efm_decoder/pipeline/unified_decoder.h` / `pipeline/unified_decoder.cpp` – Unified API wrapper

## Integration Boundary

The Orc EFM Decoder Sink stage (`orc/core/stages/efm_decoder/efm_decoder_sink_stage.*`) provides:
1. **Stage interface** – VFR input → decoder pipeline wiring, parameter translation, trigger orchestration
2. **Config mapper** – Maps Orc parameter semantics to decoder configuration (`config/` subdirectory)
3. **Report aggregation** – Collects decode stats and formats for output/logging (`report/` subdirectory)
4. **I/O handling** – Managed within stage/pipeline implementation where needed (temporary buffers, stream control)

The imported decoder code is consumed as an in-process library; stage code wraps it via clean adapter boundaries (defined in `config/` and `pipeline/` subdirectories).

## Additional Dependency Provenance (ezpwd)

The integrated decoder uses EZPWD Reed-Solomon headers (`<ezpwd/rs_base>` and `<ezpwd/rs>`).

- **Dependency Repository:** https://github.com/pjkundert/ezpwd-reed-solomon
- **Pinned Commit:** `62a490c`
- **Integration Model:** Header-only include dependency (no linked binary library)
- **Nix Flake Pin:** `flake.nix` input `ezpwd` with a derived header package (`ezpwdHeaders`)

Build integration contract:

1. Nix builds pass `-DEZPWD_INCLUDE_DIR=<path>` from the pinned header package.
2. Core CMake accepts `EZPWD_INCLUDE_DIR` and adds it to `orc-core` include paths.
3. `nix develop` exports `EZPWD_INCLUDE_DIR` so local `cmake` configure can consume the same header path.
4. If `EZPWD_INCLUDE_DIR` is not provided, CMake attempts local/system path discovery.
5. If discovery fails, configure stops with a clear error explaining how to set `EZPWD_INCLUDE_DIR`.

This keeps the dependency reproducible under Nix while remaining configurable for non-Nix development environments.

## Ownership and Maintenance

**Assumption:** Once integrated, the decoder inside decode-orc becomes the **primary/canonical** implementation.

- **Future decoder updates** occur in decode-orc; upstream efm-decoder is no longer the authority.
- **Intentional divergences** from the bootstrap source (e.g., bug fixes, Orc integration refinements) are documented in commit messages and tracked in git history.
- **No planned ongoing upstream sync** – this is a forked integration, not a live external dependency.

If critical bug fixes or feature additions emerge in the standalone project after this bootstrap, they must be manually evaluated and ported to decode-orc if needed. The team owns the decoder code going forward.

## Verifying Bootstrap Completion

To confirm the bootstrap is complete and no temporary wiring remains:

1. **Verify no git submodule exists:**
   ```sh
   cd /path/to/decode-orc
   git config --file .gitmodules --name-only --get-regexp path | grep efm-decoder || echo "✓ No efm-decoder submodule"
   ```

2. **Build and test offline:**
   ```sh
   cd build
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   cmake --build . -j
   ```

3. **Confirm stage is discoverable:**
   - Launch the GUI (`orc-gui`) and verify `EFMDecoderSink` appears in the stage registry/DAG editor.

---

**SPDX-License-Identifier:** GPL-3.0-or-later  
**SPDX-FileCopyrightText:** 2025-2026 Simon Inns
