# MP4 Output Support Migration Plan

## Overview

Add MP4/encoded video output support to the chroma decoder sink stage using FFmpeg's libav* libraries. The command-line `ffmpeg` tool is NOT required - only the libraries (libavcodec, libavformat, libavutil, libswscale).

## Architecture

### Current State
- `ChromaSinkStage::writeOutputFile()` directly uses `OutputWriter` class
- Supports raw formats: RGB48, YUV444P16, Y4M
- Writes directly to file via `std::ofstream`

### Target State
```
ComponentFrame[] → OutputBackend (abstract) → File
                   ├─ RawOutputBackend (wraps existing OutputWriter)
                   └─ FFmpegOutputBackend (libav* encoding)
```

## Migration Phases

### Phase 1: Create Abstraction Layer ✅ COMPLETE

**Goal**: Refactor existing code to use backend abstraction without changing behavior.

**New Files**:
- `orc/core/stages/chroma_sink/output_backend.h` - Abstract base class
- `orc/core/stages/chroma_sink/raw_output_backend.h` - Header for raw output
- `orc/core/stages/chroma_sink/raw_output_backend.cpp` - Implementation wrapping OutputWriter
- `orc/core/stages/chroma_sink/ffmpeg_output_backend.h` - Header for FFmpeg encoding
- `orc/core/stages/chroma_sink/ffmpeg_output_backend.cpp` - Basic H.264 MP4 implementation

**Modified Files**:
- `orc/core/stages/chroma_sink/chroma_sink_stage.cpp` - Use backend factory pattern
- `orc/core/stages/chroma_sink/CMakeLists.txt` - Add new source files, FFmpeg dependency
- `orc/core/CMakeLists.txt` - Find and link FFmpeg libraries

**Deliverables**:
- ✓ OutputBackend abstract interface
- ✓ RawOutputBackend (maintains exact current behavior)
- ✓ FFmpegOutputBackend with H.264 support
- ✓ Factory pattern for backend selection
- ✓ FFmpeg as optional compile-time dependency
- ✓ New format: `mp4-h264` in `output_format` parameter
- ✓ Backward compatibility: existing formats unchanged

**Testing**:
- Verify existing rgb/yuv/y4m outputs are identical to before
- Test mp4-h264 output produces valid playable files
- Verify builds work with and without FFmpeg libraries

### Phase 2: Codec Expansion (Future)

**Goal**: Add more codec options and quality controls.

**New Codecs**:
- H.265/HEVC: `mp4-h265` (better compression)
- FFV1: `mkv-ffv1` (lossless archival)
- ProRes: `mov-prores` (professional editing)

**New Parameters** (optional):
- `encoder_preset`: "fast", "medium", "slow", "veryslow"
- `encoder_crf`: 18-28 (quality setting for H.264/H.265)
- `encoder_bitrate`: explicit bitrate control (alternative to CRF)

**Optimizations**:
- Progress reporting for long encodes
- Frame-level threading in FFmpeg
- Investigate hardware acceleration (NVENC, QSV, VideoToolbox)

### Phase 3: Advanced Features (Future)

**Additional Features**:
- Metadata embedding (title, source info, timecode)
- Audio track support (if VBI audio decoding added)
- Two-pass encoding for better quality/size ratio
- Custom pixel format selection per codec
- GOP structure control for better seeking

## Technical Details

### FFmpeg Library Dependencies

**Required Libraries** (not the command-line tool):
- `libavcodec` - Video/audio encoding and decoding
- `libavformat` - Container muxing and demuxing  
- `libavutil` - Utilities and common functions
- `libswscale` - Pixel format conversion and scaling

**CMake Detection**:
```cmake
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG libavformat libavcodec libavutil libswscale)
    if(FFMPEG_FOUND)
        add_definitions(-DHAVE_FFMPEG)
        target_include_directories(orc-core PRIVATE ${FFMPEG_INCLUDE_DIRS})
        target_link_libraries(orc-core PRIVATE ${FFMPEG_LIBRARIES})
    endif()
endif()
```

### H.264 Encoding Parameters (Phase 1)

**Initial settings for archival quality**:
- Codec: `libx264`
- Preset: `medium` (good speed/quality balance)
- CRF: `18` (visually transparent)
- Pixel Format: `yuv444p` (full chroma resolution, no subsampling)
- Colorspace: `bt470bg` (PAL) or `smpte170m` (NTSC) - matches BT.601
- Color Range: `tv` (limited range, matches component output)
- Frame Rate: 25fps (PAL) or 29.97fps (NTSC)

### Backward Compatibility

**Guaranteed**:
- All existing `output_format` values work identically
- Projects using "rgb", "yuv", "y4m" are unaffected
- No changes to existing file formats or layouts
- Build succeeds without FFmpeg (MP4 formats unavailable)

## Implementation Notes

### Error Handling
- Clear error message if MP4 format requested but FFmpeg unavailable
- Validate codec availability at runtime (some builds may lack codecs)
- Handle encoding failures gracefully with detailed logging
- Warn about slow encoding for large frame sizes

### Testing Strategy
- Unit tests for backend factory
- Integration tests comparing raw output before/after refactor
- Visual verification of MP4 output quality
- Performance testing (threading, encoding speed)
- Cross-platform testing (Linux, macOS, Windows if applicable)

### Documentation Updates
- Update user documentation with new format options
- Add examples of MP4 output in project files
- Document FFmpeg library requirements in build instructions
- Add troubleshooting section for encoding issues

## Timeline

- **Phase 1**: Initial implementation (current)
- **Phase 2**: When additional codecs/quality controls needed
- **Phase 3**: When advanced features requested

## Dependencies

**Build-time** (optional):
- FFmpeg development packages: `libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswscale-dev`
- pkg-config for library detection

**Runtime** (when built with FFmpeg):
- FFmpeg runtime libraries (typically auto-installed with dev packages)
- H.264 encoder (libx264) included in most FFmpeg builds

## Success Criteria

### Phase 1
- [x] Design approved
- [x] Code compiles with and without FFmpeg
- [x] Existing raw formats produce identical output
- [x] MP4-H.264 output implemented
- [x] No performance regression for existing formats
- [x] Documentation updated

### Phase 2
- [ ] Additional codecs implemented and tested
- [ ] Quality parameters functional
- [ ] User documentation includes codec selection guide

### Phase 3
- [ ] Advanced features functional
- [ ] Performance optimized
- [ ] All planned features complete
