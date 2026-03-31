# Installation and Asset Setup

The way you install `encode-orc` affects how you reference assets in your YAML projects and the environment variables available.

---

## Nix Installation (Recommended)

When installed via Nix, `encode-orc` automatically handles asset location and sets up environment variables:

```bash
nix shell github:simoninns/encode-orc
```

This installation:
- Installs the binary to your PATH
- Installs testcard images to `share/encode-orc/assets` (managed by Nix)
- Automatically sets `ENCODE_ORC_ASSETS` environment variable
- Enables portable, system-independent YAML projects

**YAML files with Nix installation:**
```yaml
sections:
  - name: "Test"
    source:
      # This works automatically - ENCODE_ORC_ASSETS is already set by Nix
      file: "${ENCODE_ORC_ASSETS}/pal-raw/625_50_75_BARS.raw"
```

The Nix flake will wrap the binary with:
```bash
export ENCODE_ORC_ASSETS=/nix/store/xyz.../share/encode-orc/assets
```

---

## Local/Source Installation

For local development or source builds:

### Prerequisites
- CMake 3.14 or later
- C++17 compiler
- Libraries: sqlite3, yaml-cpp, libpng, spdlog, FFmpeg

### Building from Source

```bash
git clone https://github.com/simoninns/encode-orc.git
cd encode-orc
mkdir build && cd build
cmake ..
make
```

The binary will be at `./build/bin/encode-orc`

### Asset Location

For local installations:
- Assets are typically in `./assets` relative to your YAML file
- Or set `ENCODE_ORC_ASSETS` environment variable manually

**Setting the variable for development:**
```bash
export ENCODE_ORC_ASSETS=/path/to/encode-orc/assets
encode-orc project.yaml
```

---

## Environment Variables

Both installation methods support configurable environment variables for flexibility.

### ENCODE_ORC_ASSETS

Controls the location of installed testcard images and assets.

**Nix automatically sets this to:**
```
/nix/store/.../share/encode-orc/assets
```

**For manual setup:**
```bash
export ENCODE_ORC_ASSETS=/path/to/assets
encode-orc project.yaml
```

**Fallback behavior (when not set):**
- Looks for assets in `../assets` relative to the YAML file's directory
- Useful for development or local installations

### ENCODE_ORC_OUTPUT_ROOT

Controls the root directory where output files are written. Useful for specifying a consistent output location across multiple YAML projects.

**Setting the variable:**
```bash
export ENCODE_ORC_OUTPUT_ROOT=/home/user/encoded-videos
encode-orc project.yaml
```

**Default behavior (when not set):**
- If the YAML file is in a `test-projects` directory: outputs to `test-output`
- If the YAML file is in a `ggv-tests` directory: outputs to `ggv-output`
- Otherwise: outputs to `output` directory (sibling of YAML file's parent)

---

## Path Resolution with Environment Variables

YAML project files can reference environment variable paths:

```yaml
output:
  filename: "${ENCODE_ORC_OUTPUT_ROOT}/my-video"
  format: "pal-composite"

sections:
  - name: "Content"
    source:
      file: "${ENCODE_ORC_ASSETS}/pal-raw/625_50_75_BARS.raw"
```

This makes projects portable across different installation locations and deployment scenarios.

---

## Batch Encoding with Output Control

Example batch encoding script using `ENCODE_ORC_OUTPUT_ROOT`:

```bash
#!/bin/bash
export ENCODE_ORC_OUTPUT_ROOT=/mnt/storage/videos

# All projects output to the same root directory
encode-orc ~/projects/project1/encode.yaml
encode-orc ~/projects/project2/encode.yaml
encode-orc ~/projects/project3/encode.yaml

# Output files created:
# /mnt/storage/videos/video1.tbc
# /mnt/storage/videos/video2.tbc
# /mnt/storage/videos/video3.tbc
```

---

## Using Nix for Reproducible Environments

Nix ensures all dependencies are correct and reproducible:

```bash
# Enter a development environment with all dependencies
nix develop github:simoninns/encode-orc

# All environment variables are automatically set
echo $ENCODE_ORC_ASSETS

# Run encode-orc
encode-orc project.yaml
```

This is equivalent to:
```bash
nix shell github:simoninns/encode-orc
encode-orc project.yaml
```
