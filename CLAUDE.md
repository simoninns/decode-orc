# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Decode-Orc is a cross-platform C++/Qt6 orchestration framework for LaserDisc and tape decoding workflows. It provides GUI (`orc-gui`) and CLI (`orc-cli`) interfaces sharing a common MVP-architected core. Licensed under GPLv3 — all dependencies must be compatible.

## Build Commands

```bash
# Enter Nix dev environment (recommended — all deps pre-configured)
nix develop

# Configure with tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON

# Build
cmake --build build -j

# Run all tests (unit + MVP architecture check)
ctest --test-dir build --output-on-failure

# Run only unit tests (skip MVP check)
ctest --test-dir build -E MVPArchitectureCheck --output-on-failure

# Check MVP architecture boundaries only
ctest --test-dir build -R MVPArchitectureCheck --output-on-failure

# Run a specific test by name pattern
ctest --test-dir build -R "test_name_pattern" --output-on-failure

# Run tests by label (sources, transforms, sinks, contracts)
ctest --test-dir build -L unit -L sources --output-on-failure

# Clean rebuild
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON && cmake --build build -j
```

Key CMake flags: `BUILD_GUI=ON` (default), `BUILD_UNIT_TESTS=ON` (for dev), `CMAKE_BUILD_TYPE=Debug|Release`.

## Architecture (MVP — Strictly Enforced)

The project enforces MVP (Model-View-Presenter) with automated boundary checks in CI:

| Layer | Location | Can depend on |
|-------|----------|---------------|
| Common (utilities) | `orc/common/` | Nothing |
| Core (business logic) | `orc/core/` | common |
| View Types (DTOs) | `orc/view-types/` | common, core (read-only) |
| Presenters | `orc/presenters/` | common, core, view-types |
| GUI | `orc/gui/` | presenters, view-types, common, Qt6 |
| CLI | `orc/cli/` | presenters, view-types, common |
| Tests | `orc-unit-tests/` | any (for testing) |

**Hard rules:**
- `orc/core` must NEVER include GUI headers, presenters, or view-types
- `orc/presenters` must NEVER include `orc/gui` or `orc/cli`
- `orc/gui` and `orc/cli` must NEVER access `orc/core` directly — only through presenters

Violations are caught by `cmake/check_mvp_architecture.sh`. Always run `ctest -R MVPArchitectureCheck` before submitting PRs.

## Processing Pipeline Stages

Core processing lives in `orc/core/stages/` organized by type:
- **Source stages**: Input formats (NTSC/PAL composite, Y+C)
- **Transform stages**: Processing (dropout correction, field operations, stacking)
- **Sink stages**: Output formats (audio, chroma, Daphne VBI, EFM, etc.)

Each stage has corresponding tests under `orc-unit-tests/core/stages/<stage_id>/`.

## Testing Conventions

- **~80% unit test coverage target**; unit tests mock all dependencies (no filesystem/network/clock)
- Use Google Test `MOCK_METHOD` on **interfaces**, not classes
- Tests registered in `orc-unit-tests/core/CMakeLists.txt` with `gtest_discover_tests` and labels (`unit`, `sources`, `transforms`, `sinks`, `contracts`, `mvp`)
- Dependency injection via constructor injection; use Abstract Factory pattern for dynamic object creation

**New stage requirements**: every new stage must ship with a full unit test suite covering family-specific behavior (see `docs/stage-test-expectations.md` and `.github/copilot-instructions.md` for detailed expectations).

## Adding Features

1. **Business logic**: implement in `orc/core/`, tests in `orc-unit-tests/core/`
2. **Presentation**: add presenter in `orc/presenters/`, view types in `orc/view-types/` if needed
3. **UI**: add to `orc/gui/` or `orc/cli/` — consume presenters only, never core

## Validation Before PRs

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
ctest --test-dir build -R MVPArchitectureCheck --output-on-failure
```

## Key References

- `BUILD.md` — detailed build instructions for all platforms
- `TESTING.md` — testing philosophy, mocking patterns, examples
- `.github/copilot-instructions.md` — comprehensive AI agent guidance (stage test expectations, contribution hygiene, CI/CD details)
- `docs/stage-test-expectations.md` — required coverage for new stages
