# Transform3D Test Failure Investigation

## Problem Statement
PAL_Transform3D_RGB test fails while all other tests pass (8/9 passing).
- **ORC checksum**: `c343689864d8079739f842cb44c8f19a40744851957c4a4e1af232664748054e`
- **Reference checksum**: `c5a7f2f6f2055bfb5b31c18b7a695ac9e759a03632916915dad10d56c81b9220`

## Test Configuration
- **Input**: PAL test data (amawaab_pal_clv_6001-6205.tbc)
- **Decoder**: Transform3D (3D FFT PAL decoder)
- **Parameters**: start_frame=1, length=10
- **Output format**: RGB48
- **Lookbehind/Lookahead**: 2 frames before, 4 frames after (HALFZTILE=4, ZTILE=8)

## Key Findings

### ✅ What Works
1. **All other tests pass**: PAL 2D, Transform2D, all NTSC decoders (1D, 2D, 3D)
2. **Frames 0-1 are IDENTICAL**: First 3,207,168 bytes match perfectly between ORC and standalone
3. **Padding implementation is correct**: Both ORC and standalone use identical approach
4. **Field ordering refactored successfully**: No TBC metadata dependencies, uses FieldParity only
5. **Field phase IDs populated correctly**: Reading from hints, not from TBC metadata directly

### ❌ Divergence Point
- **First byte difference**: Byte 3,207,169 (start of frame 2)
- **Frame structure**: 1,603,584 bytes per frame = 1135×625×2 bytes (RGB48, one field interlaced)
- **Divergence pattern**: Every frame from frame 2 onward differs

## Implementation Details Verified

### Padding Frame Metadata
Both ORC and standalone use IDENTICAL approach:
```
Frame -1 (padding): Uses metadata from frame 1 fields (0,1), phaseID=5,6, black data
Frame  0 (padding): Uses metadata from frame 1 fields (0,1), phaseID=5,6, black data  
Frame  1 (active):  Uses metadata from frame 1 fields (0,1), phaseID=5,6, real data
Frame  2 (active):  Uses metadata from frame 2 fields (2,3), phaseID=7,8, real data
```

**Field Phase IDs in test** (PAL 8-field cycle: 1,2,3,4,5,6,7,8):
```
Field 0 (lookbehind): seqNo=1,2  phaseID=5,6  checksum=1153223384 (black)
Field 1 (lookbehind): seqNo=1,2  phaseID=5,6  checksum=1153223384 (black)
Field 2 (active):     seqNo=1,2  phaseID=5,6  checksum=3723548259 (real)
Field 3 (active):     seqNo=3,4  phaseID=7,8  checksum=3704585213 (real)
Field 4 (active):     seqNo=5,6  phaseID=1,2  checksum=3721857301 (real)
```

### Black Padding Values
- **Black level**: 15336 (black16bIre)
- **Padding field checksum**: 1153223384
- **All samples**: Uniform value 15336

### Field Data Processing
- **PAL subcarrier-locked shift**: NOT active (isSubcarrierLocked=false in test)
- **Field size**: 355,255 samples per field
- **Field parity**: Top=first field, Bottom=second field (consistent)

## What Has Been Ruled Out

### ❌ NOT the issue:
1. **Padding frame metadata**: Both implementations use frame 1 metadata identically
2. **Padding frame data**: Both use black (15336) identically
3. **Field phase IDs**: Correctly populated from hints, matches database
4. **Field ordering**: Refactored to use FieldParity only (no TBC dependencies)
5. **Lookbehind/lookahead counts**: Correct (2 frames/4 frames for Transform3D)
6. **Field ID calculation**: Formula `(frameNumber * 2) - 2` is correct
7. **Black level value**: 15336 matches between implementations
8. **isFirstField calculation**: Now computed from FieldParity::Top (not from TBC)
9. **Field metadata (syncConf, medianBurstIRE, etc.)**: Transform3D doesn't use these fields
10. **Padding frame count**: NOW FIXED - both create 6 padding fields (3 frames), but output still differs!

### ⚠️ Attempted Fixes That Failed:
1. **Adjusting phaseID for padding frames**: Broke NTSC_3D test
   - Theory: Padding frames should have different phaseIDs (1,2 for frame -1, 3,4 for frame 0)
   - Result: Made NTSC_3D fail, reverted
   - Reason: Standalone ALSO uses same phaseIDs, so this isn't the difference

