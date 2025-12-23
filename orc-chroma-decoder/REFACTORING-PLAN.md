# Refactoring Plan: ld-chroma-decoder → orc-core Sink Stage

**Document Status:** Active Development Plan  
**Created:** December 2025  
**Last Updated:** 22 December 2025

---

## Overview

This document outlines the step-by-step plan to refactor the standalone `orc-chroma-decoder` (legacy `ld-chroma-decoder`) into an integrated sink stage within `orc-core`. The goal is to maintain 100% output compatibility while transitioning to the DAG-based architecture.

### Success Criteria

- All 24 test signatures remain identical after migration
- GUI preview shows colorized output in real-time
- Performance matches or exceeds standalone tool
- Clean integration with orc-core stage system

---

## Current Architecture Analysis

### ld-chroma-decoder Structure

**Components:**
- **main.cpp**: CLI orchestration, command-line parsing, configuration
- **DecoderPool**: Threading coordinator, I/O management, batch processing
- **Decoder**: Abstract base class for all decoders
  - `PALDecoder` (2D) - Simple PAL comb filter
  - `TransformPal2D` - 2D frequency-domain decoder
  - `TransformPal3D` - 3D frequency-domain decoder with temporal filtering
  - `NtscDecoder` (1D/2D/3D/3D-NoAdapt) - NTSC comb filter variants
  - `MonoDecoder` - Black & white (no chroma decoding)
- **DecoderThread**: Worker threads for parallel field processing
- **OutputWriter**: Format conversion (RGB48, YUV444P16, Y4M, GRAY16)
- **SourceField/ComponentFrame**: Internal data representations
- **TBC I/O**: Direct file reading using `LdDecodeMetaData` and `SourceVideo`

**Processing Flow:**
```
TBC File → SourceVideo → DecoderPool → DecoderThreads → OutputWriter → Output File
                ↓
          LdDecodeMetaData
```

### orc-core Sink Pattern

**Interfaces:**
- `DAGStage`: Base class with `execute()` method (normal DAG execution)
- `TriggerableStage`: Interface with `trigger()` for manual export
- `PreviewableSink`: Interface with `render_preview_field()` for GUI
- `ParameterizedStage`: Configuration management

**Processing Flow:**
```
VideoFieldRepresentation → ChromaSinkStage::execute() → (no output, preview only)
                                      ↓
                        ChromaSinkStage::trigger() → Output File
```

**Key Differences:**
- orc-core operates on `VideoFieldRepresentation` artifacts, not files
- Sinks have no outputs during normal execution (only on trigger)
- Preview is generated on-demand for GUI, separate from export
- Parameters are managed through stage system, not CLI parser

## Refactoring Steps

### High-Level Plan

**Step 1:** ✅ **COMPLETE** - Create ChromaSinkStage skeleton (GUI integration, parameters)  
**Step 2:** ✅ **COMPLETE** - Copy decoder files to orc-core (read-only, with Qt6)  
**Step 3:** ✅ **COMPLETE** (23 Dec 2025) - Implement integration layer (keep Qt6)  
   - ✅ Create adapter: VideoFieldRepresentation → SourceField  
   - ✅ Implement ChromaSinkStage::trigger()  
   - ✅ Create test infrastructure (ORC projects + test script)  
   - ✅ Enable AUTOMOC for Qt threading support
   - ✅ Link FFTW3 for Transform2D/3D decoders
   - ✅ Implement VideoParameters mapping (active regions, LineParameters)
   - ✅ Synchronous decoder invocation (bypass threading infrastructure)
   - ✅ OutputWriter integration (RGB48/YUV444P16/Y4M formats)
   - ✅ Frame-to-field ID mapping and field parity detection
   - ✅ Fixed padding adjustment bug (activeVideoStart/activeVideoEnd)
   - ✅ Fixed Transform3D temporal position bug (frame-by-frame processing)
   - ✅ All 24 tests passing with pixel-perfect output
   - **STATUS:** Integration complete and production-ready
   
**Step 4:** ⏳ **PENDING** - Remove Qt6 dependencies from decoders
   - Phase A: Data types & containers  
   - Phase B: Threading & synchronization  
   - Phase C: Algorithm classes  
   - Phase D: Metadata & parameters  
   - Phase E: Build system updates  
   - **Goal:** Pure C++17 decoder algorithms, test signatures still match  
   
**Step 5:** ⏳ **PENDING** - Deeper orc-core integration  
   - Use VideoFieldRepresentation directly (remove SourceField wrapper)  
   - Use orc-core metadata/hints/observers  
   - Use DAG executor parallelism  
   - **Goal:** Native orc-core integration, no legacy abstractions  

### Testing Strategy

**Two-Phase Testing Approach:**

1. **Standalone Decoder Tests** (24 tests)
   - Located in: `orc-chroma-decoder/tests/`
   - Tests the standalone `orc-chroma-decoder` binary
   - Generates reference signatures: `references/test-signatures.txt`
   - Validates that the standalone decoder works correctly

2. **ORC Integration Tests** (24 matching tests)
   - Located in: `orc/core/stages/chroma_sink/tests/`
   - Tests the ChromaSinkStage within orc-core DAG
   - Compares output to reference signatures from standalone tests
   - Validates that ORC integration produces identical output

**Running Tests:**

```bash
# 1. Build the project
cd build && cmake --build .

# 2. Generate reference signatures (only when decoder changes)
cd ../orc-chroma-decoder/tests
./run-tests.sh generate

# 3. Verify standalone decoder (24 tests)
./run-tests.sh verify

# 4. Test ORC integration (24 tests, compare to references)
cd ../../orc/core/stages/chroma_sink/tests
./test-orc-chroma.sh verify

# 5. Optional: Compare ORC directly to standalone (slow)
./test-orc-chroma.sh compare
```

**Test Coverage (24 tests total):**
- **Decoder types:** PAL (2D, Transform2D, Transform3D), NTSC (1D, 2D, 3D, 3D-NoAdapt), Mono
- **Output formats:** RGB48, YUV444P16, Y4M
- **Parameters:** Chroma gain/phase, noise reduction, phase compensation
- **Edge cases:** Reverse fields, padding, custom line ranges, CAV discs
- **All tests must produce identical output** (byte-perfect matching signatures)

---

## Detailed Step Documentation

Each step is independently testable. Run the test suite after each step to verify no regressions.

---

### Step 1: Create ChromaSinkStage Skeleton

**Status:** ✅ COMPLETE (22 Dec 2025)  
**Risk:** Low  
**Actual Duration:** ~2 hours

#### Objectives
Establish basic infrastructure without functionality. Verify stage registration and parameter system work.

#### Actions

1. **Create directory structure:**
   ```
   orc/core/stages/chroma_sink/
   ├── chroma_sink_stage.h
   ├── chroma_sink_stage.cpp
   └── CMakeLists.txt
   ```

2. **Implement skeleton stage:**
   ```cpp
   class ChromaSinkStage : public DAGStage, 
                          public ParameterizedStage,
                          public TriggerableStage,
                          public PreviewableSink {
   public:
       // DAGStage interface
       std::vector<ArtifactPtr> execute(...) override {
           return {};  // No outputs during normal execution
       }
       
       // TriggerableStage interface
       bool trigger(...) override {
           ORC_LOG_INFO("Chroma sink triggered (not implemented)");
           return true;
       }
       
       // PreviewableSink interface
       std::shared_ptr<const VideoFieldRepresentation> 
       render_preview_field(...) const override {
           return input;  // Passthrough for now
       }
       
       // ParameterizedStage interface
       std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
       // ... parameter management
   };
   ```

3. **Define parameters:**
   - `output_path` (FILE_PATH) - Output file location
   - `decoder_type` (STRING) - auto, pal2d, transform2d, transform3d, ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt, mono
   - `output_format` (STRING) - rgb, yuv, y4m
   - `chroma_gain` (DOUBLE) - Range: 0.0-10.0, Default: 1.0
   - `chroma_phase` (DOUBLE) - Range: -180.0-180.0, Default: 0.0
   - `start_frame` (INTEGER) - Optional start frame
   - `length` (INTEGER) - Optional length in frames
   - `threads` (INTEGER) - Number of worker threads
   - `reverse_fields` (BOOLEAN) - Reverse field order

4. **Register stage:**
   ```cpp
   // In chroma_sink_stage.cpp
   static StageRegistration reg([]() {
       return std::make_shared<ChromaSinkStage>();
   });
   ```

5. **Update build system:**
   - Add `chroma_sink` to `orc/core/stages/CMakeLists.txt`
   - Link required libraries (Qt6::Core, TBC library)

#### Test Verification

**Build Tests:**
```bash
cd build
cmake .. && make -j$(nproc)
# Should complete without errors
```

**GUI Tests:**
- Launch `orc-gui`
- Verify "Chroma Sink" appears in node palette under "Sinks" category
- Create new project, add PAL source and chroma sink
- Connect source → chroma sink
- Verify:
  - Node can be added to canvas
  - Connection is valid (no type errors)
  - Parameters panel shows all parameters
  - Parameter values can be edited
  - Preview shows input unchanged (passthrough)
  - Trigger button appears and completes without error

**CLI Tests:**
```bash
# If orc-cli exists, test programmatic stage creation
orc-cli list-stages | grep chroma_sink
```

#### Success Criteria
- ✅ Build succeeds
- ✅ Stage appears in GUI
- ✅ Parameters are editable
- ✅ Connections work
- ✅ No crashes or errors
- ✅ Existing tests still pass (no regressions)

#### Completion Notes

**Files Created:**
- `/orc/core/stages/chroma_sink/chroma_sink_stage.h` (126 lines)
- `/orc/core/stages/chroma_sink/chroma_sink_stage.cpp` (237 lines)
- `/orc/core/stages/chroma_sink/CMakeLists.txt`

**Files Modified:**
- `/orc/core/CMakeLists.txt` - Added chroma_sink_stage.cpp to build
- `/orc/core/stage_init.cpp` - Added ChromaSinkStage to force-linking

**Implementation Details:**
- All 13 parameters defined with proper ParameterConstraints format:
  - output_path, decoder_type, output_format, chroma_gain, chroma_phase
  - start_frame, length, threads, reverse_fields
  - luma_nr, chroma_nr, ntsc_phase_comp, output_padding
- Used correct ParameterConstraints struct initialization (not fluent API)
- Fixed ParameterType enum names (INT32, BOOL vs INTEGER, BOOLEAN)
- Included TriggerableStage interface via ld_sink_stage.h

**Verification:**
- Build completed without errors
- Stage appears in GUI node palette as "Chroma Decoder Sink"
- Stage can be added to project canvas
- Parameters panel displays all 13 parameters correctly
- Parameter values are editable through GUI
- Stage can be connected to source nodes (accepts VideoFieldRepresentation)
- No crashes during GUI interaction
- orc-chroma-decoder standalone tests still pass (unchanged)

**Known Limitations (by design):**
- `execute()` returns empty vector (sinks have no outputs)
- `trigger()` logs message but doesn't process (implementation in Step 4)
- `render_preview_field()` returns input unchanged (implementation in Step 9)

---

### Step 2: Copy Decoder Classes (Read-Only)

