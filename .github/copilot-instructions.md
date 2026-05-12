# Copilot Instructions for Decode-Orc

## Repository Overview

**Decode-Orc** is a cross-platform orchestration framework for LaserDisc and tape decoding workflows. It provides both a GUI (`orc-gui`) and CLI (`orc-cli`) interface, sharing a common MVP-architected core (`orc/core`, `orc/presenters`, `orc/view-types`). 

- **Type:** C++ / Qt6 cross-platform application
- **Build system:** CMake 3.20+ with vcpkg dependencies
- **Reproducible builds:** Nix (recommended) with flake.nix
- **Target platforms:** Linux (Flatpak), macOS (DMG), Windows (MSI)

## Architecture & Constraints

The project enforces **MVP (Model-View-Presenter) pattern** to keep layers decoupled. Do not bypass this:

- **Model/Core:** `orc/core/` — business logic, isolated from UI
- **Presenters:** `orc/presenters/` — translates core output to view models
- **View Types:** `orc/view-types/` — shared DTO-like structures
- **View:** `orc/gui/` and `orc/cli/` — consume presenters, never touch core directly

Run `ctest -R MVPArchitectureCheck` to validate boundaries before submitting PRs.

## SDK-Only Stage/Plugin Rule

All stage implementations (Decode-Orc supplied and third-party) must use only the public plugin SDK contract.

- Do not include private host headers from `orc/core`, `orc/gui`, `orc/cli`, or `orc/presenters` in plugin-facing stage code.
- Do not link plugin targets against private host internals outside approved SDK/plugin interfaces.
- Do not add compatibility fallbacks or include-path workarounds that depend on in-tree private headers being present.
- If an SDK capability is missing, expand the SDK first rather than bypassing it.

Plugin architecture and SDK documentation are published in `docs/technical/plugin-architecture.md` and `docs/technical/plugin-sdk.md`.

**SDK documentation must be kept in sync with the implementation.** When any of the following change, update the relevant doc file in the same PR:

| Change | Doc to update |
|--------|---------------|
| `kStagePluginHostAbiVersion` or `kStagePluginApiVersion` bumped | Both files — update version tables and compatibility sections |
| New or removed public SDK header (`orc/sdk/include/orc/plugin/`) | `plugin-sdk.md` — SDK Headers table |
| `StagePluginDescriptor`, entrypoint signatures, or callback contract changed | `plugin-architecture.md` — Compatibility Gating section |
| Registry YAML schema fields added or removed | `plugin-architecture.md` — Plugin Registry table |
| Artifact naming convention changed | Both files |
| `IStageServices` interface methods added or removed | `plugin-sdk.md` — Host services section |
| `StageToolDescriptor` / `AnalysisToolDescriptor` contract changed | `plugin-sdk.md` — Optional: Stage tools section |
| Plugin cache path or download behaviour changed | `plugin-architecture.md` — Plugin Registry section |
| `orc_add_stage_plugin()` macro signature changed | `plugin-sdk.md` — CMake Integration section |

## SDK-Only Enforcement Gates (Active)

**SDK-only compliance is enforced with hard-fail gates in CI/CD.**

Do NOT attempt to work around these gates; they are non-negotiable:

### Required Validation

Before opening a PR that adds or modifies stage plugins:

```bash
# Build with tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON
cmake --build build --parallel

# Run SDK enforcement gates (REQUIRED - must pass)
ctest --test-dir build -L sdk --output-on-failure

# Run runtime architecture validation
ctest --test-dir build -R "StagePluginLoader" --output-on-failure
```

If either hard gate fails (`PluginPrivateIncludeScan` or `PluginPrivateLinkScan`), the build is rejected. Fix all violations before submitting PR.

### Gates in CI/CD

- **build-and-test.yml:** Requires all `-L sdk` tests to pass before packaging workflows
- **All platforms:** macOS, Windows, and Flatpak packaging workflows require build-and-test to succeed
- **Pre-commit locally:** Run gates locally first to avoid failed CI runs

## Licensing & Legal Requirements

**Decode-Orc is licensed under GPLv3.** All dependencies and contributions must be compatible with GPLv3:

- **Permitted licenses:** GPLv3, GPLv2, LGPL, BSD, MIT, Apache 2.0, ISC, and similar permissive licenses
- **Incompatible licenses:** AGPL (stronger copyleft), proprietary/closed-source, SSPL
- **Check before adding:** Always verify a new dependency's license in its repository or LICENSE file before proposing a PR

**For contributors:**
- When adding a new dependency (via vcpkg.json or flake.nix), document its license
- If you're unsure about license compatibility, ask in the issue or PR
- vcpkg.json and flake.nix changes will be reviewed for license compliance during CI/CD

