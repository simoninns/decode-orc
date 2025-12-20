# Dependency Management Strategy

This document explains how decode-orc manages its external dependencies and the rationale behind each approach.

## Overview

decode-orc uses a **consistent dependency management strategy**:

- **System Packages** (find_package): All dependencies use system-installed libraries
- **No CMake FetchContent**: Avoided to maintain consistency
- **No Git Submodules**: Avoided for third-party dependencies due to maintenance overhead

## Dependencies

### 1. spdlog (Logging Library)

**Type:** Header-only C++ library  
**Management:** System package via `find_package()`  
**Source:** https://github.com/gabime/spdlog

**Rationale:**
- ✅ Available in all major package managers (apt, dnf, brew, vcpkg)
- ✅ Header-only means no runtime linking complexity
- ✅ Consistent with other dependencies
- ✅ System manages version updates
- ✅ Shared across multiple projects on same system
- ⚠️ Requires documentation of installation requirements

**Why not FetchContent?**
- Would create inconsistency with other deps
- No significant benefits for widely-available library
- Increases build time on first build

**Why not submodule?**
- Requires manual `git submodule update --init`
- Adds complexity to git workflow
- No benefits over system packages

---

### 2. yaml-cpp (YAML Parser)

**Type:** Compiled C++ library  
**Management:** System package via `find_package()`  
**Source:** https://github.com/jbeder/yaml-cpp

**Rationale:**
- ✅ Available in all major package managers (apt, dnf, brew, vcpkg)
- ✅ Compiled library - benefits from system-level optimization
- ✅ Shared across multiple projects (disk space efficiency)
- ✅ Security updates handled by system package manager
- ⚠️ Requires documentation of installation requirements

**Why not FetchContent?**
- Would significantly increase build time (full compilation)
- No version pinning benefits (stable API)
- Wastes disk space with multiple copies

**Why not submodule?**
- Requires users to compile from source manually
- No benefit over system packages
- Adds unnecessary maintenance burden

---

### 3. SQLite3 (Database)

**Type:** Compiled C library  
**Management:** System package via `find_package()`  
**Source:** https://www.sqlite.org/

**Rationale:**
- ✅ Universal availability (included with most OSes)
- ✅ Highly platform-optimized by system vendors
- ✅ Security and bug fixes via system updates
- ✅ Standard dependency for C/C++ projects
- ✅ Extremely stable API (no version concerns)

**Why not FetchContent?**
- System SQLite is highly optimized for the platform
- Compiling from source offers no benefits
- Standard practice is to use system SQLite

**Why not submodule?**
- SQLite amalgamation is large
- No benefits over system package
- Would require custom build configuration

---

### 4. libpng (PNG Image Library)

**Type:** Compiled C library  
**Management:** System package via `find_package()`  
**Source:** http://www.libpng.org/pub/png/libpng.html

**Rationale:**
- ✅ Universal availability (standard on all platforms)
- ✅ System-optimized with architecture-specific builds
- ✅ Security updates via system package manager
- ✅ Standard dependency for image I/O
- ✅ Extremely stable API
- ✅ Used for PNG export from preview renderer

**Why not FetchContent?**
- System libpng is highly optimized
- Standard practice to use system library
- No benefits from source compilation

**Why not submodule?**
- Requires zlib dependency
- System package better maintained
- No benefit over system installation

---

### 5. Qt6 (GUI Framework)

**Type:** Large compiled framework  
**Management:** System package via `find_package()` (optional)  
**Source:** https://www.qt.io/

**Rationale:**
- ✅ Massive framework (gigabytes) - must be system-installed
- ✅ Platform-specific builds required
- ✅ Optional dependency (GUI-only)
- ✅ Standard Qt deployment model
- ⚠️ Users must install Qt6 development packages

**Why not FetchContent?**
- Impossible - Qt requires hours to build from source
- Platform-specific configuration complexity
- Would dramatically increase build time

**Why not submodule?**
- Completely impractical for a framework this large
- Qt has its own installer/package ecosystem

---

## General Principles

### When to use find_package (system):
- ✅ All external dependencies (consistency)
- ✅ Large compiled libraries
- ✅ Header-only libraries available in repos
- ✅ Platform-specific libraries
- ✅ Stable, widely-available dependencies

### When to use FetchContent:
- ❌ **Not used in decode-orc** for consistency
- (Could be used for truly rare dependencies not in package managers)

### When to use Git Submodules:
- ✅ **Your own libraries** that you control
- ✅ Tightly-coupled internal components
- ❌ **NOT for third-party dependencies** (maintenance burden)

---

## Maintenance

### Updating System Dependencies:

System dependencies are managed by package managers. Document minimum required versions in README.md if needed.

For manual version checking:
```bash
# Check installed versions
pkg-config --modversion spdlog
pkg-config --modversion yaml-cpp
sqlite3 --version
```

### Adding New Dependencies:

1. **Determine type**: header-only? compiled? size?
2. **Check availability**: in apt/dnf/brew?
3. **Choose strategy**: FetchContent or find_package
4. **Document**: Add to README.md and DEPENDENCIES.md
5. **Test**: Verify on Linux, macOS, Windows

---

## CI/CD Considerations

### Docker/Container Builds:
```dockerfile
# Install all dependencies via system packages
RUN apt-get update && apt-get install -y \
    cmake build-essential \
    libspdlog-dev libsqlite3-dev libyaml-cpp-dev qt6-base-dev
```

### GitHub Actions:
```yaml
- name: Install dependencies (Ubuntu)
  run: |
    sudo apt-get update
    sudo apt-get install -y libspdlog-dev libsqlite3-dev libyaml-cpp-dev qt6-base-dev
```

---

## FAQ

**Q: Why not use a C++ package manager like Conan or vcpkg?**

A: While both are excellent, they add another layer of tooling. Our hybrid approach works well for decode-orc's relatively small dependency set. For larger projects, package managers become more beneficial.

**Q: Should we pin system package versions?**

A: No. System packages evolve slowly and maintain backward compatibility. Pinning specific versions creates installation friction and doesn't provide significant benefits.

**Q: What about Windows users?**

A: Windows users can:
- Use vcpkg for dependencies: `vcpkg install sqlite3 yaml-cpp qt6-base`
- Use system-installed Qt6 from qt.io
- CMake find_package() works with vcpkg automatically

**Q: Can we reduce the number of dependencies?**

A: Current dependencies are minimal and justified:
- SQLite3: Required for TBC metadata (industry standard)
- yaml-cpp: Required for configuration files (standard format)
- spdlog: Best-in-class C++ logging (minimal overhead)
- Qt6: GUI framework (optional, no alternative)

---

## Summary

decode-orc's dependency strategy prioritizes:

1. **Consistency**: All dependencies use find_package (system packages)
2. **System integration**: Leverage platform-optimized libraries
3. **Simplicity**: One approach for all dependencies
4. **Developer experience**: Clear documentation of requirements
5. **Maintenance**: Standard system package updates handle everything

This uniform approach provides clarity, reduces build complexity, and works reliably across all platforms with proper documentation.