**Status:** ✅ COMPLETE (22 Dec 2025)  
**Risk:** Low  
**Actual Duration:** ~2 hours

#### Objectives
Make all decoder code available in orc-core without integration. Ensure clean compilation with orc-core's dependencies.

#### Actions

1. **Create decoder directory:**
   ```
   orc/core/stages/chroma_sink/decoders/
   ├── decoder.h / decoder.cpp
   ├── decoderpool.h / decoderpool.cpp
   ├── paldecoder.h / paldecoder.cpp
   ├── palcolour.h / palcolour.cpp
   ├── transformpal.h / transformpal.cpp
   ├── transformpal2d.h / transformpal2d.cpp
   ├── transformpal3d.h / transformpal3d.cpp
   ├── ntscdecoder.h / ntscdecoder.cpp
   ├── comb.h / comb.cpp
   ├── monodecoder.h / monodecoder.cpp
   ├── componentframe.h / componentframe.cpp
   ├── framecanvas.h / framecanvas.cpp
   ├── sourcefield.h / sourcefield.cpp
   ├── outputwriter.h / outputwriter.cpp
   └── encoder/
       ├── encoder.h / encoder.cpp
       └── ... (all encoder files)
   ```

2. **Copy files from orc-chroma-decoder:**
   ```bash
   cp -r orc-chroma-decoder/src/*.{h,cpp} \
         orc/core/stages/chroma_sink/decoders/
   # Exclude main.cpp
   rm orc/core/stages/chroma_sink/decoders/main.cpp
   ```

3. **Update includes:**
   - Replace `#include "lddecodemetadata.h"` → `#include "tbc_metadata.h"`
   - Replace `#include "sourcevideo.h"` → Use orc-core's TBC reader classes
   - Replace `#include "logging.h"` → `#include "logging.h"` (orc-core version)
   - Update TBC library class names if needed

4. **Fix Qt6 compatibility:**
   - Verify all Qt includes use Qt6 modules
   - Update deprecated Qt functions if any
   - Check signal/slot connections

5. **Create static library:**
   ```cmake
   # orc/core/stages/chroma_sink/decoders/CMakeLists.txt
   add_library(orc_chroma_decoders STATIC
       decoder.cpp
       decoderpool.cpp
       paldecoder.cpp
       palcolour.cpp
       transformpal.cpp
       transformpal2d.cpp
       transformpal3d.cpp
       ntscdecoder.cpp
       comb.cpp
       monodecoder.cpp
       componentframe.cpp
       framecanvas.cpp
       sourcefield.cpp
       outputwriter.cpp
       encoder/encoder.cpp
       # ... other encoder files
   )
   
   target_link_libraries(orc_chroma_decoders
       PUBLIC Qt6::Core
       PRIVATE PkgConfig::FFTW
               orc_tbc_library
   )
   ```

6. **Link to ChromaSinkStage:**
   ```cmake
   # orc/core/stages/chroma_sink/CMakeLists.txt
   target_link_libraries(chroma_sink_stage
       PRIVATE orc_chroma_decoders
   )
   ```

#### Test Verification

**Build Tests:**
```bash
cd build
cmake .. && make -j$(nproc)
# Library should compile without errors
```

**Standalone Tool Tests:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify
# All 24 tests should still pass (unchanged)
```

**Integration Test:**
```bash
# Verify library is linked
ldd build/orc/gui/orc-gui | grep orc_chroma_decoders
# Or check that stage still loads
orc-gui
```

#### Success Criteria
- ✅ `orc_chroma_decoders` library builds successfully (with Qt6 dependencies)
- ✅ No compilation errors or warnings
- ✅ ChromaSinkStage links correctly
- ✅ Original orc-chroma-decoder tests still pass (unchanged)
- ✅ No runtime behavior changes (decoders not used yet)
- ✅ GUI still works (no crashes)

#### Completion Notes

**Directory Structure Created:**
- `/orc/core/stages/chroma_sink/decoders/` - Main decoder directory
- `/orc/core/stages/chroma_sink/decoders/encoder/` - Encoder subdirectory
- `/orc/core/stages/chroma_sink/decoders/lib/tbc/` - TBC library files
- `/orc/core/stages/chroma_sink/decoders/lib/filter/` - Filter headers

**Files Copied (29 decoder/encoder files):**
- **Decoder core:** decoder.h/cpp, decoderpool.h/cpp
- **PAL decoders:** paldecoder.h/cpp, palcolour.h/cpp, transformpal.h/cpp, transformpal2d.h/cpp, transformpal3d.h/cpp
- **NTSC decoders:** ntscdecoder.h/cpp, comb.h/cpp
- **Mono decoder:** monodecoder.h/cpp
- **Frame handling:** componentframe.h/cpp, framecanvas.h/cpp, sourcefield.h/cpp
- **Output:** outputwriter.h/cpp
- **Encoders:** encoder/encoder.h/cpp, encoder/ntscencoder.h/cpp, encoder/palencoder.h/cpp
- **TBC library (12 files):** lddecodemetadata.h/cpp, dropouts.h/cpp, filters.h/cpp, logging.h/cpp, navigation.h/cpp, sourceaudio.h/cpp, sourcevideo.h/cpp, sqliteio.h/cpp, vbidecoder.h/cpp, videoiddecoder.h/cpp, vitcdecoder.h/cpp, linenumber.h
- **Filter headers:** lib/filter/deemp.h, firfilter.h, iirfilter.h

**Files Created:**
- `/orc/core/stages/chroma_sink/decoders/CMakeLists.txt` - Build configuration for decoder library

**Files Modified:**
- `/orc/core/CMakeLists.txt` - Added decoders subdirectory and linked orc_chroma_decoders
- `/orc/core/stages/chroma_sink/CMakeLists.txt` - No changes needed (linking done at core level)

**Build Configuration:**
- Created `orc_chroma_decoders` static library (3.6 MB)
- Qt6 dependencies: Core, Concurrent, Sql
- System dependencies: SQLite3
- Compile definitions: APP_BRANCH="chroma-decoder-integration", APP_COMMIT="step2"
- Include directories: decoders/, encoder/, lib/tbc/, lib/filter/

**Verification:**
- Build completed successfully without errors
- Library created: `build/lib/liborc_chroma_decoders.a` (3.6 MB)
- All 29 decoder source files compiled
- All 12 TBC library source files compiled
- GUI still launches and functions normally
- ChromaSinkStage still appears in GUI and is fully interactive
- Standalone orc-chroma-decoder tests: 24/24 passed (no regressions)

**Key Decisions:**
- Copied TBC library files directly into decoder directory instead of using separate orc_tbc_library (avoids dependency complexity)
- Added Qt6::Sql module for QSqlDatabase support (required by sqliteio.h)
- Removed orc_tbc_library dependency (self-contained decoder library)
- Kept main.cpp excluded (not needed for library)
- Kept encoder/main.cpp (part of encoder directory structure, not compiled)

**Known State:**
- All decoder code now has Qt6 dependencies (QVector, QString, QThread, etc.)
- Step 3 will remove these Qt6 dependencies and convert to standard C++17
- Decoder classes are available but not yet called from ChromaSinkStage
- Integration will begin in Step 4 after Qt6 → C++ conversion

---

### Step 3: Implement orc-core Integration Layer

**Status:** 🔄 IN PROGRESS (22 Dec 2025)
**Risk:** LOW  
**Duration:** ~4 hours

#### Revised Strategy
**Keep decoder algorithms with Qt6 temporarily, build integration layer to connect orc-core → decoders:**

The decoder algorithms (PAL/NTSC decoding, comb filters, color processing) are complex and tightly coupled with Qt types. Rather than converting them immediately, we'll:

1. **Build ChromaSinkStage integration** that:
   - Receives `VideoFieldRepresentation` from `LdPalSourceStage`/`LdNtscSourceStage`
   - Extracts field data, metadata, hints  
   - Converts to decoder input format (SourceField)
   - Calls decoder algorithms
   - Writes output files

2. **Keep decoder library with Qt6** for now:
   - All 29 decoder files remain Qt6-based
   - Continue using existing decoderpool threading
   - Works with TBC library and LdDecodeMetaData

3. **Qt6 removal deferred** to later phase:
   - Once integration works and tests pass
   - Can incrementally convert decoder internals
   - Not blocking for initial functionality

#### Integration Architecture

```
orc-cli/orc-gui
    ↓
LdPalSourceStage/LdNtscSourceStage (reads TBC files)
    ↓ (VideoFieldRepresentation with field data + metadata)
ChromaSinkStage::trigger()
    ↓
[Extract & convert] → SourceField format
    ↓
Decoder algorithms (Qt6-based, existing code)
    ↓