**License file location:** See [LICENSE](../LICENSE) in the repository root (GPLv3).

## Source Code Structure

**Repository root directory layout:**

```
decode-orc/
├── orc/                           # Main project directory (CMake target root)
│   ├── common/                    # Shared utilities (logging, file I/O, exceptions)
│   ├── core/                      # Core business logic (MVP Model layer)
│   │   ├── stages/                # Processing pipeline stages
│   │   ├── metadata/              # Metadata handling
│   │   └── [business logic]
│   ├── view-types/                # Shared DTO structures (MVP shared layer)
│   │   └── [data transfer objects used by presenters & UI]
│   ├── presenters/                # Presenter layer (MVP Presenter)
│   │   └── [translates core output to view models]
│   ├── cli/                       # Command-line interface
│   │   └── main.cpp
│   └── gui/                       # Qt6 graphical interface (optional, BUILD_GUI=ON)
│       └── [Qt widgets & dialogs]
├── orc-tests/                     # Unified test tree (compiled when test flags are enabled)
│   ├── core/
│   │   └── unit/                  # Unit tests for core module
│   └── gui/
│       └── unit/                  # GUI unit tests
├── cmake/                         # CMake build utilities
│   ├── check_mvp_architecture.sh  # MVP boundary validation script
│   ├── MVPEnforcement.cmake       # MVP constraint macros
│   └── [other build helpers]
├── .github/                       # GitHub-specific configuration
│   ├── copilot-instructions.md    # AI agent guidance (this file)
│   ├── workflows/                 # CI/CD pipelines
│   └── ISSUE_TEMPLATE/
├── external/                      # Third-party dependencies
│   └── ld-decode-tools/           # Legacy ld-decode reference (checked in; available locally)
├── docs/                          # Documentation
├── assets/                        # Images, logos
├── CMakeLists.txt                 # Top-level CMake config
├── CMakePresets.json              # CMake build presets for all platforms
├── flake.nix                      # Nix reproducible build configuration
├── vcpkg.json                     # Dependency manifest (vcpkg)
├── BUILD.md                       # Build instructions (detailed)
├── TESTING.md                     # Testing strategy & patterns
├── CONTRIBUTING.md                # Contribution guidelines
└── README.md                      # Project overview
```

**Important dependency handling notes:**

**Nix-based workflow (recommended):**
- `flake.nix` defines all external dependencies as flake inputs
  - `qtnodes` (Qt node editor) is fetched from GitHub during `nix develop` / `nix build`
  - `ezpwd-reed-solomon` headers are fetched from GitHub during `nix develop` / `nix build`
- No git submodules required; Nix handles everything automatically
- Simply run `nix develop` and dependencies are available
- When already working inside `nix develop`, if a temporary extra tool is needed to complete the task, prefer `nix shell` to pull that tool into the active workflow instead of installing it through the host system or assuming it already exists

**Non-Nix (CMake/vcpkg) workflow:**
- Dependencies are managed via `vcpkg.json` (manifest mode)
- External dependencies may need manual setup (see BUILD.md for details)
- Git submodules are NOT used in this project's primary workflow

| Module | Purpose | Dependencies | Can depend on |
|--------|---------|--------------|---------------|
| `orc/common` | Utilities (logging, file I/O) | None | — |
| `orc/core` | Business logic; no UI knowledge | common | common |
| `orc/view-types` | DTOs for presentation layer | — | common, core (read-only output types) |
| `orc/presenters` | Translates core → view models | core, view-types | common, core, view-types |
| `orc/gui` | Qt6 UI; consumes presenters | presenters, view-types, Qt6 | presenters, view-types, common |
| `orc/cli` | CLI; consumes presenters | presenters, view-types | presenters, view-types, common |
| `orc-tests` | Unit tests (mocked dependencies) | gtest | any (for testing) |

**MVP Enforcement Rules:**

- `orc/core` must **never** include GUI headers (Qt, presenters, or view-types)
- `orc/presenters` must **never** include `orc/gui` or `orc/cli` headers
- `orc/gui` and `orc/cli` must **never** directly access `orc/core` (use `orc/presenters` instead)
- Cross-layer includes are detected by `cmake/check_mvp_architecture.sh` and enforced in CI/CD

**Adding new features:**

1. **Business logic:** Add implementation to `orc/core/`; add corresponding unit tests to `orc-tests/core/unit/`
2. **Presentation layer:** Add presenter in `orc/presenters/` to translate core output; add view types to `orc/view-types/` if needed
3. **UI layer:** Add GUI dialogs/widgets to `orc/gui/` or CLI commands to `orc/cli/`; both consume presenters, never core directly