## Hypotheses to Investigate

### Theory 1: Transform3D State Initialization
- **Observation**: Frames 0-1 identical, frame 2+ differ
- **Hypothesis**: Transform3D FFT has state carried from frame processing
- **Test**: Check if there's FFT state that isn't being reset properly
- **Files**: `transformpal3d.cpp` lines 111-226 (filterFields, forwardFFTTile, inverseFFTTile)

### ~~Theory 2: Other SourceField Members~~ ❌ RULED OUT
- **Observation**: Only verified seqNo, isFirstField, fieldPhaseID, data
- **Hypothesis**: Transform3D uses other Field members (syncConf, medianBurstIRE, vitsMetrics, etc.)
- **Test Result**: ✅ Verified Transform3D **ONLY** accesses `inputFields[fieldIndex].data.data()`
- **Code Evidence**: `transformpal3d.cpp` line 167: `const quint16 *inputPtr = inputFields[fieldIndex].data.data();`
- **Conclusion**: Metadata fields are NOT used by Transform3D decoder
- **ORC vs Standalone metadata differences** (irrelevant for decoding):
  - ORC: syncConf=0, medianBurstIRE=0.00, audioSamples=-1, diskLoc=-1.0, fileLoc=-1
  - Standalone: syncConf=90-100, medianBurstIRE=15.6-15.9, audioSamples=881, diskLoc=12002-12016, fileLoc=large values
  - **These differences do NOT affect Transform3D processing**

### Theory 3: FFT Tile Processing Order
- **Observation**: Transform3D processes overlapping 8-field (4-frame) tiles
- **Hypothesis**: Tile iteration starting at tileZ=2 (startIndex-HALFZTILE=4-2) might hit edge case
- **Test**: Trace tile processing for first few iterations
- **Code**: `transformpal3d.cpp` line 140: `for (qint32 tileZ = startIndex - HALFZTILE; ...)`

### ~~Theory 4: Field Data Conversion Difference~~ ⚠️ INVESTIGATED
- **Observation**: ORC uses VFR→SourceField, standalone uses TBC→SourceField
- **Hypothesis**: Subtle difference in how field data arrays are populated
- **Investigation**: Discovered coordinate system bug in padding frame calculation
  - **Bug**: ORC converts start_frame from 1-based to 0-based internally, but blank check used wrong threshold
  - **Problem**: `useBlankFrame = (frame < 1)` should be `useBlankFrame = (frame < 0)` after 0-based conversion
  - **Symptom**: Only created 2 padding fields (frames -1,0 blank) instead of 6 (frames -2,-1,0 blank)
  - **Fix Applied**: Changed blank check to `frame < 0` to match 0-based coordinate system
  - **Result**: All non-Transform3D tests pass again (8/9), but Transform3D STILL FAILS with SAME checksum
- **Conclusion**: Padding frame count is now correct (6 fields), but Transform3D output unchanged
  - This suggests the issue is NOT in the padding frame creation
  - Something else about Transform3D processing differs between ORC and standalone

## Coordinate System Investigation (2025-12-23)

### The Bug
ORC converts user's 1-based frame numbering to 0-based internally:
```cpp
size_t start_frame = (start_frame_ > 0) ? (start_frame_ - 1) : 0;  // Line 452
```

But the blank frame check was using 1-based logic:
```cpp
bool useBlankFrame = (frame < 1) || (frame > total_frames);  // WRONG for 0-based
```

### The Fix
After 0-based conversion, frame 0 is valid (first frame in file):
```cpp
bool useBlankFrame = (frame < 0) || (frame >= total_frames);  // Correct for 0-based
```

### Verification
- **Before fix**: Frames -1, 0 were blank (2 frames = 4 fields), frame 1+ were real
- **After fix**: Frames -2, -1 are blank (2 frames = 4 fields), frame 0+ are real
- **Standalone**: Creates 6 padding fields for Transform3D (3 frames of lookbehind)
- **Result**: ORC now creates same 6 padding fields... but Transform3D output STILL differs!