Write output file (RGB/YUV/Y4M)
```

#### Implementation Steps

1. **Create adapter layer** in ChromaSinkStage:
   ```cpp
   // Convert VideoFieldRepresentation → SourceField
   SourceField convertField(const VideoFieldRepresentation& vfr) {
       // Extract sample data from vfr
       // Create SourceField with metadata
   }
   ```

2. **Implement ChromaSinkStage::trigger()**:
   ```cpp
   void ChromaSinkStage::trigger(...) override {
       // Get input fields from upstream LD source stages
       auto field1 = getInputField(0);  // VideoFieldRepresentation
       auto field2 = getInputField(1);
       
       // Convert to decoder format
       SourceField sf1 = convertField(field1);
       SourceField sf2 = convertField(field2);
       
       // Create decoder (PAL/NTSC based on config)
       decoder->decodeFrame(sf1, sf2, outputFrame);
       
       // Write output
       outputWriter->writeFrame(outputFrame);
   }
   ```

3. **Test with orc-cli**:
   ```bash
   # Create project: PAL source → chroma sink
   orc-cli run test-chroma-decode.orcprj
   ```

4. **Verify output** matches standalone orc-chroma-decoder tool

#### Test Infrastructure Created ✅

**ORC Project Files:**
- `test-projects/chroma-test-pal.orcprj` - PAL source → chroma sink
- `test-projects/chroma-test-ntsc.orcprj` - NTSC source → chroma sink

**Test Script:**
- `orc/core/stages/chroma_sink/tests/test-orc-chroma.sh` - Automated test runner
  - Dynamically generates project files for each test
  - Tests PAL decoders: pal2d, transform2d, transform3d
  - Tests NTSC decoders: ntsc2d, ntsc3d
  - Tests output formats: RGB, YUV, Y4M
  - Compares output signatures with standalone orc-chroma-decoder
  - Mirrors existing `orc-chroma-decoder/tests/run-tests.sh` structure

**Integration Implementation Status:** ✅ **WORKING** (with caveats)

[chroma_sink_stage.cpp](orc/core/stages/chroma_sink/chroma_sink_stage.cpp) implementation complete:

**Core Functionality (COMPLETE):**
- ✅ `trigger()` method fully implemented (~150 lines)
  - Extracts VideoFieldRepresentation from DAG inputs
  - Converts orc::VideoParameters → LdDecodeMetaData::VideoParameters
  - Maps video system, sample rates, color subcarrier frequency
  - Calculates frame range from start_frame/length parameters
  - Collects all input fields for batch processing
  
- ✅ `convertToSourceField()` adapter working (~35 lines)
  - Converts VideoFieldRepresentation fields to SourceField format
  - Maps FieldParity (Top/Bottom) to isFirstField boolean
  - Extracts field data: std::vector<uint16_t> → QVector<quint16>
  - Sets sequence numbers and field metadata
  
- ✅ `writeOutputFile()` fully functional (~60 lines)
  - Uses OutputWriter API for format conversion
  - Supports RGB48, YUV444P16, Y4M formats
  - Writes stream/frame headers for Y4M
  - Converts ComponentFrame → OutputFrame → binary file
  
- ✅ Decoder instantiation working
  - MonoDecoder: MonoConfiguration with yNRLevel
  - PalColour: Direct instantiation (bypasses PalDecoder wrapper)
  - Comb: Direct instantiation (bypasses NtscDecoder wrapper)
  - Uses algorithm classes directly (not Decoder base class)
  
**Build System Fixes (COMPLETE):**
- ✅ CMake: AUTOMOC enabled for decoder library (Qt MOC compilation)
- ✅ CMake: FFTW3 linked to orc-cli and orc-gui (Transform2D/3D support)
- ✅ Includes: Using algorithm classes directly (palcolour.h, comb.h, monodecoder.h)

**Video Parameters Mapping (COMPLETE):**
- ✅ Basic fields: fieldWidth, fieldHeight, sampleRate, fSC
- ✅ Color burst: colourBurstStart, colourBurstEnd
- ✅ IRE levels: white16bIre, black16bIre
- ✅ Active region: activeVideoStart, activeVideoEnd
- ✅ **Active line ranges:** firstActiveFieldLine, lastActiveFieldLine
- ✅ **Frame line calculation:** LineParameters::applyTo() for defaults
  - PAL: 44-620 frame lines (from 22-308 field lines)
  - NTSC: 40-525 frame lines (from 20-259 field lines)
  
**Frame Processing (COMPLETE):**
- ✅ Frame-to-field ID mapping: frame N → fields N*2, N*2+1
- ✅ Field parity detection: Top=isFirstField, Bottom=!isFirstField
- ✅ Batch processing: collect all fields, decode all frames, write output
- ✅ Synchronous decoder invocation: PalColour::decodeFrames(), Comb::decodeFrames(), MonoDecoder::decodeFrames()

**Output Generation (WORKING):**
- ✅ **File created:** 32,071,680 bytes (31 MB for 10 frames PAL RGB48)
- ✅ **Dimensions:** 928x576 RGB48 (matches standalone: "Input video of 1135 x 625 will be colourised and trimmed to 928 x 576 RGB48 frames")
- ✅ **Format:** Raw RGB48 data (16-bit per channel)
- ✅ **Structure:** Valid output accepted by video players

**Current Status:**
- ✅ **All tests passing:** 24/24 decoder tests produce pixel-perfect output
  - PAL decoders: 2D, Transform2D, Transform3D (all formats: RGB, YUV, Y4M)
  - NTSC decoders: 1D, 2D, 3D, 3D-NoAdapt (all formats)
  - Edge cases: Reverse fields, custom padding, custom line ranges
  - All output checksums match standalone decoder exactly

**Critical Bug Fixed (22 Dec 2025):**
- **Issue:** Transform PAL 2D/3D decoders produced incorrect output (wrong checksums)
- **Root Cause:** activeVideoStart/activeVideoEnd values need padding adjustment BEFORE decoder initialization
  - Original values from database: activeVideoStart=185, activeVideoEnd=1107 (width=922)
  - OutputWriter applies padding to make width divisible by 8: activeVideoStart=182, activeVideoEnd=1110 (width=928)
  - Decoder was initialized with original values (185/1107) but OutputWriter used adjusted values (182/1110)
  - Transform PAL's FFT tiles were processing wrong region → different pixel values
- **Solution:** Apply OutputWriter padding adjustments to VideoParameters BEFORE configuring decoder
  ```cpp
  // Apply padding adjustments BEFORE configuring decoder
  {
      OutputWriter::Configuration writerConfig;
      writerConfig.paddingAmount = 8;
      OutputWriter tempWriter;
      tempWriter.updateConfiguration(ldVideoParams, writerConfig);
      // ldVideoParams now has adjusted activeVideoStart/End values
  }
  // NOW configure decoder with adjusted parameters
  palDecoder->updateConfiguration(ldVideoParams, config);
  ```
- **Impact:** All Transform PAL tests now pass with pixel-perfect output

#### Success Criteria
- ✅ Test infrastructure created (ORC projects + test script)
- ✅ ChromaSinkStage::trigger() implemented and working
- ✅ All helper methods implemented
- ✅ Synchronous decoder invocation (PalColour/Comb/MonoDecoder)
- ✅ First successful decode (PAL RGB48)
- ✅ Output file written correctly (31 MB, 928x576 frames)
- ✅ Dimensions match standalone tool
- ✅ Output pixel-perfect identical to standalone tool
- ✅ All 24 decoder tests passing with exact checksum matches

#### Completion Summary

**Step 3 Status:** ✅ **COMPLETE** (22 Dec 2025)

The integration layer is fully working and passes all tests. The ChromaSinkStage successfully:

1. **Converts orc-core data → decoder format:** VideoFieldRepresentation → SourceField adapter working perfectly
2. **Applies video parameters correctly:** Active regions, line ranges, field parity, and padding all calculated properly
3. **Processes all decoder types:** PAL (2D, Transform2D, Transform3D), NTSC (1D, 2D, 3D, 3D-NoAdapt), Mono
4. **Handles all output formats:** RGB48, YUV444P16, Y4M with correct headers and frame structure
5. **Produces pixel-perfect output:** All 24 test signatures match standalone decoder exactly

**Test Results (22 Dec 2025):**
```
Mode:    verify
Total:   24
Passed:  24
Failed:  0
Skipped: 0
[PASS] All tests passed!
```

**Test Coverage:**
- PAL decoders: 9 tests (basic, transform2d, transform3d, formats, parameters)
- NTSC decoders: 10 tests (1d, 2d, 3d variants, formats, parameters)  
- Edge cases: 5 tests (CAV format, reverse fields, padding, custom lines)

**Critical Issue Resolved:**
The major blocker was a subtle bug where `activeVideoStart` and `activeVideoEnd` values needed to be adjusted for padding alignment BEFORE the decoder was initialized. The OutputWriter modifies these values to ensure output width is divisible by the padding factor (8 pixels), but the decoder needs to process the SAME region that gets written to output. This was particularly critical for Transform PAL decoders which use FFT-based processing on specific tile regions.

**Debugging Methodology:**
- Used execution tracing to compare ORC vs standalone decoder behavior
- Added strategic debug output to compare Transform PAL configuration values
- Discovered 3-pixel offset in first FFT tile position (tileX: 169 vs 166)
- Traced back to activeVideoStart difference (185 vs 182)
- Found OutputWriter padding logic was applied AFTER decoder initialization
- Fixed by applying padding adjustments before decoder configuration

**Files Created/Modified:**
- `orc/core/stages/chroma_sink/chroma_sink_stage.cpp` - Full trigger() implementation (~750 lines)
  - Video parameter conversion from orc-core → ld-decode format
  - **CRITICAL:** Padding adjustment applied before decoder initialization
  - Field collection and conversion to SourceField format
  - Field ordering detection using FieldParityHint
  - Proper field pairing algorithm (handles is_first_field_first flag)
  - Synchronous decoder invocation (all decoder types)
  - OutputWriter integration with all formats
- `orc/core/stages/chroma_sink/decoders/CMakeLists.txt` - Added AUTOMOC, FFTW3
- `orc/cli/CMakeLists.txt` - Added FFTW3 linkage
- `orc/gui/CMakeLists.txt` - Added FFTW3 linkage
- `test-projects/chroma-test-pal.orcprj` - PAL test project (Transform2D)
- `test-projects/chroma-test-ntsc.orcprj` - NTSC test project
- `orc/core/include/tbc_metadata.h` - Added is_first_field_first flag

**Ready for Step 4:** Yes (Qt6 removal can proceed with confidence)

**Key Learnings:**
1. **Order of operations matters:** Padding adjustments must happen before decoder initialization
2. **Region alignment is critical:** FFT-based decoders process specific tiles that must match output region
3. **Debugging approach:** Execution tracing and watchpoints were invaluable for comparing decoder behavior
4. **Test-driven development:** Having comprehensive test suite caught subtle bugs immediately
5. **Field ordering complexity:** Need to handle both field parity AND first/second field ordering from metadata

#### Next Steps
1. Implement ChromaSinkStage::trigger() to:
   - Extract VideoFieldRepresentation from inputs
   - Convert to SourceField format (adapter layer)
   - Call existing Qt6-based decoder algorithms
   - Write output files
2. Run test suite and fix issues
3. Verify output matches standalone tool exactly

#### Future Work (Post-Integration)
- Incrementally convert decoder algorithms from Qt6 to C++17
- Replace decoderpool with DAG executor parallelism
- Replace TBC library with orc-core metadata structures

---

### Step 4: Remove Qt6 Dependencies from Decoder Algorithms

**Status:** Not Started (DEFERRED until integration works)
**Risk:** MEDIUM-HIGH  
**Duration:** ~12-16 hours

#### Why This Step Was Deferred

Initial attempts to convert Qt6 → C++17 revealed tight coupling between:
- Decoder algorithms (paldecoder, ntscdecoder, comb, palcolour, transforms)
- I/O layer (decoderpool, sourcevideo, outputwriter)
- Threading infrastructure (QThread, QAtomicInt, QMutex)
- Data types (QVector, QString, qint32)

Converting everything at once was error-prone. Better to:
1. **First:** Get integration working (Step 3) with Qt6 decoders
2. **Then:** Systematically remove Qt6 (this step)
3. **Verify:** Output signatures match throughout

This ensures we have a working baseline to test against.

#### Conversion Strategy

**Phase A: Data Types & Containers** (~3 hours)
- Convert all decoder algorithm files:
  ```
  qint32 → int32_t
  quint32 → uint32_t
  qint16 → int16_t
  QVector<T> → std::vector<T>
  QString → std::string
  ```
- Update includes:
  ```cpp
  #include <QtGlobal> → #include <cstdint>
  #include <QVector> → #include <vector>
  #include <QString> → #include <string>
  ```
- Replace Qt methods:
  ```cpp
  .fill(val) → std::fill(vec.begin(), vec.end(), val)
  .squeeze() → shrink_to_fit()
  ```

**Phase B: Threading & Synchronization** (~4 hours)
- Remove DecoderPool (replaced by orc-core DAG executor)
- Remove DecoderThread/QThread infrastructure
- Convert remaining synchronization:
  ```cpp
  QAtomicInt → std::atomic<int>
  QMutex → std::mutex
  QMutexLocker → std::lock_guard<std::mutex>
  ```

**Phase C: Algorithm Classes** (~4 hours)
- Convert decoder algorithm classes (no I/O dependencies):
  - `componentframe.h/cpp` - Frame buffer
  - `paldecoder.h/cpp` - PAL decoding
  - `ntscdecoder.h/cpp` - NTSC decoding
  - `monodecoder.h/cpp` - Mono decoder
  - `palcolour.h/cpp` - PAL color processing
  - `comb.h/cpp` - NTSC comb filters
  - `transformpal.h/cpp` - Transform PAL base
  - `transformpal2d.h/cpp` - 2D transform
  - `transformpal3d.h/cpp` - 3D transform

**Phase D: Metadata & Parameters** (~3 hours)
- Replace `LdDecodeMetaData::VideoParameters` with orc-core types
- Replace `LdDecodeMetaData::Field` with orc-core FieldMetadata
- Remove TBC library dependencies from algorithm code

**Phase E: Build System** (~2 hours)
- Update CMakeLists.txt to remove Qt6 dependencies:
  ```cmake
  # Remove:
  find_package(Qt6 COMPONENTS Core Concurrent Sql REQUIRED)
  target_link_libraries(orc_chroma_decoders Qt6::Core Qt6::Concurrent Qt6::Sql)
  
  # Keep only:
  target_link_libraries(orc_chroma_decoders SQLite::SQLite3)
  ```
- Split library if needed:
  - `orc_chroma_algorithms` - Pure C++17, no Qt
  - `orc_chroma_io` - Qt6 for legacy file I/O (if still needed)

#### Files to Convert (29 total)

**Algorithm Core (Qt removal required):**
- componentframe.h/cpp
- paldecoder.h/cpp
- ntscdecoder.h/cpp
- monodecoder.h/cpp
- palcolour.h/cpp
- comb.h/cpp
- transformpal.h/cpp
- transformpal2d.h/cpp
- transformpal3d.h/cpp

**I/O Layer (will be replaced, low priority):**
- decoder.h/cpp - Base class with QThread
- decoderpool.h/cpp - Threading pool
- sourcefield.h/cpp - Field loading
- outputwriter.h/cpp - File output
- framecanvas.h/cpp - Canvas utilities

**TBC Library (keep Qt for now, used by metadata):**
- lib/tbc/lddecodemetadata.h/cpp
- lib/tbc/sourcevideo.h/cpp
- lib/tbc/*.h/cpp (12 files)

#### Testing Strategy

After each phase:
1. Build orc_chroma_decoders library
2. Run `orc/core/stages/chroma_sink/tests/test-orc-chroma.sh`
3. Verify output signatures still match standalone tool
4. Fix any compilation errors or test failures
5. Commit working state before next phase

#### Success Criteria
- ✅ All decoder algorithm files compile without Qt6
- ✅ `orc_chroma_decoders` library no longer links Qt6::Core
- ✅ Test suite passes with matching output signatures
- ✅ No Qt types in algorithm class interfaces
- ✅ Ready for deeper orc-core integration (metadata, hints, observers)

#### Blockers & Dependencies
- **Requires:** Step 3 completed (working integration with Qt6 decoders)
- **Requires:** Test suite passing and generating valid signatures
- **Risk:** Breaking output signature match during conversion
- **Mitigation:** Convert incrementally, test after each phase

---

### Step 5: Deeper orc-core Integration

**Status:** Not Started (After Step 4)
**Risk:** Low-Medium
**Duration:** ~6 hours

#### Objectives

Once Qt6 is removed from decoder algorithms (Step 4), integrate more deeply with orc-core:
- Replace SourceField with direct VideoFieldRepresentation access
- Use orc-core metadata and hints instead of LdDecodeMetaData
- Leverage orc-core's observer system for dropout detection
- Use DAG executor parallelism instead of custom threading

#### Actions

1. **Replace SourceField wrapper** with direct VFR access
2. **Use orc-core metadata:**
   - `VideoFieldRepresentation::get_dropout_hints()` → dropout detection
   - `VideoFieldRepresentation::get_field_parity_hint()` → field order
   - `VideoFieldRepresentation::get_field_phase_hint()` → PAL phase
3. **Remove TBC library dependency** from decoder algorithms
4. **Use DAG executor** for parallel field processing

---

## Implementation Details (Post-Integration)
   # Create checklist of all files that need conversion
   cd orc/core/stages/chroma_sink/decoders
   grep -r "Q[A-Z]" . | cut -d: -f1 | sort -u > qt-usage.txt
   ```