**Key configuration files:**

- **`CMakeLists.txt` (root):** Top-level project setup, MVP enforcement, subdir inclusion (`-DBUILD_UNIT_TESTS`, `-DBUILD_GUI`)
- **`orc/CMakeLists.txt`:** Main project config; defines build options; includes all subdirectories
- **`CMakePresets.json`:** Build presets for all platforms (linux-gui-debug, macos-gui-debug, windows-gui-release, etc.)
- **`flake.nix`:** Nix dev environment and reproducible build configuration
- **`vcpkg.json`:** Dependency manifest (spdlog, Qt6, FFmpeg, sqlite, yaml-cpp, fftw, gtest)

**Where to find things:**

- **Tests:** `orc-tests/` (organized by source module, for example `core/unit/` and `gui/unit/`)
- **Headers:** `orc/<module>/` (public headers in root; internal in subdirs)
- **Build outputs:** `build/bin/` (orc-gui, orc-cli); `build/lib/` (libraries)
- **Generated files:** `build/generated/version.h` (auto-generated version info)

## Build & Test (Nix-First Recommended)

For complete build instructions including Nix setup, CMake configuration, dependency management, and troubleshooting, see [BUILD.md](../BUILD.md).

**Quick flow (Nix-based, recommended):**

```bash
# Enter Nix development environment (all dependencies, reproducible)
nix develop

# Configure with tests enabled
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON

# Build
cmake --build build -j

# Run all tests + MVP architecture check
ctest --test-dir build --output-on-failure
```

**Key build flags:**
- `BUILD_GUI=ON` (default) — build GUI; set OFF for CLI-only testing
- `BUILD_UNIT_TESTS=ON` (local) / OFF (release) — controls unit test compilation
- `BUILD_GUI_TESTS=ON` — enables the `orc-tests/gui/unit` test subtree when GUI behavior changes are in scope
- `CMAKE_BUILD_TYPE=Debug` (local) / Release (packaging)
- `EZPWD_INCLUDE_DIR` — path to ezpwd-reed-solomon headers (auto-set in Nix, required for manual builds)

## Testing Strategy

`TESTING.md` is the authoritative source for test strategy, labels, and definition-of-done guidance for both `orc-core` and `orc-gui` work. Use that document when the codebase or older planning docs disagree.

The project targets ~80% unit test coverage with these principles:

- **Unit tests:** Mock all dependencies; never hit filesystem, network, database, or system clock. Test one method/class in isolation.
- **Integration tests:** Sparse; reserved for happy-path wiring checks only.
- **Mocks:** Use Google Test `MOCK_METHOD` framework on interfaces (not classes).

### ⚠️ CRITICAL: Unit Test Constraints (Non-Negotiable)

**Unit tests MUST NOT access the filesystem, network, database, or system clock under ANY circumstances.** This is enforced in code review:

- ❌ **Forbidden:** Creating temp files, reading JSON/YAML files, loading config files, accessing any external resource
- ❌ **Forbidden:** Directly calling code that touches disk/network (even "just to test happy path")
- ✅ **Required:** Mock ALL external dependencies via interfaces; inject mocks into the class under test
- ✅ **Required:** Test the class's logic in isolation; verify calls to dependencies, not the dependencies' behavior

**Example:** When testing metadata parsing logic that handles both "PAL_M" and "PAL-M" format names, do NOT read real JSON files. Instead, mock the metadata reader interface and feed it expected format-name strings directly through mocked method calls.

See [TESTING.md](../TESTING.md) "Unit tests" section and examples like `daphne_vbi_writer_util_test.cpp` for how to structure tests with proper mocking.

**When adding/changing behavior:**
1. Write or update unit tests first.
2. Mock external dependencies (file I/O, network, etc.).
3. Run the label-appropriate `ctest` invocation from `TESTING.md` locally.
4. Verify no new MVP violations: `ctest -R MVPArchitectureCheck` or `cmake --build build --target check-mvp`.

### CTest Labels

Use CTest labels to keep local iteration and CI slices aligned with the repo conventions.

| Label | Scope |
|-------|-------|
| `unit` | Fast GoogleTest-based unit-test suites |
| `mvp` | MVP architecture boundary check only (`MVPArchitectureCheck`) |
| `sources` | Source-stage unit tests |
| `transforms` | Transform-stage unit tests |
| `sinks` | Sink-stage unit tests |
| `contracts` | Cross-stage contract and registry coverage |
| `gui` | Any test in `orc-tests/gui/unit` |
| `gui-logic` | Tier 1 GUI helper tests with no `QApplication` |
| `gui-model` | Tier 2 GUI model/coordinator tests with `QCoreApplication` |
| `gui-widget` | Tier 3 offscreen widget/dialog tests |