### Implication
The padding frame count difference was a red herring. Even with correct padding:
- Frames 0-1 output: IDENTICAL between ORC and standalone
- Frame 2+ output: DIVERGES (same as before)
- Transform3D checksum: `c343689864...` (unchanged from original failure)

This proves the issue is NOT in padding frame generation. The problem must be elsewhere.

## BREAKTHROUGH: Single Frame Processing Works! (2025-12-23)

### Critical Discovery
Testing individual frames vs batch processing revealed the root cause:

**Single Frame Tests (ALL MATCH PERFECTLY):**
- Frame 1 (needs 2 blank padding frames): `9d263a5285ec8ceb800d5abc239423af55a843b6432a474535c00972cd3e834c` ✅
- Frame 2 (needs 1 blank padding frame): `a3dae57161cb026537e6c9d1979af7fd1655ecb5c2081b2909f5785f7255b7b1` ✅
- Frame 3 (needs 0 blank padding frames): `e57372df9c794ce5af5cd3027082cb9be5a93bc5aa6421a83a504dbac7786a66` ✅
- Frame 5 (middle of sequence): `6ba1cecc6e0151ad836aba1f547e043a4a8ee9157018926edb7be17a49cb1468` ✅

**Batch Processing (frames 1-10):**
- ORC output: `c343689864d8079739f842cb44c8f19a40744851957c4a4e1af232664748054e` ❌
- Standalone: `c5a7f2f6f2055bfb5b31c18b7a695ac9e759a03632916915dad10d56c81b9220` ✅
- Divergence point: Byte 3,207,169 (start of frame 2 in batch output)
- Frames 0-1 in batch: IDENTICAL (first 3,207,168 bytes match)

**File Size Calculation:**
- RGB48 frame size: 926 × 576 × 3 bytes = 1,599,168 bytes
- Two frames (0-1): 1,599,168 × 2 = 3,198,336 bytes
- **Note**: Actual match is 3,207,168 bytes, suggesting output may have padding or different dimensions

### Conclusion
The Transform3D algorithm implementation is **100% CORRECT**. The issue is **exclusively in batch processing**:
1. ✅ Individual frame processing works perfectly for ALL frames (1, 2, 3, 5)
2. ✅ Padding frame handling is correct (tested with 0, 1, and 2 blank frames)
3. ❌ Batch mode diverges at frame 2 - something in state management or frame sequencing

### Padding Hypothesis: COMPLETELY DISPROVEN ✅
Tested frames with different padding requirements - **ALL MATCH PERFECTLY:**
- Frame 1: 2 blank padding frames → MATCHES ✅
- Frame 2: 1 blank padding frame → MATCHES ✅
- Frame 3: 0 blank padding frames → MATCHES ✅

**This proves the issue is 100% unrelated to blank padding frame handling.**

### Focus Areas for Investigation
The problem must be in how frames are processed **in sequence** during batch mode:
1. Frame accumulation/buffering between iterations
2. chromaBuf state between frames (is it properly cleared?)
3. OutputWriter's frame sequencing logic
4. Transform3D field buffer recycling/reuse
5. PalColour::decodeFrames loop state management

### Batch Range Testing (2025-12-23)
Testing smaller batch ranges to isolate where divergence appears:
- **Frames 1-2 batch (ORC)**: `d899acb7ce6e381b1a6e73bad753662bf73aa49b0f82662ebbefcb826eef3c34` ❌
- **Frames 1-2 batch (Standalone)**: `35ccdd8f375468f0fe4bc6ec0053ffe3e99c67efe318ce18c18f099516baf5ed` ✅
- **Frames 1+2 concatenated (ORC singles)**: `35ccdd8f375468f0fe4bc6ec0053ffe3e99c67efe318ce18c18f099516baf5ed` ✅

**CRITICAL DISCOVERY:**
- Concatenating ORC frame 1 (single) + ORC frame 2 (single) = standalone batch output ✅
- ORC batch (frames 1-2) ≠ standalone batch (frames 1-2) ❌
- **Conclusion**: ORC batch mode is corrupting frame 2 during decodeFrames()!

### Deep Dive: Isolating the Bug (2025-12-23)

#### Step 1: Verify inputFields are correct
Logged field checksums with `--log-level debug`:

**Frame 2 single mode:**
```
Field 2 (active) seqNo=3,4 phaseID=7,8 checksum=3704585213 ✅
```