2. **Convert data types (systematic approach):**
   ```cpp
   // Example conversions:
   
   // BEFORE (Qt6):
   QVector<SourceField> fields;
   fields.resize(10);
   
   // AFTER (C++):
   std::vector<SourceField> fields;
   fields.resize(10);
   
   // BEFORE:
   QString filename = "test.tbc";
   
   // AFTER:
   std::string filename = "test.tbc";
   
   // BEFORE:
   QByteArray data(1024);
   
   // AFTER:
   std::vector<uint8_t> data(1024);
   
   // BEFORE:
   qint32 frameNumber;
   
   // AFTER:
   int32_t frameNumber;
   ```

3. **Convert threading primitives:**
   ```cpp
   // BEFORE (Qt6):
   class DecoderThread : public QThread {
       Q_OBJECT
   public:
       void run() override {
           // processing
       }
   };
   
   QAtomicInt abort;
   QMutex inputMutex;
   QElapsedTimer timer;
   timer.start();
   int elapsed = timer.elapsed();
   
   // AFTER (C++):
   class DecoderThread {
   public:
       void run() {
           // processing
       }
       
       std::thread thread_;
   };
   
   std::atomic<bool> abort{false};
   std::mutex input_mutex;
   auto start_time = std::chrono::steady_clock::now();
   auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
       std::chrono::steady_clock::now() - start_time
   ).count();
   ```

4. **Convert file I/O:**
   ```cpp
   // BEFORE (Qt6):
   QFile file(filename);
   if (!file.open(QIODevice::WriteOnly)) {
       qCritical() << "Failed to open file";
       return false;
   }
   file.write(data.constData(), data.size());
   file.close();
   
   // AFTER (C++):
   std::ofstream file(filename, std::ios::binary);
   if (!file.is_open()) {
       ORC_LOG_ERROR("Failed to open file: {}", filename);
       return false;
   }
   file.write(reinterpret_cast<const char*>(data.data()), data.size());
   file.close();
   ```

5. **Convert logging:**
   ```cpp
   // BEFORE (Qt6):
   qDebug() << "Processing frame" << frameNumber;
   qInfo() << "Processing complete";
   qWarning() << "Warning: " << message;
   qCritical() << "Error: " << errorMessage;
   
   // AFTER (C++):
   ORC_LOG_DEBUG("Processing frame {}", frameNumber);
   ORC_LOG_INFO("Processing complete");
   ORC_LOG_WARN("Warning: {}", message);
   ORC_LOG_ERROR("Error: {}", errorMessage);
   ```

6. **Remove Qt meta-object system:**
   ```cpp
   // BEFORE:
   class DecoderThread : public QThread {
       Q_OBJECT  // REMOVE
   public:
   signals:     // REMOVE
       void finished();
   ```
   
   Use callbacks/std::function instead of signals/slots

7. **Update CMakeLists.txt:**
   ```cmake
   # BEFORE:
   target_link_libraries(orc_chroma_decoders
       PUBLIC Qt6::Core
       PRIVATE PkgConfig::FFTW
               orc_tbc_library
   )
   
   # AFTER:
   target_link_libraries(orc_chroma_decoders
       PUBLIC orc_tbc_library
       PRIVATE PkgConfig::FFTW
   )
   # No more Qt6 dependency!
   ```

8. **File-by-file conversion order:**
   
   **Phase 1: Core data structures (no dependencies)**
   1. `componentframe.h/cpp` - Just data, minimal Qt
   2. `framecanvas.h/cpp` - Data structure
   3. `sourcefield.h/cpp` - Field data structure
   
   **Phase 2: Processing code**
   4. `outputwriter.h/cpp` - Output format conversion
   5. `encoder/*.cpp` - RGB/YUV encoders
   6. `palcolour.h/cpp` - PAL color processing
   7. `comb.h/cpp` - NTSC comb filter
   8. `transformpal*.cpp` - Transform PAL decoders
   
   **Phase 3: Decoder classes**
   9. `decoder.h/cpp` - Base decoder class
   10. `monodecoder.h/cpp` - Simplest decoder
   11. `paldecoder.h/cpp` - PAL decoder
   12. `ntscdecoder.h/cpp` - NTSC decoder
   
   **Phase 4: Threading (most complex)**
   13. `decoderpool.h/cpp` - Thread pool and coordination

9. **Create helper utilities:**
   ```cpp
   // orc/core/stages/chroma_sink/decoders/compat.h
   // Helper functions to ease transition
   
   namespace compat {
       // String utilities
       inline std::vector<std::string> split(const std::string& s, char delim) {
           std::vector<std::string> result;
           std::stringstream ss(s);
           std::string item;
           while (std::getline(ss, item, delim)) {
               result.push_back(item);
           }
           return result;
       }
       
       // Number to string
       template<typename T>
       inline std::string to_string(T value) {
           return std::to_string(value);
       }
   }
   ```

#### Test Verification

**Compilation tests:**
```bash
cd build
cmake .. && make -j$(nproc)
# Should compile without Qt6 dependency
# Check that Qt6 is not linked:
ldd build/orc/core/liborc_chroma_decoders.a | grep -i qt
# Should be empty
```

