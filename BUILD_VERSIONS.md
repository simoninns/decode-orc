# Build Environment Version Alignment

This document describes the standardized library and tool versions used across all build platforms to ensure consistent behavior and easier bug tracking.

## Qt Version

Target version: **Qt 6.8.x** for consistency across platforms

**Platform-specific Qt versions:**
- **Windows**: Qt 6.8.1 (via `jurplel/install-qt-action@v4`)
- **macOS**: Qt 6.8.1 (via `jurplel/install-qt-action@v4`)
- **Fedora**: System Qt (currently 6.10.x - newer but API compatible)
- **Flatpak**: Qt 6.8.x (via `org.kde.Platform 6.8` runtime)

## Core Library Versions

### Target Versions (aligned with Flatpak manifest)

| Library    | Version  | Notes                                      |
|------------|----------|--------------------------------------------|
| spdlog     | ≥1.14.1  | Logging library                            |
| yaml-cpp   | ≥0.8.0   | YAML configuration parser                  |
| fmt        | ≥10.2.1  | Formatting library (spdlog dependency)     |
| fftw3      | Latest   | Fast Fourier Transform (float precision)   |
| sqlite3    | Latest   | Database library                           |
| libpng     | Latest   | PNG image library                          |

### Platform-Specific Implementation

#### Windows (vcpkg)
- Uses `vcpkg.json` manifest mode for automatic dependency management
- Versions specified in `vcpkg.json` with `version>=` constraints
- Baseline commit ensures reproducible builds

#### macOS (Homebrew)
- Qt 6.8.1 installed via install-qt-action (overrides brew qt@6)
- Other libraries use Homebrew latest (generally close to target versions)
- Homebrew packages track upstream releases closely

#### Fedora
- Qt 6.8.1 installed via install-qt-action (overrides Fedora RPM packages)
- System libraries via DNF (Fedora packages are well-maintained and recent)
- Fedora Latest container provides near-upstream versions

#### Flatpak
- Qt 6.8 via org.kde.Platform 6.8 runtime
- All dependencies built from source with exact version pinning
- Most controlled and reproducible environment

## Version Alignment Strategy

### Primary Reference: Flatpak Manifest
The `io.github.simoninns.decode-orc.yml` flatpak manifest serves as the **source of truth** for library versions:
- Explicit version tags for all dependencies
- Cryptographic hashes for archive integrity
- Well-tested KDE Platform 6.8 runtime

### Secondary Platforms Alignment
Other platforms (Windows, macOS, Fedora) should track Flatpak versions as closely as possible:
- **Windows**: vcpkg.json specifies minimum versions matching Flatpak
- **macOS**: Homebrew formulas typically provide recent versions; Qt override ensures consistency
- **Fedora**: System packages updated frequently; Qt override ensures consistency

## Maintenance Guidelines

When updating library versions:

1. **Update Flatpak manifest first** (`io.github.simoninns.decode-orc.yml`)
   - Test the build thoroughly
   - Verify runtime compatibility

2. **Update vcpkg.json** (Windows)
   - Adjust `version>=` constraints
   - Update `builtin-baseline` if needed for newer packages

3. **Verify macOS/Fedora**
   - Check that system packages meet minimum versions
   - Update install commands if package names change

4. **Update this documentation**
   - Keep version table current
   - Note any platform-specific workarounds

## Testing Version Consistency

To verify version alignment across platforms:

1. Check build logs for actual installed versions
2. Compare library versions in compiled binaries
3. Run identical test suites on all platforms
4. Compare behavior of edge cases across platforms

## Troubleshooting Version Mismatches

If you encounter platform-specific bugs:

1. **Check library versions first**
   ```bash
   # On built system
   ldd bin/orc-cli  # Linux
   otool -L bin/orc-cli  # macOS
   # Check CMake output logs for version info
   ```

2. **Compare with Flatpak versions**
   - Flatpak builds are most controlled
   - Use as reference for expected behavior

3. **Document version-specific workarounds**
   - Add notes to this file
   - Reference in bug reports

## Future Improvements

- [ ] Add automated version checking in CI
- [ ] Generate version report artifacts from each build
- [ ] Create version comparison dashboard
- [ ] Automate version bumps across all platforms
