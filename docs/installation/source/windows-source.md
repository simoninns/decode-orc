# Compiling from source using cmake (Windows)

The project requires [Nix](https://nixos.org/) for deterministic, reproducible builds.  Since Nix is not supported on Windows it is recommended to only build on Windows in order to test specific Windows functionality.  Any Windows-specific code will be built by Nix and tested during CI/CD so it is important to retest using Nix after making any changes under Windows.

## Required Environment

Install the following tools:

- **Visual Studio 2022 Build Tools** (C++ workload)
  - MSVC v143 toolset
  - Windows 10/11 SDK
- **CMake** (3.20+)
- **Git for Windows** (provides `git` and `bash`)
- **vcpkg** (for C/C++ dependencies)
- **ImageMagick** (`magick`) for Windows icon generation (recommended)

Optional but useful:

- **Ninja** (not required when using Visual Studio generator/presets)
- **PowerShell 7**
- **7-Zip**

## Required Source Dependencies

From repo root:

```powershell
git submodule update --init --recursive
```

This project expects `QtNodes` at:

- `external/qtnodes`

## Required vcpkg Packages

From your `vcpkg` checkout:

```powershell
.\vcpkg.exe install spdlog:x64-windows sqlite3:x64-windows yaml-cpp:x64-windows libpng:x64-windows fftw3:x64-windows ffmpeg:x64-windows qtbase:x64-windows
```

## Build Using CMake Presets (Recommended)

This repository provides Windows presets in CMakePresets.json.

### Configure + build GUI (Debug)

From repo root

```powershell
cmake --preset windows-gui-debug
cmake --build --preset build-windows-gui-debug
```

To run the resulting executable:

```
.\build\windows-gui-debug\bin\Debug\orc-gui.exe  --log-level debug --log-file /tmp/orc-gui.log
```

### Configure + build CLI-only (Debug)

```powershell
cmake --preset windows-cli-debug
cmake --build --preset build-windows-cli-debug
```

### Release builds

```powershell
cmake --preset windows-gui-release
cmake --build --preset build-windows-gui-release

cmake --preset windows-cli-release
cmake --build --preset build-windows-cli-release
```

To run the resulting executable:

```
.\build\windows-gui-release\bin\Release\orc-gui.exe  --log-level debug --log-file /tmp/orc-gui.log
```

The Windows MSI GitHub Action uses the same `windows-gui-release` + `build-windows-gui-release` preset flow so CI and local builds stay aligned. This preset keeps `BUILD_GUI=ON`, which guarantees `orc-gui.exe` is built.

### Run tests

```powershell
ctest --preset test-windows-gui-debug
# or
ctest --preset test-windows-cli-debug
```

## Manual Build Commands (Without Presets)

If needed, configure explicitly:

```powershell
cmake -S . -B build-vs-gui -G "Visual Studio 17 2022" -A x64 -DBUILD_GUI=ON -DBASH_EXECUTABLE="C:/Program Files/Git/bin/bash.exe" -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build-vs-gui --config Debug -- /m
```

CLI-only:

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DBUILD_GUI=OFF -DBASH_EXECUTABLE="C:/Program Files/Git/bin/bash.exe" -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build-vs --config Debug -- /m
```

## Output Paths

- GUI executable: `build/windows-gui-debug/bin/Debug/orc-gui.exe`
- CLI executable: `build/windows-cli-debug/bin/Debug/orc-cli.exe`
