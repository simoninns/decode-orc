# Transform3D Batch Processing Debug Investigation

## Issue Summary
PAL_Transform3D_RGB test was failing (1/9 tests) with output differing from standalone reference.

## Root Cause Discovery

### Initial Hypotheses (Eliminated)
1. ❌ **Field metadata differences** - Transform3D doesn't use field metadata
2. ❌ **Padding frame calculation bug** - Found and fixed coordinate system bug (`frame < 0` vs `frame < 1`), but didn't fix the divergence
3. ❌ **Missing PAL subcarrier shift** - Added for completeness, but test uses `isSubcarrierLocked=false`
4. ❌ **FFT buffer state persistence** - Buffers are properly initialized:
   - `fftReal`: Completely overwritten in `forwardFFTTile`
   - `fftComplexOut`: Explicitly cleared in `applyFilter`
   - `fftComplexIn`: Written by FFTW during forward FFT execution

### Breakthrough: Single Frame Tests
Testing frames individually revealed ALL frames work perfectly when processed alone:
- Frame 1 alone: ✅ Matches standalone
- Frame 2 alone: ✅ Matches standalone  
- Frame 3 alone: ✅ Matches standalone
- Frame 5 alone: ✅ Matches standalone

But processing frames 1-2 together: ❌ Frame 2 diverges

### Critical Insight: Temporal Position Dependency

Transform3D is a **3D temporal FFT filter** that processes the Z-axis (temporal dimension). The FFT results depend on WHERE a frame is positioned in the temporal field array.

**Standalone decoder behavior:**
```cpp
// From SourceField::loadFields()
qint32 frameNumber = firstFrameNumber - lookBehindFrames;
for (qint32 i = 0; i < fields.size(); i += 2) {
    // Process frame...
    frameNumber++;
}
```

When processing frames ONE AT A TIME:
- Frame 2 processing: 
  - Fields: [3 lookbehind frames] + [frame 2] + [4 lookahead frames]
  - Frame 2 is at **field indices 6-7** (after 3×2 lookbehind fields)

**ORC behavior (before fix):**
When processing frames 1-5 in ONE BATCH:
- Fields: [3 lookbehind frames] + [frame 1] + [frame 2] + [frame 3] + [frame 4] + [frame 5] + [4 lookahead frames]
- Frame 1 at field indices 6-7
- Frame 2 at field indices **8-9** ⚠️ Different Z-position!
- Frame 3 at field indices 10-11
- etc.

**The 3D FFT filter produces different results when the same frame data is at different Z-positions!**

### Verification
Created 2-frame batch test:
- Processing frames 1-2 together → Frame 2 diverges
- Concatenating frame 1 (processed alone) + frame 2 (processed alone) → Perfect match with standalone

This proved the algorithm is correct; only the temporal positioning differs.

## The Fix

Modified `ChromaSinkStage::trigger()` to process frames **one at a time** in a loop:

```cpp
// OLD: Process all frames in one batch
palDecoder->decodeFrames(inputFields, startIndex, endIndex, outputFrames);

// NEW: Process each frame individually at the same temporal position
for (qint32 frameIdx = 0; frameIdx < numFrames; frameIdx++) {
    QVector<SourceField> frameFields;
    // Extract [lookbehind + target frame + lookahead] for this frame
    for (qint32 i = lookbehindIdx; i < lookaheadEndIdx && i < inputFields.size(); i++) {
        frameFields.append(inputFields[i]);
    }
    
    // Always decode at the same Z-position: after lookbehind fields
    qint32 frameStartIndex = lookBehindFrames * 2;
    qint32 frameEndIndex = frameStartIndex + 2;
    
    palDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
    outputFrames[frameIdx] = singleOutput[0];
}
```

Each frame is now processed at **the same temporal Z-position** (field indices `lookBehind*2` to `lookBehind*2+2`), matching standalone behavior.

## Test Results

### Before Fix
- Quick tests: 8/9 passing (PAL_Transform3D_RGB failing)
- Full suite: Not run

### After Fix
- Quick tests: **9/9 passing** ✅
- Full suite: **24/24 passing** ✅

### Key Checksums
All frame checksums now match between ORC and standalone reference outputs.

## Lessons Learned

1. **Temporal filters are position-dependent**: 3D FFT filters that process the temporal dimension can produce different results for the same data at different temporal positions.

2. **Batch processing requires careful design**: When porting batch-processing code, understand whether the algorithm is position-dependent and ensure batch behavior matches single-item behavior.

3. **Single-item tests are invaluable**: Testing individual items revealed the algorithm was correct; the issue was only in batch coordination.

4. **Read the reference implementation carefully**: The standalone decoder's one-frame-at-a-time processing wasn't arbitrary—it ensures consistent temporal positioning.

## Related Files
- Fix: `orc/core/stages/chroma_sink/chroma_sink_stage.cpp` (lines 625-664)
- Transform3D implementation: `orc/core/stages/chroma_sink/decoders/transformpal3d.cpp`
- Standalone reference: `orc-chroma-decoder/src/decoderpool.cpp` (`getInputFrames()`)
- Test suite: `orc-chroma-decoder/tests/run-tests.sh`

## Date
Fixed: 23 December 2025