**Standalone tool still works:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify
# All 24 tests should STILL pass
# (Standalone tool still uses Qt6, but shared decoder code doesn't)
```

**Verify no Qt symbols in decoder library:**
```bash
nm build/orc/core/stages/chroma_sink/decoders/liborc_chroma_decoders.a | grep -i qt
# Should be empty or minimal
```

#### Common Pitfalls

1. **QString implicit conversions:**
   - Qt: `QString` automatically converts to/from `const char*`
   - C++: `std::string` needs explicit `.c_str()` or constructors

2. **QVector::data() vs std::vector::data():**
   - Both exist, but be careful with const-correctness

3. **Thread lifecycle:**
   - Qt: QThread manages its own lifetime, can be deleted
   - C++: Must call `thread.join()` or `thread.detach()` before destruction

4. **Signal/slot patterns:**
   - Replace with callbacks: `std::function<void()>`
   - Or use observer pattern

5. **File path handling:**
   - Qt: QString handles paths
   - C++: Use `std::filesystem::path` (C++17)

#### Incremental Testing Strategy

After converting each file:
1. Compile decoder library
2. Link to ChromaSinkStage (even if not used yet)
3. Verify standalone tool still works
4. Run quick smoke test

**Don't proceed to Step 4 until all Qt6 is removed!**

#### Success Criteria
- ✅ No Qt6 includes in decoder headers
- ✅ No Qt6 symbols in decoder library
- ✅ All decoders compile without Qt6
- ✅ Standalone tool still passes all tests (it still uses Qt6 wrapper)
- ✅ Thread pool uses std::thread
- ✅ All data types are standard C++
- ✅ Logging uses orc-core macros
---

## Detailed Implementation Notes (Reference)

The sections below provide detailed implementation guidance that can be referenced during Steps 3-5.

---

### Implementation: Basic Mono Decoder in trigger()

**Status:** Not Started  
**Risk:** Medium  
**Duration:** ~6 hours

#### Objectives
Implement end-to-end decode pipeline for simplest decoder (mono/black-and-white). Establish data conversion patterns.

#### Actions

1. **Implement data conversion helpers:**
   ```cpp
   // In chroma_sink_stage.cpp
   
   // Convert VideoFieldRepresentation to SourceField
   SourceField convert_to_source_field(
       const VideoFieldRepresentation& vfr,
       const FieldID& field_id
   ) {
       // Extract field data from VFR
       // Create SourceField with appropriate metadata
   }
   
   // Write ComponentFrame to output file
   bool write_output_frame(
       const ComponentFrame& frame,
       QFile& output_file,
       const OutputWriter& writer
   ) {
       // Convert to OutputFrame
       // Write to file
   }
   ```

2. **Implement trigger() for mono decoder:**
   ```cpp
   bool ChromaSinkStage::trigger(
       const std::vector<ArtifactPtr>& inputs,
       const std::map<std::string, ParameterValue>& parameters
   ) {
       // 1. Extract VideoFieldRepresentation
       auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
       if (!vfr) {
           ORC_LOG_ERROR("Chroma sink requires VideoFieldRepresentation input");
           return false;
       }
       
       // 2. Get parameters
       std::string output_path = get_string_param(parameters, "output_path");
       int start_frame = get_int_param(parameters, "start_frame", 1);
       int length = get_int_param(parameters, "length", -1);
       
       // 3. Open output file
       QFile output_file(QString::fromStdString(output_path));
       if (!output_file.open(QIODevice::WriteOnly)) {
           ORC_LOG_ERROR("Failed to open output file: {}", output_path);
           return false;
       }
       
       // 4. Configure decoder
       MonoDecoder decoder;
       MonoDecoder::MonoConfiguration config;
       config.videoParameters = vfr->get_video_parameters();
       if (!decoder.configure(config.videoParameters)) {
           ORC_LOG_ERROR("Failed to configure mono decoder");
           return false;
       }
       
       // 5. Configure output writer
       OutputWriter writer;
       OutputWriter::Configuration output_config;
       output_config.pixelFormat = OutputWriter::RGB48;
       writer.updateConfiguration(config.videoParameters, output_config);
       
       // 6. Write stream header
       output_file.write(writer.getStreamHeader());
       
       // 7. Process frames (single-threaded for now)
       int frames_to_process = (length > 0) ? length : vfr->get_total_frames();
       for (int frame = start_frame; frame < start_frame + frames_to_process; frame++) {
           // Get two fields for this frame
           QVector<SourceField> fields;
           for (int field_num = 0; field_num < 2; field_num++) {
               FieldID field_id(frame, field_num);
               fields.push_back(convert_to_source_field(*vfr, field_id));
           }
           
           // Decode to component frame
           QVector<ComponentFrame> component_frames;
           decoder.decodeFrame(fields, 0, 1, component_frames);
           
           // Convert to output format
           OutputFrame output_frame;
           writer.convert(component_frames[0], output_frame);
           
           // Write frame header and data
           output_file.write(writer.getFrameHeader());
           output_file.write(reinterpret_cast<const char*>(output_frame.data()),
                            output_frame.size() * sizeof(quint16));
       }
       
       output_file.close();
       
       trigger_status_ = QString("Exported %1 frames to %2")
                        .arg(frames_to_process)
                        .arg(output_path);
       
       return true;
   }
   ```

3. **Implement conversion functions:**
   - Map orc-core `VideoFieldRepresentation` to legacy `SourceField`
   - Handle field data buffers
   - Convert metadata structures

4. **Handle parameter extraction:**
   - Add helper functions for type-safe parameter access
   - Validate parameter ranges
   - Provide sensible defaults

#### Test Verification

**Create test project:**
```bash
# Create minimal .orcprj file for testing
cat > test-chroma-mono.orcprj << 'EOF'
{
  "nodes": [
    {
      "id": "source1",
      "stage_name": "ld_pal_source",
      "parameters": {
        "tbc_path": "/path/to/test-data/laserdisc/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc"
      }
    },
    {
      "id": "sink1",
      "stage_name": "chroma_sink",
      "parameters": {
        "output_path": "/tmp/test-mono-output.rgb",
        "decoder_type": "mono",
        "output_format": "rgb",
        "start_frame": 1,
        "length": 10
      }
    }
  ],
  "connections": [
    {"from": "source1:0", "to": "sink1:0"}
  ]
}
EOF
```

**Trigger export:**
```bash
# Via CLI or programmatically trigger the sink
# Expected: Creates /tmp/test-mono-output.rgb

# Verify output
sha256sum /tmp/test-mono-output.rgb
# Compare with orc-chroma-decoder mono output signature
```

**Modify test suite:**
```bash
# Add orc-core testing mode to run-tests.sh
# Temporarily run only mono test:
cd orc-chroma-decoder/tests
./run-tests.sh verify -f MONO
```

#### Success Criteria
- ✅ Mono decoder produces output file
- ✅ Output file signature matches baseline (test suite)
- ✅ No crashes during processing
- ✅ Progress logging works
- ✅ File I/O is correct (size, content)
- ✅ Start frame and length parameters respected

---

### Implementation: Add PAL 2D Decoder

**Status:** Not Started  
**Risk:** Medium  
**Duration:** ~4 hours

#### Objectives
Add real chroma decoding for PAL video. Implement auto-detection logic.

#### Actions

1. **Implement decoder selection:**
   ```cpp
   std::unique_ptr<Decoder> ChromaSinkStage::create_decoder(
       const VideoFieldRepresentation& vfr,
       const std::string& decoder_type
   ) {
       auto video_params = vfr.get_video_parameters();
       
       // Auto-detect if requested
       if (decoder_type == "auto") {
           if (video_params.system == PAL) {
               return std::make_unique<PALDecoder>();
           } else if (video_params.system == NTSC) {
               return std::make_unique<NtscDecoder>();
           } else {
               return std::make_unique<MonoDecoder>();
           }
       }
       
       // Manual selection
       if (decoder_type == "pal2d") {
           return std::make_unique<PALDecoder>();
       } else if (decoder_type == "mono") {
           return std::make_unique<MonoDecoder>();
       }
       // ... other decoders
   }
   ```

2. **Add PAL-specific parameters:**
   - `chroma_gain` (already defined)
   - `chroma_phase` (already defined)
   - `luma_nr` - Luma noise reduction (dB)

3. **Configure PALDecoder:**
   ```cpp
   PalColour::Configuration pal_config;
   pal_config.chromaGain = get_double_param(parameters, "chroma_gain", 1.0);
   pal_config.chromaPhase = get_double_param(parameters, "chroma_phase", 0.0);
   pal_config.yNRLevel = get_double_param(parameters, "luma_nr", 0.0);
   
   auto decoder = std::make_unique<PALDecoder>();
   decoder->setPalConfig(pal_config);
   ```

4. **Update trigger() to use decoder factory:**
   ```cpp
   auto decoder = create_decoder(*vfr, decoder_type);
   if (!decoder->configure(video_params)) {
       // Error handling
   }
   ```

#### Test Verification

**Run PAL tests:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify -f PAL
```

**Expected results:**
- PAL_2D_RGB: ✅ Signature matches
- PAL_2D_YUV: ✅ Signature matches
- PAL_2D_Y4M: ✅ Signature matches
- PAL_2D_Mono: ✅ Signature matches
- PAL_2D_ChromaGain: ✅ Signature matches
- PAL_2D_ChromaPhase: ✅ Signature matches

#### Success Criteria
- ✅ PAL decoder produces correct output
- ✅ All PAL 2D tests pass
- ✅ Chroma gain/phase parameters work
- ✅ Auto-detection selects PAL decoder for PAL video
- ✅ Output formats (RGB, YUV, Y4M) all work

---

### Implementation: Add NTSC Decoders

**Status:** Not Started  
**Risk:** Medium  
**Duration:** ~5 hours

#### Objectives
Complete NTSC support with all decoder variants and NTSC-specific features.

#### Actions

1. **Add NTSC decoder types to factory:**
   ```cpp
   if (decoder_type == "ntsc1d") {
       auto decoder = std::make_unique<NtscDecoder>();
       decoder->setDecoder(NtscDecoder::Decoder1D);
       return decoder;
   } else if (decoder_type == "ntsc2d") {
       auto decoder = std::make_unique<NtscDecoder>();
       decoder->setDecoder(NtscDecoder::Decoder2D);
       return decoder;
   } else if (decoder_type == "ntsc3d") {
       auto decoder = std::make_unique<NtscDecoder>();
       decoder->setDecoder(NtscDecoder::Decoder3D);
       return decoder;
   } else if (decoder_type == "ntsc3dnoadapt") {
       auto decoder = std::make_unique<NtscDecoder>();
       decoder->setDecoder(NtscDecoder::Decoder3DNoAdapt);
       return decoder;
   }
   ```

2. **Add NTSC-specific parameters:**
   - `chroma_nr` (DOUBLE) - Chroma noise reduction (dB), range 0-10
   - `luma_nr` (DOUBLE) - Luma noise reduction (dB), range 0-10
   - `ntsc_phase_comp` (BOOLEAN) - Phase compensation
   - `show_adaptive_map` (BOOLEAN) - Debug overlay (test mode)

3. **Configure Comb filter:**
   ```cpp
   Comb::Configuration comb_config;
   comb_config.chromaGain = get_double_param(parameters, "chroma_gain", 1.0);
   comb_config.chromaPhase = get_double_param(parameters, "chroma_phase", 0.0);
   comb_config.cNRLevel = get_double_param(parameters, "chroma_nr", 0.0);
   comb_config.yNRLevel = get_double_param(parameters, "luma_nr", 0.0);
   comb_config.phaseCompensation = get_bool_param(parameters, "ntsc_phase_comp", false);
   comb_config.showMap = get_bool_param(parameters, "show_adaptive_map", false);
   
   ntsc_decoder->setCombConfig(comb_config);
   ```

4. **Handle 3D decoder lookbehind/lookahead:**
   - 3D decoders need temporal context (previous/next frames)
   - Implement frame buffering in trigger()
   - Provide dummy frames at boundaries

#### Test Verification