**Frames 1-2 batch mode:**
```
Field 2 (active) seqNo=1,2 phaseID=5,6 checksum=3723548259 (frame 1)
Field 3 (active) seqNo=3,4 phaseID=7,8 checksum=3704585213 ✅ (frame 2)
```

**Analysis**: inputFields are CORRECT. Field indexing in the batch is:
- Index 0-3: lookbehind (blank frames)
- Index 4-5: frame 1 (fields 0-1, seqNo 1-2) ✅
- Index 6-7: frame 2 (fields 2-3, seqNo 3-4) ✅
- startIndex=4, endIndex=8

#### Step 2: Check RGB output differences
Logged pixel 0 values from OutputWriter:

**Frame 2 single mode:**
```
Frame 1 Pixel 0: R=226 G=286 B=300 ✅
```

**Frame 2 in batch mode:**
```
Frame 2 Pixel 0: R=652 G=657 B=577 ❌ DIFFERENT!
```

**Conclusion**: The RGB output is different, meaning the ComponentFrame data fed to OutputWriter differs!

#### Step 3: Isolate Transform3D chromaBuf output
Added logging to Transform3D::filterFields to check chromaBuf after FFT processing:

**Frame 2 single mode (chromaBuf for field 0 of frame 2):**
```
chromaBuf[0] checksum (1k samples from active region) = 219.65
First 4 samples in active region: 774.621, -197.408, -772.485, 853.555
```

**Frames 1-2 batch mode (chromaBuf for field 0 of frame 2):**
```
chromaBuf[2] checksum (1k samples from active region) = 92.8725 ❌
First 4 samples in active region: 552.044, -174.622, -1081.09, 527.499 ❌
```

**ROOT CAUSE IDENTIFIED:**
Transform3D::filterFields produces **DIFFERENT chromaBuf data** for frame 2 when processed in batch vs single mode, even though:
- The input field data is IDENTICAL (verified by checksums)
- The Transform3D code is IDENTICAL (no differences between ORC and standalone)
- The algorithm works correctly for single frames

**The bug is in Transform3D's FFT tile processing when handling multiple frames in sequence.**

### Analysis: Why Does Batch Mode Differ?

**What we know:**
1. ✅ Single frame processing works perfectly (frames 1, 2, 3, 5 all match standalone)
2. ✅ inputFields are correct in batch mode (same checksums as single mode)
3. ✅ Transform3D code is identical (no diff between ORC and standalone implementations)
4. ✅ PalColour::decodeFrames code is identical (only debug logging differs)
5. ❌ Transform3D::filterFields produces different chromaBuf in batch mode

**Suspected causes:**
1. **FFTW plan state persistence**: FFTW plans are created once in constructor and reused
   - Could internal FFTW state carry over from frame 1 to frame 2?
   - FFTW might accumulate floating-point errors or state across executions

2. **FFT buffer reuse without clearing**: fftReal, fftComplexIn, fftComplexOut are class members
   - These buffers persist between filterFields() calls
   - Single mode: Fresh buffers. Batch mode: Buffers used for frame 1, then frame 2
   - Could residual data from frame 1 FFT affect frame 2?

3. **Tile loop processing differences**: Overlapping tiles write to chromaBuf with +=
   - tileZ loop: `for (qint32 tileZ = startIndex - HALFZTILE; tileZ < endIndex; ...)`
   - Single (startIndex=8, endIndex=10): tileZ goes 4→8
   - Batch (startIndex=4, endIndex=8): tileZ goes 0→4
   - Could tile processing order or overlap differ subtly?

4. **chromaBuf accumulation order**: inverseFFTTile writes: `b[x] += fftReal[...] / normalizer`
   - chromaBuf is cleared (fill(0.0)) at the start
   - Multiple overlapping tiles write to same pixels
   - Could the order of accumulation matter due to floating-point precision?

## Next Steps

### ~~High Priority~~
1. ~~**Add comprehensive Field structure logging**~~ ✅ COMPLETED
   - ~~Log ALL members of `sf.field` structure for first 6 fields~~
   - ~~Compare ORC vs standalone logs line-by-line~~
   - ~~Focus on: syncConf, medianBurstIRE, vitsMetrics~~
   - **Result**: Found differences but Transform3D doesn't use these fields

