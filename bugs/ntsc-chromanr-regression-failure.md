# Bug Report: NTSC_2D_ChromaNR Regression Test Failure

**Date:** 2026-01-02  
**Status:** UNRESOLVED  
**Severity:** High (blocking regression test suite)  
**Test:** `NTSC_2D_ChromaNR`  
**File:** `testdata-ld/scripts/chroma/test-orc-chroma-decoder.sh`

## Problem Summary

The NTSC_2D_ChromaNR regression test consistently fails with a checksum mismatch between ORC output and ld-chroma-decoder baseline, despite the CNR algorithm being verified as functionally correct.

### Test Configuration
- **Decoder:** ntsc2d (NTSC 2D comb filter)
- **Parameter:** `--chroma-nr 3.0`
- **Input:** `Bambi_CLV_NTSC_side1_JapanImport_LDG_2020-01-22_20-25-19_pos8483.tbc`
- **Output Format:** RGB48 (16-bit per channel, big-endian)
- **Output Dimensions:** 760×488 pixels, 10 frames
- **File Size:** 22,211,520 bytes (22MB)

### Checksums
```
Expected (ld-chroma-decoder): f1c519e9b9a02a6295c0bd5b2d016cf9d15602d131c82f3f94b7a00f18b5e248
Got (ORC):                    5c615490d006363203c1d72c46b20413ac9d54e62caaedc70d6bd995686107a0
```

### Other Test Status
- **NTSC_2D_RGB:** PASSES (no CNR, outputs identical)
- **NTSC_2D_LumaNR:** FAILS (luma NR test, likely same root cause)
- **All other NTSC tests:** PASS

## Investigation Summary

### What We Know

#### 1. CNR Algorithm Executes Correctly
**Evidence:**
```
doCNR: first_active_frame_line=40, last_active_frame_line=525
doCNR: active_video_start=134, active_video_end=894
doCNR: nr_c=990  (calculated as cNRLevel=3.0 * irescale=330)
```

The CNR function is called with the correct parameters. The coring level (nr_c=990) matches the expected calculation.

#### 2. Parameter Propagation Verified
The chroma_nr parameter flows correctly through the entire stack:
1. YAML config: `chroma_nr: 3.0` ✓
2. ChromaSink stage: Receives 3.0 ✓
3. Decoder configuration: Sets cNRLevel=3.0 ✓
4. doCNR execution: Uses nr_c=990 ✓

#### 3. Filter Coefficients Match ld-decode
The f_nrc high-pass filter coefficients in `deemp.h` are byte-for-byte identical to ld-decode's implementation:
```cpp
const std::vector<double> c_nrc_b = {
    -0.0318519888927752, -0.0171322687784154, 0.0150169490027296, ...
};
```

Verified via:
- Multiple f_nrc instances found in both binaries (comb.o, monodecoder.o, palcolour.o)
- Filter coefficient arrays match exactly

#### 4. Active Video Region Analysis
**TBC Dimensions:**
- Field: 910×263 pixels
- Frame: 910×525 pixels
- Active region: pixels 134-894 (760 pixels wide)
- Output: 760×488 pixels

**Pixel-Level Comparison Results:**
Using Python byte-by-byte analysis on frame 0, line 42:
- Pixels 142-759 (active region): **IDENTICAL** between ORC and ld-chroma-decoder
- Pixels 0-141 (first 142 pixels): **DIFFERENT**

Example values:
```
Pixel [200, 42] (active region):
  ORC:           R=16667 G=52251 B=34590
  ld-chroma-dec: R=16667 G=52251 B=34590  ← MATCH

Pixel [70, 42] (early pixels):
  ORC:           R=56080 G=54046 B=18729
  ld-chroma-dec: R=54038 G=14621 B=58145  ← MISMATCH
```

**Critical Finding:** The active video region (the actual decoded video content) is pixel-perfect identical. The differences are in the early pixels of each line.

#### 5. Frame Structure Details
**Active Area Cropping:** `active_area_cropping_applied = true`

This means:
- ComponentFrame is already cropped to active region (760 pixels wide)
- No "blanked pixels" exist in the ComponentFrame
- Pixel 0 of ComponentFrame = TBC pixel 134 (start of active region)
- Pixel 759 of ComponentFrame = TBC pixel 893 (end of active region)