**Run NTSC tests:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify -f NTSC
```

**Expected results:**
- NTSC_1D_RGB: ✅
- NTSC_2D_RGB: ✅
- NTSC_3D_RGB: ✅
- NTSC_3D_NoAdapt_RGB: ✅
- NTSC_2D_YUV: ✅
- NTSC_2D_Y4M: ✅
- NTSC_2D_Mono: ✅
- NTSC_2D_ChromaNR: ✅
- NTSC_2D_LumaNR: ✅
- NTSC_2D_PhaseComp: ✅

#### Success Criteria
- ✅ All NTSC decoder variants work
- ✅ All NTSC tests pass
- ✅ Chroma/luma noise reduction works
- ✅ Phase compensation works
- ✅ 3D decoders handle temporal context correctly
- ✅ Auto-detection selects appropriate NTSC decoder

---

### Implementation: Add Transform PAL Decoders

**Status:** Not Started  
**Risk:** Medium  
**Duration:** ~5 hours

#### Objectives
Implement advanced frequency-domain PAL decoders (Transform 2D/3D).

#### Actions

1. **Add Transform decoder types:**
   ```cpp
   else if (decoder_type == "transform2d") {
       return std::make_unique<TransformPal2D>();
   } else if (decoder_type == "transform3d") {
       return std::make_unique<TransformPal3D>();
   }
   ```

2. **Add Transform-specific parameters:**
   - `simple_pal` (BOOLEAN) - Use 1D UV filter instead of 2D
   - `transform_threshold` (DOUBLE) - Similarity threshold (0-1), default 0.4
   - `transform_thresholds_file` (FILE_PATH) - Per-bin thresholds file
   - `show_ffts` (BOOLEAN) - Debug overlay

3. **Configure Transform PAL:**
   ```cpp
   PalColour::Configuration pal_config;
   // ... basic config
   pal_config.simplePAL = get_bool_param(parameters, "simple_pal", false);
   pal_config.transformThreshold = get_double_param(parameters, "transform_threshold", 0.4);
   pal_config.showFFTs = get_bool_param(parameters, "show_ffts", false);
   
   // Load thresholds file if specified
   if (has_param(parameters, "transform_thresholds_file")) {
       std::string file = get_string_param(parameters, "transform_thresholds_file");
       pal_config.transformThresholds = load_thresholds_file(file);
   }
   
   transform_decoder->setPalConfig(pal_config);
   ```

4. **Verify FFTW3 linkage:**
   - Ensure FFTW3 library is properly linked
   - Check that FFT plans are created correctly
   - Handle FFT cleanup on decoder destruction

5. **Handle 3D decoder temporal context:**
   - Transform3D needs lookbehind/lookahead
   - Similar to NTSC 3D implementation

#### Test Verification

**Run Transform PAL tests:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify -f Transform
```

**Expected results:**
- PAL_Transform2D_RGB: ✅
- PAL_Transform3D_RGB: ✅
- PAL_Transform2D_Simple: ✅

#### Success Criteria
- ✅ Transform 2D decoder works
- ✅ Transform 3D decoder works
- ✅ All Transform tests pass
- ✅ Simple PAL mode works
- ✅ Threshold configuration works
- ✅ FFT operations are correct
- ✅ No memory leaks in FFT code

---

### Implementation: Multi-Threading

**Status:** Not Started  
**Risk:** High  
**Duration:** ~8 hours

#### Objectives
Enable parallel processing for performance. Maintain output determinism.

#### Actions

1. **Assess orc-core threading model:**
   - Does orc-core have a thread pool?
   - What threading primitives are available?
   - How do other stages handle threading?

2. **Design threading strategy:**
   ```cpp
   class ChromaSinkThreadPool {
   public:
       ChromaSinkThreadPool(int num_threads, 
                           Decoder& decoder,
                           OutputWriter& writer);
       
       // Process a batch of frames
       bool process_batch(
           const VideoFieldRepresentation& vfr,
           int start_frame,
           int end_frame,
           QFile& output_file
       );
       
   private:
       std::vector<std::unique_ptr<DecoderThread>> threads_;
       std::mutex input_mutex_;
       std::mutex output_mutex_;
       std::atomic<bool> abort_;
   };
   ```

3. **Port DecoderPool logic:**
   - Adapt `DecoderPool::getInputFrames()` for VFR
   - Adapt `DecoderPool::putOutputFrames()` for ordered output
   - Implement batch processing with configurable batch size
   - Handle thread synchronization

4. **Ensure deterministic output:**
   - Frames must be written in order
   - Use pending frame buffer (like original DecoderPool)
   - Handle partial batches at end

5. **Add progress reporting:**
   ```cpp
   // Callback for GUI progress bar
   using ProgressCallback = std::function<void(int current, int total)>;
   
   void set_progress_callback(ProgressCallback callback) {
       progress_callback_ = callback;
   }
   ```

6. **Optimize thread count:**
   - Default: `QThread::idealThreadCount()`
   - Allow override via parameters
   - Consider I/O vs CPU balance

#### Test Verification

**Determinism test:**
```bash
# Run same test multiple times, verify identical output
for i in {1..10}; do
    ./orc-core-trigger chroma_sink test.orcprj > /tmp/out$i.rgb
    sha256sum /tmp/out$i.rgb
done
# All checksums should be identical
```

**Performance benchmark:**
```bash
# Single-threaded
time ./orc-core-trigger --threads=1 chroma_sink test.orcprj

# Multi-threaded (various counts)
time ./orc-core-trigger --threads=2 chroma_sink test.orcprj
time ./orc-core-trigger --threads=4 chroma_sink test.orcprj
time ./orc-core-trigger --threads=8 chroma_sink test.orcprj
time ./orc-core-trigger --threads=16 chroma_sink test.orcprj

# Compare with original tool
cd orc-chroma-decoder/tests
time ./run-tests.sh generate -v  # Check FPS in output
```