### orc-core Expectations

For new stages and stage behavior changes, follow the `orc-core` section in `TESTING.md`. The detailed expectations previously duplicated in `docs/stage-test-expectations.md` now live there.

Minimum definition of done for stage work:
- Add the matching suite under `orc-tests/core/unit/stages/<stage_id>` in the same PR.
- Register the suite with `gtest_discover_tests(... PROPERTIES LABELS ...)` using `unit` plus the appropriate family label.
- Preserve shared contract coverage for registry, node discovery, parameter/default parity, and project-to-DAG wiring.

### orc-gui Expectations

For GUI testing tiers, offscreen harness details, and command examples, follow the `orc-gui` section in `TESTING.md`.

Required for GUI behavior changes in `orc/gui/`:
- Enable `BUILD_GUI_TESTS=ON`.
- Add tier-appropriate tests under `orc-tests/gui/unit/` in the same PR.
- Keep GUI tests at the presenter boundary; do not run a live `orc-core` pipeline in GUI unit tests.
- Use `QT_QPA_PLATFORM=offscreen` for widget/dialog coverage.

Minimum GUI coverage expectations:
- Pure helper logic: Tier 1 `gui-logic` coverage.
- Model/coordinator classes: Tier 2 `gui-model` coverage with presenter-boundary mocks.
- Dialog classes: Tier 3 `gui-widget` smoke coverage using the offscreen backend.
- Parameter-editing dialogs: Tier 3 smoke coverage plus parameter round-trip coverage.
- `RenderCoordinator`: request ordering, response delivery, stale-response suppression, and clean shutdown semantics.

### Validation Gates

Run the narrowest set that matches the change scope before proposing changes.

Core-only or mixed changes:
1. `cmake --build build -j`
2. `ctest --test-dir build --output-on-failure`
3. `ctest --test-dir build -R MVPArchitectureCheck --output-on-failure`

GUI behavior changes:
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON -DBUILD_GUI_TESTS=ON`
2. `cmake --build build -j`
3. `QT_QPA_PLATFORM=offscreen ctest --test-dir build -L gui --output-on-failure`
4. `ctest --test-dir build -R MVPArchitectureCheck --output-on-failure`

## CI/CD & Multi-Platform

Current workflows (`.github/workflows/`):

- **build-and-test.yml:** Runs on Linux with Nix; executes CMake config, build, and ctest.
- **package-macos.yml:** Builds on macOS; uses Homebrew + manual vcpkg. DMG output.
- **package-flatpak.yml:** Builds on Linux; produces Flatpak bundle.
- **package-windows.yml:** Builds on Windows with MSVC 2022 & vcpkg; produces MSI.
- **release-from-artifact.yml:** Publishes artifacts to GitHub Releases (tags only).

**Before proposing changes:**
- Test locally with the Nix/Linux flow (primary CI gate).
- If modifying build system, packaging, or dependencies, review the platform-specific workflow file for that target.
- Document in PR what was validated locally and what could not be (e.g., "Unable to validate Windows MSI build on Linux"; add flag for reviewers to test).

## Contribution Hygiene

From CONTRIBUTING.md:

- **Focused changes:** One clear problem per PR.
- **Commit messages:** Clear, descriptive; reference issue numbers.
- **Documentation:** Update README.md and docs if behavior or usage changes (see BUILD.md for build-related changes).
- **Avoid refactoring unrelated code** — saves iteration and speeds review.
- **Search existing issues and PRs first** to avoid duplicates.

## Guardrails

- **Trust the instructions:** Use them as the source of truth. Search codebase only if instructions are incomplete or found to be incorrect.
- **Verify build commands:** If docs disagree with working workflows, workflows are correct; flag the discrepancy in your PR.
- **Respect architecture:** MVP layer boundaries are enforced by tests and CI.
- **Test first, refactor never:** Don't introduce new frameworks or patterns unless explicitly requested; follow existing conventions.

## Common Commands Reference

```bash
# Clean build
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON -DBUILD_GUI_TESTS=ON && cmake --build build -j

# Run only unit tests (skip MVP check)
ctest --test-dir build -E MVPArchitectureCheck --output-on-failure

# Check MVP architecture only
ctest --test-dir build -R MVPArchitectureCheck

# Run all GUI tests with the offscreen backend
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L gui --output-on-failure

# Run a GUI widget slice only
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L gui-widget --output-on-failure

# Build without tests (faster for iteration)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=OFF && cmake --build build -j

# Run a specific test
ctest --test-dir build -R "test_name_pattern" --output-on-failure
```