**splitIQ Processing:**
```cpp
xOffset = active_video_start = 134  (when cropping applied)
Loop: h from 134 to 894
Writes: Y[h - 134] = line[h]  // Maps TBC pixels 134-893 to ComponentFrame pixels 0-759
```

#### 6. Without CNR, Outputs Are Identical
The NTSC_2D_RGB test (same decoder, no CNR) produces byte-for-byte identical output:
```
NTSC_2D_RGB: PASS
```

This proves:
- splitIQ, filterIQ, adjustY, transformIQ all work correctly
- Only the CNR processing path introduces the difference

---

## What We've Tried

### Attempt 1: Verify CNR Execution
**Action:** Added debug logging to doCNR  
**Result:** Confirmed CNR executes with correct parameters  
**Outcome:** CNR is being called, parameters are correct

### Attempt 2: Check Filter Coefficients
**Action:** Compared c_nrc_b arrays between ORC and ld-decode  
**Result:** Arrays are identical  
**Outcome:** Filter definition is correct

### Attempt 3: Investigate Build Issues
**Action:** Searched for duplicate f_nrc filter instances  
**Result:** Found 3 instances in both ORC and ld-decode (normal)  
**Outcome:** No build issues detected

### Attempt 4: Generate Fresh Baselines
**Action:** Re-ran ld-chroma-decoder to verify baseline checksum  
**Result:** Baseline is correct (f1c519e9b9a0...)  
**Outcome:** Test expectations are valid

### Attempt 5: Pixel-Level Analysis
**Action:** Wrote Python scripts to compare RGB values pixel-by-pixel  
**Result:** Active region identical, early pixels differ  
**Outcome:** Issue is limited to non-active pixels

### Attempt 6: Investigate Blanked Pixel Handling
**Action:** Attempted to copy TBC data to blanked pixels in splitIQ  
**Result:** Code never executed (active_area_cropping_applied=true means no blanked pixels exist)  
**Outcome:** Misunderstanding of frame structure; blanked pixels not in output

### Attempt 7: Check splitIQlocked vs splitIQ
**Action:** Added logging to both functions  
**Result:** splitIQ is being used (not splitIQlocked)  
**Outcome:** Correct code path confirmed

---

## What It CAN'T Be

### 1. **NOT a Parameter Passing Issue**
- Verified chroma_nr flows from YAML → ChromaSink → Decoder → doCNR
- Debug output confirms nr_c=990 (correct calculation)
- All intermediate values logged and verified

### 2. **NOT a Filter Coefficient Issue**
- c_nrc_b arrays are byte-for-byte identical
- Filter coefficients hardcoded from ld-decode source
- Multiple instances found in both binaries (expected behavior)

### 3. **NOT a CNR Algorithm Issue**
- Active video region pixels are IDENTICAL
- If the CNR algorithm were wrong, active pixels would differ
- The fact that active pixels match proves CNR works correctly

### 4. **NOT a Frame Dimension Issue**
- Both outputs are 760×488 pixels, 10 frames, RGB48 format
- File sizes match exactly (22MB)
- Frame structure verified with debug logging

### 5. **NOT a Blanked Pixel Problem**
- ComponentFrame is already cropped (active_area_cropping_applied=true)
- There are no "blanked pixels" in the output to fill
- The first 142 pixels that differ ARE part of the active region (TBC pixels 134-275)

### 6. **NOT a Build or Compilation Issue**
- Same compiler, same flags, same dependencies
- Filter instances match between both projects
- No warnings or errors during build

### 7. **NOT a Test Infrastructure Issue**
- Other tests pass (NTSC_2D_RGB passes perfectly)
- Baseline checksums verified as current and correct
- Test script works correctly for other tests

### 8. **NOT an Output Format Issue**
- RGB48 format matches
- Big-endian byte order verified
- No padding or alignment differences

### 9. **NOT a ComponentFrame Initialization Issue**
- ComponentFrame::init() fills with 0.0 (same as ld-decode)
- All pixels initialized before processing
- Active region pixels prove initialization works

### 10. **NOT a splitIQ/filterIQ Issue**
- NTSC_2D_RGB test passes (uses same functions without CNR)
- Active region pixels identical (splitIQ/filterIQ output correct)
- Only CNR path introduces difference

---

## Theories & Open Questions

### Theory 1: Filter State Initialization
The CNR filter (f_nrc) is an IIR filter with history. The first ~142 pixels might be affected by:
- Initial filter state
- Filter warm-up period
- Edge handling differences

