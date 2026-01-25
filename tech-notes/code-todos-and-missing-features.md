# Code TODOs and Missing Features

**Last Updated:** 25 January 2026

This document tracks TODO comments, HACKs, and missing implementations found in the codebase.

**Verification Status:** All items verified against current source code (25 Jan 2026)

## Summary

- **Total items tracked:** 21 (down from 24)
- **Active implementation gaps:** 7 (items #3, #6, #7, #8, #16, #17, #4)
- **Working code with TODOs:** 9 (items #2, #5, #9, #10, #11, #12, #13, #22, #23)
- **Known bugs/hacks:** 5 (items #18, #19, #20, #21, #23)
- **Aspirational features:** 1 (item #14)
- **Resolved and removed:** 3 (observer system refactor, IRE level scaling, parameter dependency logic - removed 25 Jan 2026)

**Key Findings:**
- Most TODOs are legitimate and still active
- Several are placeholder comments for working-but-suboptimal code
- Audio/EFM loading are the most significant missing features
- PAL color decoder has several unexplained constants requiring research

## High Priority Implementation Items

### GUI Components

2. **Y/C Sample Separation** - `orc/gui/render_coordinator.cpp:632`
   - TODO: Extend RenderPresenter to provide Y/C samples separately
   - Context: Render coordination needs separate luminance/chrominance access

3. **FFmpeg Codec Probing** - `orc/gui/ffmpegpresetdialog.cpp:489`
   - TODO: Actually probe FFmpeg codecs using `avcodec_find_encoder_by_name`
   - Context: FFmpeg preset dialog should validate available codecs

4. **Custom Range Parsing** - `orc/gui/masklineconfigdialog.cpp:257`
   - TODO: Parse custom ranges for the custom section
   - Context: Mask line configuration dialog

5. **Field Descriptor Coordinate Mapping** - `orc/gui/mainwindow.cpp:3629`
   - TODO: Get actual field descriptor to properly map coordinates
   - Context: Main window coordinate transformation

## Core Library Missing Features

### Media Loading

6. **Audio Loading** - `orc/core/tbc_yc_video_field_representation.cpp:558`
   - TODO: Implement audio loading
   - Current: `has_audio_ = false;` (hardcoded)
   - Context: TBC Y/C video field representation

7. **EFM Loading** - `orc/core/tbc_yc_video_field_representation.cpp:580`
   - TODO: Implement EFM loading
   - Current: `has_efm_ = false;` (hardcoded)
   - Context: LaserDisc EFM data extraction

### Stage Implementations

8. **Dropout Correction - Explicit Lists** - `orc/core/stages/dropout_correct/dropout_correct_stage.cpp:272`
   - TODO: Support explicit dropout list and decisions
   - Context: Dropout correction stage needs manual dropout specification

9. **Dropout Correction - Video Parameters** - `orc/core/stages/dropout_correct/dropout_correct_stage.cpp:307`
   - TODO: Get these from video parameters
   - Context: Currently hardcoded values that should come from metadata

10. **FFmpeg Deinterlace Parameter** - `orc/core/stages/chroma_sink/ffmpeg_output_backend.cpp:555`
    - TODO: Check for apply_deinterlace parameter
    - Context: FFmpeg output backend deinterlacing control

11. **LD Sink Observations** - `orc/core/stages/ld_sink/ld_sink_stage.cpp:62`
    - TODO: Use for observations
    - Current: `(void)observation_context;` (unused parameter)
    - Context: LaserDisc sink stage observation integration

12. **Stacker Neighbor Modes** - `orc/core/stages/stacker/stacker_stage.cpp:1105`
    - TODO: Implement neighbor modes (3, 4)
    - Current: Parameter marked as unused
    - Context: Field stacking with neighbor field analysis

### Performance Optimizations

13. **Comb Filter Array Caching** - `orc/core/stages/chroma_sink/decoders/comb.cpp:962`
    - TODO: Cache arrays instead of reallocating every field
    - Context: Performance optimization for comb filter decoder

### Rendering

14. **Future Output Types** - `orc/core/preview_renderer.cpp:402`
    - TODO: Future output types
    - Status: **PARTIALLY IMPLEMENTED**
      - ✅ Luma: Rendering code exists (line 470) but not exposed in available outputs (line 402)
      - ❌ Chroma: Not implemented (line 565 - returns "not yet implemented")
      - ❌ Composite: Not implemented (line 566 - returns "not yet implemented")
    - Context: Preview renderer output format expansion
    - Note: Luma just needs to be added to `get_available_outputs()` to be fully functional

## Presenter Layer

16. **Observation Serialization** - `orc/presenters/src/render_presenter.cpp:645`
    - TODO: Serialize observations to JSON
    - Context: Render presenter observation data export

17. **Cache Statistics** - `orc/presenters/src/render_presenter.cpp:665`
    - TODO: Implement cache stats
    - Context: Render presenter cache monitoring/reporting

## Known Issues and HACKs

### Color Decoder Issues

18. **PAL-M Vector Swap** - `orc/core/stages/chroma_sink/decoders/palcolour.cpp:134-136`
    - HACK: PAL-M ends up with vectors swapped and out of phase
    - TODO: Find a proper solution to this
    - Context: PAL color decoder PAL-M format handling

19. **Unexplained Coefficient** - `orc/core/stages/chroma_sink/decoders/palcolour.cpp:169`
    - XXX: Where does the 0.5* come from?
    - Context: PAL color decoder math operation origin unclear

20. **Magic Number 130000** - `orc/core/stages/chroma_sink/decoders/palcolour.cpp:518`
    - XXX: Magic number 130000 - needs verification
    - Context: PAL color decoder constant value

21. **ZTILE Division Question** - `orc/core/stages/chroma_sink/decoders/transformpal3d.cpp:267`
    - XXX: Why ZTILE / 4? It should be (6 * ZTILE) / 8...
    - Context: Transform PAL 3D decoder calculation

22. **Chroma Alignment Shift** - `orc/core/stages/chroma_sink/decoders/comb.cpp:696`
    - TODO: Needed to shift the chroma 1 sample to the right to get it to line up
    - Context: Comb filter chroma alignment - needs investigation

### Architecture HACKs

23. **Void Pointer Storage** - `orc/presenters/src/render_presenter.cpp:699`
    - Comment: "this is a hack - we're storing pointers as void* to avoid exposing the type"
    - Context: Render presenter data storage encapsulation workaround

## Development Notes

### HackDAC Sink Stage

The codebase includes a "HackDAC" sink stage that appears to be a specialized export format:
- Purpose: Exports signed 16-bit field data without half-line padding
- Files: `orc/core/stages/hackdac_sink/`
- Note: This appears to be a development/testing stage rather than a production feature

## Priority Assessment

### Critical (Affects Core Functionality)
- Audio loading (#6) - **Real missing feature**
- EFM loading (#7) - **Real missing feature**

### High (Feature Completeness)
- Dropout correction explicit lists (#8) - **Active implementation gap**
- FFmpeg codec probing (#3) - **Active implementation gap**
- Observation serialization (#16) - **Active implementation gap**

### Medium (Enhancements)
- Y/C sample separation (#2) - **Enhancement to working feature**
- Stacker neighbor modes (#12) - **Unused par
- Custom range parsing (#4) - **Partial impleameters**
- Cache statistics (#17) - **Returns placeholder string**mentation**
- Field coordinate mapping (#5) - **Uses approximation**

### Low (Technical Debt / Optimizations)
- Array caching (#13) - **Performance optimization**
- IRE level scaling (#15) - **Enhancement to working code**
- Video parameters source (#9) - **Uses hardcoded fallbacks**
- FFmpeg deinterlace check (#10) - **Missing parameter check**
- LD Sink observations (#11) - **Unused parameter**

### Aspirational (Future Features)
- Chroma/Composite output types (#14 - partial) - **Chroma and Composite not implemented**
  - Luma is implemented but not exposed

### Research Required (HACKs to Fix)
- PAL-M vector swap (#18) - **Known issue with workaround**
- Magic numbers in PAL decoder (#19, #20) - **Unclear origins**
- ZTILE calculation (#21) - **Questionable math**
- Chroma alignment shift (#22) - **Working but unexplained**
- Void pointer storage (#23) - **Architectural workaround**

## Related Documentation

- See `bugs/ntsc-chromanr-regression-failure.md` for NTSC chroma noise reduction implementation details
- See `tech-notes/iec-608-control-codes.md` for closed caption control codes

## Changelog

### 25 January 2026
- **Removed:** Item #24 (Observer system refactor) - Completed, stale TODOs cleaned up
- **Fixed:** Item #15 (IRE Level Scaling) - PreviewRenderer now uses proper IRE scaling from metadata
  - Updated `PreviewRenderer::tbc_sample_to_8bit()` to accept blackIRE and whiteIRE parameters
  - Modified render_field(), render_frame(), and render_split_frame() to pass IRE parameters
  - Now consistent with PreviewHelpers implementation
- **Implemented:** Item #1 (Parameter Dependency Logic) - GenericAnalysisDialog now supports parameter dependencies
  - Added `updateParameterDependencies()` implementation
  - Parameters are now conditionally enabled/disabled based on values of other parameters
  - Uses same logic as AnalysisDialog for consistency
  - Added required includes: `<algorithm>` and `<map>`