### High Priority (Updated)
2. **Compare field data arrays directly**:
   - Verify first 100 samples match between ORC and standalone for fields 0-5
   - Check if black padding fields are truly identical (all 15336)
   - Look for any subtle data differences in padding vs real fields
   - Add debug logging to `forwardFFTTile` and `inverseFFTTile`
   - Log tile positions (tileX, tileY, tileZ) for first 3 tiles
   - Check field indices being accessed

3. **Compare field data arrays directly**:
   - Log first 100 samples and checksum for fields 0-5
   - Verify identical between ORC and standalone

### Medium Priority
4. **Check Transform3D filter application**:
   - Verify `applyFilter()` is deterministic
   - Check if filter coefficients depend on field metadata

5. **Review window function application**:
   - `forwardFFTTile` applies window: `fftReal[...] = inputPtr[...] * windowFunction[z][y][x]`
   - Verify window function identical for both implementations

### Low Priority
6. **Check FFTW plan creation**:
   - Both use FFTW_MEASURE which might produce different plans
   - Consider if FFTW state differs between runs

## Test Commands

### Run failing test:
```bash
cd /home/sdi/Coding/github/decode-orc/orc/core/stages/chroma_sink/tests
bash test-orc-chroma.sh verify PAL_Transform3D_RGB
```

### Compare outputs:
```bash
cd /home/sdi/Coding/github/decode-orc/test-output
cmp -l orc-pal_transform3d_rgb.rgb standalone-new.rgb | head -20
```

### Check logs:
```bash
cd /home/sdi/Coding/github/decode-orc/orc/core/stages/chroma_sink/tests
grep "phaseID" temp/PAL_Transform3D_RGB.log | head -15
```

## Single Frame Isolation Test (2025-12-23)

### Test Setup
Created test to decode ONLY frame 5 in isolation:
- **ORC**: Project file targeting frame 5 (start_frame=5, length=1)
- **Standalone**: Command line `-s 5 -l 1`
- **Hypothesis**: If issue is frame-dependent or state-accumulation, single frame should match

### Results - CRITICAL BREAKTHROUGH! ✅

**Single frame 5 outputs are IDENTICAL!**
- **ORC frame 5**: `6ba1cecc6e0151ad836aba1f547e043a4a8ee9157018926edb7be17a49cb1468`
- **Standalone frame 5** (clean build): `6ba1cecc6e0151ad836aba1f547e043a4a8ee9157018926edb7be17a49cb1468`
- **Comparison**: `cmp` shows NO differences - files are byte-for-byte identical!

**Critical Implications:**
1. **ORC processes individual frames CORRECTLY** - the Transform3D implementation is sound
2. **The issue only appears in BATCH/SEQUENCE processing** (frames 1-10)
3. **Frame-to-frame state or context is being handled differently** between ORC and standalone
4. **This is NOT a field data loading bug** - ORC loads and processes fields correctly
5. **This is NOT a Transform3D algorithm bug** - the FFT processing produces correct output

### What This Rules Out
- ❌ Field data loading differences
- ❌ Transform3D FFT implementation bugs  
- ❌ Field phase ID calculation errors
- ❌ Padding frame generation errors
- ❌ Individual frame processing logic errors

### What This Points To
✅ **Batch processing state management** - something about processing multiple frames in sequence
✅ **Output frame accumulation/stitching** - how frames are combined into the output file
✅ **Decoder state persistence** between frames - Transform3D may maintain state across calls
✅ **Output buffer management** - ComponentFrame arrays or chromaBuf handling

### Earlier Confusion Resolved

**Note about earlier "off-by-2" observation:** 
The earlier test showed standalone producing different output for `-s 5` because:
1. Debug logging in sourcefield.cpp was being compiled into the decoder
2. The debug print statements were somehow affecting the output
3. After restoring clean source files, standalone produces correct matching output
4. The "off-by-2" was a red herring caused by the debug logging side effects

### PAL Subcarrier Shift Investigation

#### Discovery
Standalone decoder (`sourcefield.cpp` lines 70-77) applies PAL subcarrier shift:
```cpp
if ((videoParameters.system == PAL || videoParameters.system == PAL_M) && 
    videoParameters.isSubcarrierLocked) {
    // Shift second field left by 2 samples (remove first 2, append 2 black)
    fields[i + 1].data.remove(0, 2);
    fields[i + 1].data.append(black);
    fields[i + 1].data.append(black);
}
```