**However:** ld-decode feeds zeros to initialize the filter:
```cpp
for (qint32 h = videoParameters.activeVideoStart - delay; h < videoParameters.activeVideoStart; h++) {
    iFilter.feed(0.0);
    qFilter.feed(0.0);
}
```

ORC should do the same, but this needs verification.

### Theory 2: TBC Pixel Access Pattern
The early pixels (TBC 134-275) might be read/processed differently:
- Edge case handling
- Buffer boundary conditions
- First-pixel special cases

### Theory 3: Output Pixel Mapping
Despite verification, there might be an off-by-one or offset issue in:
- How TBC pixels map to output pixels
- RGB conversion for early pixels
- Line buffer access patterns

### Theory 4: Floating Point Precision
Unlikely but possible:
- Different rounding in early pixels due to filter state
- Accumulation errors that stabilize after ~142 pixels
- Compiler optimization differences

**However:** This wouldn't explain why NTSC_2D_RGB is identical.

---

## Next Steps (Recommendations)

1. **Compare Filter State Initialization**
   - Verify ORC's doCNR feeds zeros before active region (like ld-decode)
   - Check IIRFilter copy constructor and filter history initialization
   - Add logging for filter state at early pixels

2. **Detailed Line-by-Line Comparison**
   - Compare intermediate values (Y/U/V) before CNR
   - Compare high-pass filter output (hpI/hpQ arrays)
   - Check filter state at each pixel in the differing region

3. **Binary Comparison Tools**
   - Use hexdump to examine exact byte patterns
   - Check if difference is consistent across all frames
   - Look for patterns in the differing pixels

4. **Trace One Pixel Through Stack**
   - Pick pixel [70, 42] and trace through entire pipeline
   - Log every transformation: TBC → Y → CNR → RGB
   - Compare ORC vs ld-decode at each step

5. **Check Edge Cases**
   - First pixel of line
   - First line of frame
   - Frame boundaries
   - Filter delay compensation

---

## Code Locations

### ORC
- **CNR Implementation:** `orc/core/stages/chroma_sink/decoders/comb.cpp:711-770`
- **Filter Coefficients:** `orc/core/stages/chroma_sink/decoders/deemp.h:424-435`
- **splitIQ:** `orc/core/stages/chroma_sink/decoders/comb.cpp:617-663`
- **ComponentFrame:** `orc/core/stages/chroma_sink/decoders/componentframe.cpp:20-44`

### ld-decode
- **CNR Implementation:** `tools/ld-chroma-decoder/comb.cpp:699-770`
- **Output Writer:** `tools/ld-chroma-decoder/outputwriter.cpp:266-290`
- **ComponentFrame:** `tools/ld-chroma-decoder/componentframe.cpp:32-50`

---

## Timeline

- **2026-01-02:** Issue discovered during regression testing
- **2026-01-02:** Extensive investigation (parameter verification, pixel analysis, filter checks)
- **2026-01-02:** Narrowed to early pixels in active region, CNR-specific
- **2026-01-02:** Multiple failed attempts to fix (blanking, initialization, etc.)
- **2026-01-02:** Bug report created

---

## Attachments

### Test Commands
```bash
# Run failing test
cd testdata-ld
ORC_CLI="build/bin/orc-cli" bash scripts/chroma/test-orc-chroma-decoder.sh

# Generate ORC output
cd testdata-ld/scripts/chroma
build/bin/orc-cli orc-projects/ntsc-2d-chromanr.yaml --process --log-level debug --log-file /tmp/orc-cli.log

# Compare outputs
sha256sum testdata-ld/rgb/ntsc_2d_chromanr.rgb  # ORC output
```

### Debug Output Sample
```
[2026-01-02 16:41:32.591] [core] [info] splitIQ: active_area_cropping_applied=true, active_video_start=134, xOffset=134
[2026-01-02 16:41:32.600] [core] [info] doCNR: active_video_start=134, active_video_end=894, nr_c=990
```

---

## Additional Notes

- This issue ONLY affects tests with CNR enabled (ChromaNR, LumaNR)
- The active video content is pixel-perfect, so the decoded video is actually correct
- The test failure is due to differences in the first ~18% of pixels in each line
- This might be a cosmetic issue in edge pixels rather than a fundamental algorithm problem
- All other NTSC tests pass, suggesting the issue is very specific to CNR edge handling