**Full test suite:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify
# All 24 tests must pass with threading enabled
```

#### Success Criteria
- ✅ Multi-threading works without crashes
- ✅ Output is deterministic (same input → same output)
- ✅ All 24 tests still pass
- ✅ Performance matches or exceeds standalone tool
- ✅ Scaling improves with thread count (up to CPU limit)
- ✅ No race conditions or deadlocks
- ✅ Memory usage is reasonable

---

### Implementation: Preview Support

**Status:** Not Started  
**Risk:** Medium  
**Duration:** ~6 hours

#### Objectives
Enable real-time preview in GUI. Decode single fields on-demand without full export.

#### Actions

1. **Implement render_preview_field():**
   ```cpp
   std::shared_ptr<const VideoFieldRepresentation> 
   ChromaSinkStage::render_preview_field(
       std::shared_ptr<const VideoFieldRepresentation> input,
       FieldID field_id
   ) const {
       // 1. Check cache
       if (preview_cache_.contains(field_id)) {
           return preview_cache_.at(field_id);
       }
       
       // 2. Get decoder (use fast settings for preview)
       auto decoder = create_preview_decoder();
       
       // 3. Extract fields needed for decoding
       QVector<SourceField> fields = extract_fields_for_preview(input, field_id);
       
       // 4. Decode
       QVector<ComponentFrame> component_frames;
       decoder->decodeFrames(fields, 0, 1, component_frames);
       
       // 5. Convert to VideoFieldRepresentation
       auto output = create_colorized_vfr(component_frames[0], field_id);
       
       // 6. Cache and return
       preview_cache_[field_id] = output;
       return output;
   }
   ```

2. **Implement preview-specific optimizations:**
   ```cpp
   std::unique_ptr<Decoder> create_preview_decoder() const {
       // Use faster settings for preview:
       // - Disable noise reduction (expensive)
       // - Use 2D instead of 3D (no temporal context)
       // - Lower quality settings if available
       
       auto decoder = create_decoder(...);
       if (preview_mode_) {
           // Apply preview optimizations
       }
       return decoder;
   }
   ```

3. **Implement preview cache:**
   ```cpp
   mutable std::map<FieldID, std::shared_ptr<VideoFieldRepresentation>> preview_cache_;
   
   // Clear cache when parameters change
   bool set_parameters(...) override {
       bool changed = // detect parameter changes
       if (changed) {
           preview_cache_.clear();
       }
       return true;
   }
   ```

4. **Handle temporal decoders:**
   - For 2D decoders: Single frame decode is straightforward
   - For 3D decoders: May need to decode neighboring frames too
   - Consider simplifying to 2D for preview (faster, acceptable quality)

5. **Convert ComponentFrame to VideoFieldRepresentation:**
   ```cpp
   std::shared_ptr<VideoFieldRepresentation> 
   create_colorized_vfr(
       const ComponentFrame& component_frame,
       FieldID field_id
   ) {
       // Extract RGB or YUV data from component frame
       // Create new VFR with colorized data
       // Copy metadata from input
       // Return as read-only
   }
   ```

#### Test Verification

**Manual GUI testing:**

1. **Basic preview:**
   - Open project with PAL source → chroma sink
   - Set decoder to pal2d
   - Scrub through fields in preview
   - Verify colorized output appears
   - Check preview updates when changing fields

2. **Parameter changes:**
   - Change chroma_gain parameter
   - Verify preview updates
   - Change chroma_phase parameter
   - Verify preview updates
   - Switch decoder types
   - Verify preview updates

3. **Different decoders:**
   - Test PAL 2D preview
   - Test Transform 2D preview
   - Test NTSC 2D preview
   - Test mono preview

4. **Performance:**
   - Scrubbing should feel responsive (<100ms per field)
   - Cache should prevent redundant decoding
   - Memory usage should be reasonable

5. **Edge cases:**
   - First field in video
   - Last field in video
   - Rapid parameter changes
   - Switching between projects

#### Success Criteria
- ✅ Preview shows colorized output
- ✅ Preview updates when scrubbing
- ✅ Parameter changes update preview
- ✅ Decoder type changes update preview
- ✅ Preview is reasonably responsive (<200ms)
- ✅ Cache prevents redundant work
- ✅ No crashes during preview
- ✅ Memory usage is acceptable

---

### Implementation: Output Format Support

**Status:** Not Started  
**Risk:** Low  
**Duration:** ~4 hours

#### Objectives
Complete feature parity with all output formats and options.

#### Actions

1. **Implement output format selection:**
   ```cpp
   OutputWriter::PixelFormat get_pixel_format(const std::string& format) {
       if (format == "rgb") return OutputWriter::RGB48;
       if (format == "yuv") return OutputWriter::YUV444P16;
       if (format == "y4m") {
           // Y4M is YUV444P16 with special headers
           return OutputWriter::YUV444P16;
       }
       return OutputWriter::RGB48;  // default
   }
   ```

2. **Add remaining parameters:**
   - `output_padding` (INTEGER) - Padding multiple (1-32), default 8
   - `first_active_field_line` (INTEGER) - Override first visible field line
   - `last_active_field_line` (INTEGER) - Override last visible field line
   - `first_active_frame_line` (INTEGER) - Override first visible frame line
   - `last_active_frame_line` (INTEGER) - Override last visible frame line

3. **Implement output padding:**
   ```cpp
   OutputWriter::Configuration output_config;
   output_config.paddingAmount = get_int_param(parameters, "output_padding", 8);
   
   // Validate range
   if (output_config.paddingAmount < 1 || output_config.paddingAmount > 32) {
       // Error
   }
   ```

4. **Implement field reversal:**
   ```cpp
   bool reverse_fields = get_bool_param(parameters, "reverse_fields", false);
   
   // When extracting fields, swap order:
   if (reverse_fields) {
       fields[0] = get_field(frame, 1);  // Second field first
       fields[1] = get_field(frame, 0);  // First field second
   }
   ```

5. **Implement custom line ranges:**
   ```cpp
   if (has_param(parameters, "first_active_frame_line")) {
       video_params.firstActiveFrameLine = 
           get_int_param(parameters, "first_active_frame_line");
   }
   // ... similarly for other line parameters
   ```

6. **Implement Y4M format:**
   - Y4M has special headers (stream header, frame headers)
   - Already handled by OutputWriter if properly configured
   - Ensure `outputY4m` flag is set correctly

#### Test Verification

**Run format tests:**
```bash
cd orc-chroma-decoder/tests
./run-tests.sh verify -f Format
```

**Expected results:**
- PAL_2D_RGB: ✅
- PAL_2D_YUV: ✅
- PAL_2D_Y4M: ✅
- NTSC_2D_RGB: ✅
- NTSC_2D_YUV: ✅
- NTSC_2D_Y4M: ✅

**Run edge case tests:**
```bash
./run-tests.sh verify -f Edge
```

**Expected results:**
- PAL_CAV_2D: ✅
- NTSC_CAV_2D: ✅
- PAL_ReverseFields: ✅
- PAL_Padding: ✅
- PAL_CustomLines: ✅

#### Success Criteria
- ✅ RGB48 output works
- ✅ YUV444P16 output works
- ✅ Y4M output works (with correct headers)
- ✅ Output padding works
- ✅ Field reversal works
- ✅ Custom line ranges work
- ✅ All format and edge case tests pass

---

### Implementation: Parameter Validation & Error Handling

**Status:** Not Started  
**Risk:** Low  
**Duration:** ~4 hours

#### Objectives
Make the stage robust and user-friendly with proper validation and error reporting.

#### Actions

1. **Add parameter validation:**
   ```cpp
   bool ChromaSinkStage::validate_parameters(
       const std::map<std::string, ParameterValue>& params
   ) {
       // Check required parameters
       if (!has_param(params, "output_path")) {
           set_error("Output path is required");
           return false;
       }
       
       // Validate ranges
       double chroma_gain = get_double_param(params, "chroma_gain", 1.0);
       if (chroma_gain < 0.0 || chroma_gain > 10.0) {
           set_error("Chroma gain must be between 0 and 10");
           return false;
       }
       
       double chroma_phase = get_double_param(params, "chroma_phase", 0.0);
       if (chroma_phase < -180.0 || chroma_phase > 180.0) {
           set_error("Chroma phase must be between -180 and 180 degrees");
           return false;
       }
       
       // ... other validations
       
       return true;
   }
   ```

2. **Implement error reporting:**
   ```cpp
   std::string error_message_;
   
   void set_error(const std::string& message) {
       error_message_ = message;
       ORC_LOG_ERROR("ChromaSink: {}", message);
   }
   
   std::string get_last_error() const {
       return error_message_;
   }
   ```

3. **Add file system checks:**
   ```cpp
   // Check output directory exists and is writable
   std::filesystem::path output_path(get_string_param(params, "output_path"));
   std::filesystem::path output_dir = output_path.parent_path();
   
   if (!std::filesystem::exists(output_dir)) {
       set_error("Output directory does not exist: " + output_dir.string());
       return false;
   }
   
   if (!std::filesystem::is_directory(output_dir)) {
       set_error("Output path parent is not a directory: " + output_dir.string());
       return false;
   }
   
   // Check if file already exists (warn or error?)
   if (std::filesystem::exists(output_path)) {
       ORC_LOG_WARN("Output file already exists, will be overwritten: {}", 
                    output_path.string());
   }
   ```

4. **Handle input validation:**
   ```cpp
   // In trigger()
   if (inputs.empty()) {
       set_error("No input connected");
       return false;
   }
   
   auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
   if (!vfr) {
       set_error("Input must be VideoFieldRepresentation");
       return false;
   }
   
   if (vfr->get_total_frames() == 0) {
       set_error("Input has no frames");
       return false;
   }
   ```

5. **Add graceful cancellation:**
   ```cpp
   std::atomic<bool> cancel_requested_{false};
   
   void request_cancel() {
       cancel_requested_ = true;
   }
   
   // In processing loop:
   if (cancel_requested_) {
       ORC_LOG_INFO("Processing cancelled by user");
       return false;
   }
   ```

6. **Improve progress reporting:**
   ```cpp
   // Report progress periodically
   if (frame % 10 == 0 || frame == last_frame) {
       double progress = static_cast<double>(frame - start_frame) / frames_to_process;
       if (progress_callback_) {
           progress_callback_(frame - start_frame, frames_to_process);
       }
       ORC_LOG_INFO("Processing: {}% ({}/{})", 
                    static_cast<int>(progress * 100),
                    frame - start_frame,
                    frames_to_process);
   }
   ```

#### Test Verification

**Test invalid parameters:**
```bash
# Test various invalid parameter combinations
# - Missing output_path
# - Invalid decoder_type
# - Out-of-range chroma_gain
# - Out-of-range chroma_phase
# - Invalid thread count
# - Non-existent output directory
```

**Test error conditions:**
```bash
# - No input connected
# - Wrong input type
# - Empty input
# - Disk full (if possible to simulate)
# - Permission denied on output directory
```

**Test cancellation:**
```bash
# Start long processing job
# Cancel mid-way
# Verify graceful shutdown
# Check partial output is cleaned up
```

#### Success Criteria
- ✅ Invalid parameters are caught early
- ✅ Error messages are clear and helpful
- ✅ File system errors are handled gracefully
- ✅ Cancellation works without crashes
- ✅ Progress reporting is accurate
- ✅ No silent failures
- ✅ Log messages are informative

---

### Implementation: Documentation & Integration

**Status:** Not Started  
**Risk:** Low  
**Duration:** ~4 hours

#### Objectives
Complete documentation and ensure smooth integration into orc-core ecosystem.

#### Actions

1. **Write stage documentation:**
   ```markdown
   # Chroma Sink Stage
   
   Decodes and exports colorized video from TBC fields.
   
   ## Overview
   
   The Chroma Sink stage performs chroma decoding on LaserDisc TBC data,
   converting composite PAL or NTSC video into component RGB or YUV output.
   
   ## Supported Decoders
   
   ### PAL Decoders
   - **pal2d**: 2D comb filter (fast, good quality)
   - **transform2d**: 2D frequency-domain (better quality)
   - **transform3d**: 3D frequency-domain (best quality, slower)
   
   ### NTSC Decoders
   - **ntsc1d**: 1D comb filter (fast, lower quality)
   - **ntsc2d**: 2D comb filter (good quality)
   - **ntsc3d**: 3D adaptive comb (best quality, slower)
   - **ntsc3dnoadapt**: 3D non-adaptive comb
   
   ### Other
   - **mono**: Black & white output (no chroma decoding)
   - **auto**: Automatically select based on video system
   
   ## Parameters
   
   [Full parameter documentation...]
   
   ## Usage Examples
   
   [Examples of common configurations...]
   
   ## Performance Notes
   
   [Threading, memory usage, etc...]
   ```

2. **Add tooltips/help text:**
   ```cpp
   ParameterDescriptor{
       "chroma_gain",
       "Chroma Gain",
       "Gain factor applied to chroma components. Values > 1.0 increase "
       "color saturation, values < 1.0 decrease it. Range: 0.0-10.0",
       ParameterType::DOUBLE,
       ParameterConstraints{}.min(0.0).max(10.0).default_value(1.0)
   }
   ```

3. **Create project templates:**
   ```json
   // templates/pal-export.orcprj
   {
     "name": "PAL Video Export",
     "description": "Standard PAL chroma decoding to RGB",
     "nodes": [
       {
         "id": "source",
         "stage_name": "ld_pal_source",
         "position": {"x": 100, "y": 200},
         "parameters": {
           "tbc_path": ""
         }
       },
       {
         "id": "chroma",
         "stage_name": "chroma_sink",
         "position": {"x": 400, "y": 200},
         "parameters": {
           "decoder_type": "pal2d",
           "output_format": "rgb",
           "output_path": ""
         }
       }
     ],
     "connections": [
       {"from": "source:0", "to": "chroma:0"}
     ]
   }
   ```

4. **Update GUI integration:**
   - Add chroma sink to node palette with icon
   - Ensure parameter UI works correctly
   - Test trigger button behavior
   - Verify progress bar updates

5. **Write migration guide:**
   ```markdown
   # Migrating from ld-chroma-decoder to Chroma Sink
   
   ## Command-Line Mapping
   
   Old command:
   ```bash
   ld-chroma-decoder -s 1 -l 100 -f pal2d -p rgb input.tbc output.rgb
   ```
   
   New workflow:
   1. Create .orcprj with source and chroma sink
   2. Set parameters in GUI or project file
   3. Trigger export
   
   ## Parameter Mapping
   
   | ld-chroma-decoder | Chroma Sink | Notes |
   |-------------------|-------------|-------|
   | -s, --start | start_frame | |
   | -l, --length | length | |
   | -f, --decoder | decoder_type | |
   | -p, --output-format | output_format | |
   | --chroma-gain | chroma_gain | |
   | --chroma-phase | chroma_phase | |
   | -t, --threads | threads | |
   | -r, --reverse | reverse_fields | |
   
   [Complete mapping table...]
   ```

6. **Update build documentation:**
   - Document new dependencies (if any)
   - Update CMake options
   - Update installation instructions

#### Test Verification

**Documentation review:**
- Read through all documentation
- Verify examples are correct
- Check that help text is helpful
- Ensure migration guide is complete

**GUI testing:**
- Verify node appears correctly in palette
- Check icon is appropriate
- Test all parameter controls
- Verify trigger button works
- Check progress reporting

**Template testing:**
- Load each template
- Verify it creates correct structure
- Test with sample data

#### Success Criteria
- ✅ Documentation is complete and accurate
- ✅ Help text is clear and helpful
- ✅ Templates work correctly
- ✅ GUI integration is smooth
- ✅ Migration guide covers all cases
- ✅ Build instructions are current

---

### Implementation: Final Validation & Deprecation

**Status:** Not Started  
**Risk:** Low  
**Duration:** ~2 hours

#### Objectives
Final comprehensive testing and begin deprecation of standalone tool.

#### Actions

1. **Run complete test suite:**
   ```bash
   cd orc-chroma-decoder/tests
   ./run-tests.sh verify
   
   # Expected: All 24 tests pass
   # Mode: verify, Total: 24, Passed: 24, Failed: 0, Skipped: 0
   ```

2. **Performance benchmarking:**
   ```bash
   # Compare processing speeds
   # Standalone tool:
   time orc-chroma-decoder -s 1 -l 1000 input.tbc output.rgb
   
   # Integrated sink:
   time orc-trigger chroma_sink test.orcprj
   
   # Should be comparable (within 5-10%)
   ```

3. **Memory profiling:**
   ```bash
   valgrind --leak-check=full orc-gui
   # Run chroma sink export
   # Check for memory leaks
   ```

4. **Mark standalone tool as deprecated:**
   ```cpp
   // In orc-chroma-decoder/src/main.cpp
   qWarning() << "============================================";
   qWarning() << "NOTE: orc-chroma-decoder is deprecated.";
   qWarning() << "Please use the Chroma Sink stage in orc-core.";
   qWarning() << "This standalone tool will be removed in a future release.";
   qWarning() << "============================================";
   ```

5. **Update README:**
   ```markdown
   # orc-chroma-decoder (DEPRECATED)
   
   ⚠️ **This standalone tool is deprecated.**
   
   Please use the **Chroma Sink** stage in orc-core instead.
   
   This directory contains:
   - Legacy source code (for reference)
   - Comprehensive test suite (still used for validation)
   - Build system (optional, for compatibility testing)
   
   ## Migration
   
   See [../docs/CHROMA-SINK-MIGRATION.md](../docs/CHROMA-SINK-MIGRATION.md)
   for migration guide.
   
   ## Test Suite
   
   The test suite is still actively used to validate the Chroma Sink
   stage in orc-core. See [tests/README.md](tests/README.md) for details.
   ```

6. **Make standalone build optional:**
   ```cmake
   # In top-level CMakeLists.txt
   option(BUILD_LEGACY_CHROMA_DECODER 
          "Build legacy standalone orc-chroma-decoder (deprecated)" 
          OFF)
   
   if(BUILD_LEGACY_CHROMA_DECODER)
       add_subdirectory(orc-chroma-decoder)
   endif()
   ```

#### Test Verification

**Final validation checklist:**

- [ ] All 24 test signatures match
- [ ] Performance is acceptable
- [ ] No memory leaks
- [ ] No crashes in normal use
- [ ] No crashes in error conditions
- [ ] Documentation is complete
- [ ] GUI integration works
- [ ] Progress reporting works
- [ ] Cancellation works
- [ ] All parameters work
- [ ] All decoders work
- [ ] All output formats work
- [ ] Preview works
- [ ] Threading works
- [ ] Error messages are helpful

**User acceptance testing:**

- [ ] Process real PAL laserdisc video
- [ ] Process real NTSC laserdisc video
- [ ] Try all decoder types
- [ ] Try all output formats
- [ ] Try various parameter combinations
- [ ] Test with large videos (>10000 frames)
- [ ] Test with small videos (<100 frames)
- [ ] Test edge cases (first/last frames)

#### Success Criteria
- ✅ All tests pass
- ✅ Performance is comparable
- ✅ No regressions detected
- ✅ User acceptance passed
- ✅ Deprecation notices in place
- ✅ Build system updated
- ✅ Ready for production use

---

## Testing Strategy

### Test Suite Integration

The existing test suite in `orc-chroma-decoder/tests/` will be adapted to test the new Chroma Sink stage:

1. **Modify `run-tests.sh`:**
   - Add mode to test orc-core implementation
   - Support both standalone and integrated testing
   - Compare outputs for equivalence

2. **Test execution:**
   ```bash
   # Test standalone tool (baseline)
   ./run-tests.sh verify --mode=standalone
   
   # Test integrated sink
   ./run-tests.sh verify --mode=integrated
   
   # Compare both
   ./run-tests.sh verify --mode=both
   ```

3. **Continuous testing:**
   - Run tests after each refactoring step
   - Maintain baseline signatures
   - Detect any regressions immediately

### Success Criteria for Each Step

Each step must pass these checks before proceeding:

1. **Build succeeds** - No compilation errors
2. **Tests pass** - All applicable tests from suite pass
3. **No crashes** - Stable under normal and error conditions
4. **No regressions** - Existing functionality unaffected
5. **Performance** - No significant slowdown (after Step 7)

---

## Risk Assessment

### High Risk Steps

- **Step 7 (Multi-threading):** Complex synchronization, potential race conditions
  - Mitigation: Extensive testing, use proven DecoderPool logic
  
### Medium Risk Steps

- **Step 3 (First decoder):** Establishing data conversion patterns
  - Mitigation: Start with simplest decoder (mono)
  
- **Step 8 (Preview):** New functionality, performance sensitive
  - Mitigation: Use caching, consider simplified preview mode

### Low Risk Steps

- Steps 1, 2, 9, 10, 11, 12: Straightforward implementation
  - Well-defined interfaces and patterns

---

## Timeline Estimate

| Step | Description | Duration | Cumulative |
|------|-------------|----------|------------|
| 1 | Skeleton stage | 2 hours | 2h |
| 2 | Copy decoders | 3 hours | 5h |
| **3** | **Convert Qt6 to C++** | **12 hours** | **17h** |
| 4 | Mono decoder | 6 hours | 23h |
| 5 | PAL 2D | 4 hours | 27h |
| 6 | NTSC decoders | 5 hours | 32h |
| 7 | Transform PAL | 5 hours | 37h |
| 8 | Multi-threading | 8 hours | 45h |
| 9 | Preview | 6 hours | 51h |
| 10 | Output formats | 4 hours | 55h |
| 11 | Validation & errors | 4 hours | 59h |
| 12 | Documentation | 4 hours | 63h |
| 13 | Final validation | 2 hours | 65h |

**Total estimated effort:** ~65 hours (~1.5-2 weeks of focused development)

---

## Rollout Strategy

### Phase 1: Development (Steps 1-8)
- Core functionality implemented
- **Qt6 to C++ conversion complete** (Step 3)
- All decoders working
- Single-threaded and multi-threaded
- Internal testing only

### Phase 2: Integration (Steps 9-11)
- GUI integration complete
- Preview working
- All features implemented
- Beta testing with select users

### Phase 3: Production (Steps 12-13)
- Documentation complete
- User acceptance testing
- Deprecation notices
- General availability

### Phase 4: Deprecation
- Standalone tool marked deprecated
- Keep available for 1-2 releases
- Eventually remove (archive for reference)

---

## Maintenance

### Test Suite
- Continue maintaining test suite in `orc-chroma-decoder/tests/`
- Use for regression testing of Chroma Sink stage
- Keep baseline signatures up to date

### Legacy Code
- Keep `orc-chroma-decoder/` directory as reference
- Document any deviations from original
- Maintain build for compatibility testing (optional)

### Future Enhancements
- GPU acceleration (separate project)
- Additional output formats
- Real-time preview optimization
- Advanced decoder tuning options

---

## Critical Issues & Resolutions

### Issue #1: Padding Alignment Bug (22 Dec 2025)

**Severity:** CRITICAL  
**Status:** ✅ RESOLVED  
**Affected Decoders:** Transform PAL 2D, Transform PAL 3D (FFT-based)

#### Problem Description

Transform PAL decoders produced incorrect output with checksums differing from the standalone decoder, despite using identical algorithms and parameters. Initial investigation suggested floating-point precision or compiler optimization differences, but the actual issue was more subtle.

#### Root Cause Analysis

The `OutputWriter` class modifies `activeVideoStart` and `activeVideoEnd` values to ensure the output width is divisible by the padding factor (default: 8 pixels). This padding adjustment happens during `OutputWriter::updateConfiguration()`.

**Original workflow (INCORRECT):**
1. Read video parameters from TBC metadata: `activeVideoStart=185, activeVideoEnd=1107` (width=922)
2. Initialize decoder with original parameters
3. Later, initialize OutputWriter which adjusts parameters: `activeVideoStart=182, activeVideoEnd=1110` (width=928)
4. Decoder processes region 185-1107, OutputWriter expects region 182-1110
5. **Mismatch:** Different regions → incorrect pixel data

**Why this affected Transform PAL specifically:**
- Transform PAL uses FFT-based processing on overlapping tiles
- First tile X position calculated as: `tileX = activeVideoStart - HALFXTILE`
  - ORC (wrong): 185 - 16 = 169
  - Standalone (correct): 182 - 16 = 166
- Processing different tiles produces completely different frequency-domain data
- Simple comb filters (PAL 2D, NTSC) were less affected due to pixel-by-pixel processing

#### Debugging Methodology

1. **Initial comparison:** Noticed file sizes matched but checksums differed
2. **Byte-level analysis:** Used `cmp -l` to confirm pixel data differences from byte 3 onward
3. **Parameter verification:** Confirmed all decoder parameters (gain, phase, NR) were identical
4. **Execution tracing:** Added debug output to compare Transform PAL configuration:
   ```
   ORC: activeVideoStart=185, activeVideoEnd=1107, tileX=169
   Standalone: activeVideoStart=182, activeVideoEnd=1110, tileX=166
   ```
5. **Database inspection:** Verified TBC database contains 185/1107 (original values)
6. **Code inspection:** Found OutputWriter padding logic modifies parameters during initialization

#### Solution

Apply padding adjustments BEFORE initializing the decoder:

```cpp
// BEFORE decoder initialization
{
    OutputWriter::Configuration writerConfig;
    writerConfig.paddingAmount = 8;
    
    // Create temporary OutputWriter just to apply padding adjustments
    OutputWriter tempWriter;
    tempWriter.updateConfiguration(ldVideoParams, writerConfig);
    // ldVideoParams now has adjusted activeVideoStart/End values (182/1110)
}