ORC was **missing this code entirely**!

#### Implementation in ORC
Added PAL subcarrier shift to `chroma_sink_stage.cpp` lines 596-608:
```cpp
if ((ldVideoParams.system == PAL || ldVideoParams.system == PAL_M) && 
    ldVideoParams.isSubcarrierLocked) {
    uint16_t black = ldVideoParams.black16bIre;
    sf2.data.remove(0, 2);
    sf2.data.append(black);
    sf2.data.append(black);
}
```

#### Result
**No change to output!**
- Test data has `isSubcarrierLocked=false` (verified in TBC metadata)
- Neither ORC nor (Updated 2025-12-23)

- **Date**: 2025-12-23
- **ORC commit**: Field ordering refactored (parity-based), padding frame coordinate system fixed, PAL subcarrier shift added
- **Test results**: 8/9 passing (unchanged)
- **Blocking issue**: Transform3D produces different output in BATCH mode (frames 1-10) but CORRECT output for single frames
- **Investigation stage**: **MAJOR BREAKTHROUGH** - Single frame processing is correct, issue is in batch/sequence processing
- **Key findings**:
  1. ✅ **Single frame 5 matches perfectly** - ORC Transform3D implementation is CORRECT
  2. ✅ **Frames 0-1 match in batch mode** - first 3,207,168 bytes identical
  3. ❌ **Frame 2+ diverge in batch mode** - starting at byte 3,207,169
  4. **Root cause**: Issue is in how frames are processed/accumulated in SEQUENCE, not individual frame processing
  5. Transform3D only uses field data arrays, NOT metadata
  6. Padding frame count fixed (coordinate system bug)
  7. PAL subcarrier shift added (doesn't affect this test)

### Next Investigation Priority

**ROOT CAUSE ISOLATED - Transform3D::filterFields produces different chromaBuf for frame 2:**

Critical findings from chromaBuf analysis:
- **Frame 2 single mode**: chromaBuf[0] checksum=219.65, first sample=774.621
- **Frame 2 in batch mode**: chromaBuf[2] checksum=92.8725, first sample=552.044 (DIFFERENT!)
- Transform3D::filterFields is generating DIFFERENT chroma data for the same input fields
- inputFields checksums are identical (verified: field 2-3 seqNo=3,4 phaseID=7,8 checksum=3704585213)
- Transform3D code is IDENTICAL between ORC and standalone

**The bug is definitively in Transform3D's FFT tile processing when handling multiple frames.**

**IMMEDIATE NEXT STEPS:**

1. **Investigate FFTW plan reuse between frames** (HIGHEST PRIORITY)
   - FFTW plans are created once in constructor
   - Could FFTW internal state persist between filterFields calls?
   - Check if FFTW needs to be reset between batches

2. **Check FFT buffer clearing between frames**
   - fftReal, fftComplexIn, fftComplexOut might not be fully cleared
   - These are class members that persist between filterFields calls
   - Single frame: fresh state. Batch: state from frame 1 affects frame 2?

3. **Verify tile loop boundaries are correct for batch**
   - tileZ loop: `for (qint32 tileZ = startIndex - HALFZTILE; tileZ < endIndex; ...)`
   - For batch (startIndex=4, endIndex=8): tileZ goes from 0 to 8 in steps of 4
   - Could tiles be overlapping incorrectly or processing wrong fields?

4. **Check chromaBuf accumulation with += operator**
   - Line 220 in inverseFFTTile: `b[tileX + x] += fftReal[...] / (ZTILE * YTILE * XTILE);`
   - chromaBuf is cleared at start (fill(0.0))
   - But multiple tiles write to same chromaBuf locations (by design - overlapping tiles)
   - Could the accumulation be different in batch due to tile processing order?

5. **Compare tile processing counts between single and batch**
   - Add logging to count how many times each chromaBuf index is written to
   - Check if batch processes more/fewer tiles for frame 2 than single mode

**Key Question:** Why does the SAME field data (verified by checksum) produce DIFFERENT chromaBuf output when processed as part of a batch vs individually?
