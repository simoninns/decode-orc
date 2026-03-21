# Building Decode-Orc

This guide covers building Decode-Orc from source for development and production use.

## Quick Start

### Using Nix (Recommended for Reproducible Builds)

The project supports [Nix](https://nixos.org/) for deterministic, reproducible builds with all dependencies pre-configured:

```bash
# Enter development environment
nix develop

# Build the project
nix build

# Run the application
nix run .#orc-gui
```

If you want to pass command line options when using `nix run`:

```bash
nix run git+file:///home/pathtoproject/decode-orc#orc-gui -- --log-level debug --log-file /tmp/orc-gui.log
```

### CMake with Presets (Cross-Platform)

For Linux, macOS, and Windows developers using system package managers or vcpkg:

```bash
# Configure with tests enabled (development)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON

# Build
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure
```

## Detailed Build Instructions

### Prerequisites

#### Nix-Based Setup
- [Nix](https://nixos.org/) (recommended for reproducible builds)
- `flake.nix` defines all dependencies and development environment

#### CMake-Based Setup
- **CMake**: 3.20 or later
- **C++ Compiler**: C++17 support
  - Linux: GCC 9+ or Clang 9+
  - macOS: Apple Clang (Xcode 12+)
  - Windows: MSVC 2022
- **Qt6**: For GUI builds (`BUILD_GUI=ON`)
- **Dependencies**: Managed via vcpkg or system packages (see `vcpkg.json` for the dependency list)
- **EZPWD Headers**: Reed-Solomon library headers (automatically provided in Nix; for manual setups, set `EZPWD_INCLUDE_DIR` environment variable)

### Nix Development Environment

The `nix develop` shell includes all build tools and dependencies:

```bash
# Enter the development shell
nix develop

# Inside the shell, you have access to:
# - cmake, ninja
# - Qt6 (qtbase, qttools)
# - FFmpeg, spdlog, yaml-cpp, sqlite, libpng, fftw
# - Google Test (gtest) for unit testing
# - pkg-config for dependency discovery
# - Clang tools, ccache, doxygen, graphviz (on Linux/macOS)
# - gdb/lldb debuggers

# Typical workflow:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Manual CMake Build (Without Nix)

If you prefer to use system package managers or vcpkg manually:

#### 1. Install Dependencies

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build pkg-config \
  qtbase6-dev qttools6-dev \
  libspdlog-dev libfmt-dev \
  libsqlite3-dev libyaml-cpp-dev libpng-dev libfftw3-dev \
  libffmpeg-dev google-gtest-dev
```

**macOS (Homebrew):**
```bash
brew install cmake ninja qt@6 spdlog fmt sqlite yaml-cpp libpng fftw ffmpeg google-benchmark
```

**Windows (vcpkg):**
See `vcpkg.json` for the manifest-based dependency list. The CI/CD workflow (`package-windows.yml`) documents how dependencies are resolved on Windows.

#### 2. Provide EZPWD Headers

The project requires the `ezpwd-reed-solomon` headers (header-only library):

```bash
# Clone the ezpwd repository
git clone --depth 1 https://github.com/pjkundert/ezpwd-reed-solomon external/ezpwd-reed-solomon

# Set the environment variable (or pass via CMake)
export EZPWD_INCLUDE_DIR="$PWD/external/ezpwd-reed-solomon/c++"
```

Alternatively, pass it directly to CMake:
```bash
cmake -S . -B build -DEZPWD_INCLUDE_DIR="/path/to/ezpwd-reed-solomon/c++" ...
```

#### 3. Configure with CMake

```bash
# Debug build with unit tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON

# Or use a preset for your platform (see CMakePresets.json):
# Linux:
cmake --preset linux-gui-debug

# macOS:
cmake --preset macos-gui-debug

# Windows:
cmake --preset windows-gui-debug
```

**Common CMake options:**
- `CMAKE_BUILD_TYPE`: `Debug` or `Release`
- `BUILD_GUI`: `ON` (default) or `OFF` (CLI only)
- `BUILD_UNIT_TESTS`: `ON` (development) or `OFF` (release)
- `BUILD_DOCS`: `OFF` (default) or `ON` (generate Doxygen docs)
- `EZPWD_INCLUDE_DIR`: Path to ezpwd headers (or set `EZPWD_INCLUDE_DIR` environment variable)

#### 4. Build

```bash
# Build using the configured generator
cmake --build build -j

# Or use make/ninja directly (depending on your generator)
make -C build -j
ninja -C build
```

#### 5. Run Tests

```bash
# Run all tests (unit tests + MVP architecture check)
ctest --test-dir build --output-on-failure

# Run only unit tests (skip MVP check)
ctest --test-dir build -E MVPArchitectureCheck --output-on-failure

# Run only MVP architecture check
ctest --test-dir build -R MVPArchitectureCheck --output-on-failure

# Run a specific test by name
ctest --test-dir build -R "test_name_pattern" --output-on-failure
```

### Build Outputs

After a successful build, executables and libraries are placed in:

```
build/
├── bin/
│   ├── orc-gui          # GUI application (if BUILD_GUI=ON)
│   └── orc-cli          # CLI application
├── lib/
│   └── [library files]
├── generated/
│   └── version.h        # Generated version header
└── ... (other CMake artifacts)
```

### Development Workflow

**Recommended development workflow:**

```bash
# Enter Nix shell
nix develop

# Configure once
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON

# Edit code, then rebuild (fast iteration)
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure

# Validate MVP architecture
ctest --test-dir build -R MVPArchitectureCheck

# Clean build if needed
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON && cmake --build build -j
```

## Architecture Notes

The project follows this CMake structure:

- **`orc/core/`** — Core business logic library (no UI dependencies)
- **`orc/presenters/`** — Presenter layer (bridges core to UI)
- **`orc/view-types/`** — Shared data structures for presentation
- **`orc/cli/`** — CLI application
- **`orc/gui/`** — Qt6 GUI application (optional)
- **`orc-unit-tests/`** — Unit test suite (compiled when `BUILD_UNIT_TESTS=ON`)

MVP architecture boundaries are enforced via `ctest -R MVPArchitectureCheck`. See [TESTING.md](TESTING.md) for testing strategy details.

## CI/CD & Multi-Platform Builds

The repository includes GitHub Actions workflows that test and package for multiple platforms:

- **Linux + Nix:** Primary CI gate (`.github/workflows/build-and-test.yml`)
- **macOS:** DMG packaging (`.github/workflows/package-macos.yml`)
- **Windows:** MSI packaging (`.github/workflows/package-windows.yml`)
- **Linux Flatpak:** Flatpak bundle (`.github/workflows/package-flatpak.yml`)

See [`.github/workflows/`](.github/workflows/) for details on platform-specific build configurations and dependencies.

## Troubleshooting

### CMake not found
Ensure CMake 3.20+ is installed and in your PATH:
```bash
cmake --version
```

### Qt6 not found
Install Qt6 via your package manager or set `CMAKE_PREFIX_PATH`:
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/lib/cmake/Qt6
```

### EZPWD headers not found
Set the `EZPWD_INCLUDE_DIR` environment variable or pass it to CMake:
```bash
export EZPWD_INCLUDE_DIR="$PWD/external/ezpwd-reed-solomon/c++"
```

### Tests failing to compile
Ensure Google Test (gtest) is available. In Nix, use `nix develop`. For manual setups, install:
```bash
# Linux
sudo apt-get install google-gtest-dev

# macOS
brew install google-benchmark
```

### Build is slow
Use ccache to speed up incremental builds:
```bash
export CMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON
```

## Further Reading

- [TESTING.md](TESTING.md) — Testing strategy and unit test patterns
- [CONTRIBUTING.md](CONTRIBUTING.md) — Contribution guidelines
- [CMakePresets.json](CMakePresets.json) — Available build presets for all platforms
- [flake.nix](flake.nix) — Nix development environment and reproducible build configuration