// NOW initialize decoder with adjusted parameters
palDecoder->updateConfiguration(ldVideoParams, config);
```

**File:** `orc/core/stages/chroma_sink/chroma_sink_stage.cpp` lines 336-348

#### Verification

After fix, all 24 decoder tests pass with pixel-perfect output:
- All PAL decoders: 9/9 tests ✅
- All NTSC decoders: 10/10 tests ✅
- Edge cases: 5/5 tests ✅
- **Total: 24/24 tests passing**

Example checksums (Transform2D PAL):
```
Reference:  e7beef3c7dcf355c8bb3776ce74cf0becbdacbfdd88e0a131d4e9addc9bcef94
ORC Before: c6c9c81b3acae6ba3b74a96fecb62c85419c19e50382d9957f822e2fc69da344 ❌
ORC After:  e7beef3c7dcf355c8bb3776ce74cf0becbdacbfdd88e0a131d4e9addc9bcef94 ✅
```

#### Key Takeaways

1. **Order of operations matters:** Configuration steps must happen in the correct sequence
2. **FFT-based algorithms are sensitive:** Small region offsets produce completely different results
3. **Debug with execution tracing:** Comparing intermediate values between implementations is invaluable
4. **Test-driven development:** Comprehensive test suite caught the issue immediately
5. **Architecture insight:** OutputWriter's padding logic is a cross-cutting concern that affects decoder initialization

#### Related Code Locations

- **Bug fix:** `orc/core/stages/chroma_sink/chroma_sink_stage.cpp:336-348`
- **Padding logic:** `orc/core/stages/chroma_sink/decoders/outputwriter.cpp:68-83`
- **Transform PAL tiles:** `orc/core/stages/chroma_sink/decoders/transformpal2d.cpp:142`
- **Test verification:** `orc-chroma-decoder/tests/run-tests.sh`

---

## References

- Original ld-chroma-decoder: https://github.com/happycube/ld-decode
- orc-core design: [../docs/DESIGN.md](../docs/DESIGN.md)
- Test suite documentation: [tests/README.md](tests/README.md)
- Chroma decoding theory: Various academic papers (see DEPENDENCIES.md)

---

## Notes

- **Step 3 (Qt6 → C++) is the most critical and time-consuming** - Don't underestimate this!
- The standalone tool will continue using Qt6 as a wrapper around the converted decoders
- This plan prioritizes correctness over speed - get it working first, optimize later
- Each step is a checkpoint - can pause for review/testing
- Test suite is critical - must maintain 100% signature match
- Performance target: Match or exceed standalone tool after Step 8
- Preview can be simplified for better responsiveness (Step 9)
- Threading conversion (Step 3) and multi-threading implementation (Step 8) are separate concerns

---

**Document Version:** 1.0  
**Status:** Ready for implementation
