# YC Source Implementation Plan

**Date**: January 11, 2026  
**Status**: Design Phase  
**Target Version**: TBD

## Overview

This document outlines the implementation plan for supporting YC sources where luma (Y) and chroma (C) are captured in separate files, as opposed to composite sources where Y and C are modulated together in a single file.

**Note**: YC sources (separate Y and C files) should not be confused with "component video" which refers to Y/Pb/Pr or RGB signals. YC sources contain separated luma and modulated chroma, typically from color-under formats like VHS.

### Motivation

Different video sources provide signals in different formats:
- **Composite sources** (LaserDisc, composite captures): Y+C modulated together → `.tbc` file
- **YC sources** (color-under tapes like VHS/Betamax): Y and C separated → `.tbcy` + `.tbcc` files

Supporting YC sources provides significant quality advantages:
- **No comb filter artifacts on luma** - Y is already clean
- **Simpler chroma decoding** - only demodulation needed, no Y/C separation
- **Better preservation** of original signal quality

### File Format

**YC source files**:
- `.tbcy` - Pure luma samples (16-bit, one sample per time position)
- `.tbcc` - Pure chroma samples (16-bit, modulated, one sample per time position)
- `.tbc.db` - Shared metadata (SQLite database, same format as composite)

**Properties**:
- Y and C files have **identical field counts** and **identical geometry**
- Each Y sample has a **corresponding C sample** at the same position
- **Single dropout map** in metadata applies to both channels (dropouts affect Y and C together)
- Both are 16-bit samples with same field structure

---

## Architecture Overview

### Current Architecture (Composite)

```
TBCVideoFieldRepresentation
├─ TBCReader (single .tbc file)
├─ TBCMetadataReader (.tbc.db)
└─ Interface: get_line() → uint16_t* (modulated Y+C)
     ↓
VideoFieldRepresentationWrapper chain
     ↓
ChromaSinkStage
└─ Comb filter: Separate Y from C + Demodulate C → U/V
```

### New Architecture (YC)

```
TBCYCVideoFieldRepresentation
├─ TBCReader (Y) (.tbcy file)
├─ TBCReader (C) (.tbcc file)
├─ TBCMetadataReader (.tbc.db)
└─ Interface: get_line_luma() → uint16_t* (Y only)
              get_line_chroma() → uint16_t* (C only)
     ↓
VideoFieldRepresentationWrapper chain (dual channel)
     ↓
ChromaSinkStage
└─ YC decoder: Y (direct) + Demodulate C → U/V
   (NO comb filter Y/C separation needed!)
```

### Key Design Principles

1. **Channel mode propagates through entire pipeline** - once a source is YC, it stays YC until final decode
2. **Wrappers are channel-agnostic** - they forward both composite and YC interfaces
3. **Stages handle both modes** - check `has_separate_channels()` and branch accordingly
4. **No mixing in DAG** - a project must use exclusively composite OR exclusively YC sources (cannot mix both types)
5. **Quality preservation** - YC sources bypass comb filtering entirely

---

## Implementation Phases

### Phase 1: Core Interface Extensions

**Goal**: Extend VFR interface to support dual-channel access

**Components**:

1. **VideoFieldRepresentation interface** (`video_field_representation.h`)
   - Add `virtual bool has_separate_channels() const { return false; }`
   - Add `virtual const uint16_t* get_line_luma(FieldID, size_t) const { return nullptr; }`
   - Add `virtual const uint16_t* get_line_chroma(FieldID, size_t) const { return nullptr; }`
   - Add `virtual std::vector<uint16_t> get_field_luma(FieldID) const { return {}; }`
   - Add `virtual std::vector<uint16_t> get_field_chroma(FieldID) const { return {}; }`

2. **VideoFieldRepresentationWrapper** (`video_field_representation.cpp`)
   - Forward `has_separate_channels()` to source
   - Forward `get_line_luma()` to source
   - Forward `get_line_chroma()` to source
   - Forward `get_field_luma()` to source
   - Forward `get_field_chroma()` to source

**Testing**:
- Verify interface compiles
- Verify existing composite sources still work (return false/nullptr)
- Verify wrapper forwarding works

**Dependencies**: None  
**Estimated effort**: 2-4 hours

---

### Phase 2: YC Source Implementation

**Goal**: Implement Y/C source stages that read separate files

**Components**:

1. **TBCYCVideoFieldRepresentation** (`tbc_yc_video_field_representation.h/cpp`)
   ```cpp
   class TBCYCVideoFieldRepresentation : public VideoFieldRepresentation {
   public:
       TBCYCVideoFieldRepresentation(
           std::shared_ptr<TBCReader> y_reader,
           std::shared_ptr<TBCReader> c_reader,
           std::shared_ptr<TBCMetadataReader> metadata,
           ...);
       
       bool has_separate_channels() const override { return true; }
       const uint16_t* get_line_luma(FieldID, size_t) const override;
       const uint16_t* get_line_chroma(FieldID, size_t) const override;
       
       // Composite methods throw or return nullptr
       const uint16_t* get_line(FieldID, size_t) const override;
       
   private:
       std::shared_ptr<TBCReader> y_reader_;
       std::shared_ptr<TBCReader> c_reader_;
       std::shared_ptr<TBCMetadataReader> metadata_reader_;
       
       // Line caching (separate for Y and C)
       mutable LRUCache<std::pair<FieldID, size_t>, std::vector<uint16_t>> y_line_cache_;
       mutable LRUCache<std::pair<FieldID, size_t>, std::vector<uint16_t>> c_line_cache_;
   };
   ```

2. **Helper function** (`tbc_video_field_representation.h/cpp`)
   ```cpp
   std::shared_ptr<VideoFieldRepresentation> create_tbc_yc_representation(
       const std::string& y_path,
       const std::string& c_path,
       const std::string& metadata_path,
       const std::string& pcm_path = "",
       const std::string& efm_path = "");
   ```
   - Validate Y and C files have matching field counts
   - Validate geometry matches from metadata
   - Create dual TBCReader instances
   - Return TBCYCVideoFieldRepresentation

3. **Source stages** (`stages/ld_pal_yc_source/`, `stages/ld_ntsc_yc_source/`)
   - `LDPALYCSourceStage` / `LDNTSCYCSourceStage`
   - Parameters: `y_path`, `c_path`, `db_path`, `pcm_path`, `efm_path`
   - Display names: "PAL YC", "NTSC YC"
   - Call `create_tbc_yc_representation()` in execute()

4. **Registration** (`stage_init.cpp`)
   - Register new stages
   - Add force_link functions

**Testing**:
- Create synthetic Y/C test files (simple gradients)
- Verify Y and C can be read independently
- Verify field counts match
- Verify validation catches mismatched files
- Test with and without optional PCM/EFM

**Dependencies**: Phase 1  
**Estimated effort**: 1-2 days

---

### Phase 3: Transform Stage Updates

**Goal**: Update transform stages to handle dual-channel data

**Priority order** (implement in this sequence):

#### 3.1 Field Map Stage
**Why first**: Simple, commonly used, good test case

```cpp
class FieldMappedRepresentation : public VideoFieldRepresentationWrapper {
    const uint16_t* get_line_luma(FieldID id, size_t line) const override {
        if (!source_->has_separate_channels()) {
            return VideoFieldRepresentationWrapper::get_line_luma(id, line);
        }
        FieldID mapped_id = field_mapping_[id.value()];
        return source_->get_line_luma(mapped_id, line);
    }
    
    const uint16_t* get_line_chroma(FieldID id, size_t line) const override {
        if (!source_->has_separate_channels()) {
            return VideoFieldRepresentationWrapper::get_line_chroma(id, line);
        }
        FieldID mapped_id = field_mapping_[id.value()];
        return source_->get_line_chroma(mapped_id, line);
    }
};
```

**Testing**:
- Field map with Y/C source
- Verify Y and C use same mapping (stay synchronized)
- Test range specifications

**Estimated effort**: 4-6 hours

#### 3.2 Dropout Correction Stage
**Why second**: Critical for quality, more complex

```cpp
class CorrectedVideoFieldRepresentation : public VideoFieldRepresentationWrapper {
private:
    // Dual caches for YC sources
    mutable LRUCache<FieldID, std::vector<uint16_t>> corrected_luma_fields_;
    mutable LRUCache<FieldID, std::vector<uint16_t>> corrected_chroma_fields_;
    
    void ensure_field_corrected(FieldID field_id) const {
        if (source_->has_separate_channels()) {
            // Correct Y channel using dropout map
            auto y_field = source_->get_field_luma(field_id);
            auto dropouts = source_->get_dropout_hints(field_id);
            auto corrected_y = apply_corrections(y_field, dropouts, /*channel=*/LUMA);
            corrected_luma_fields_.put(field_id, corrected_y);
            
            // Correct C channel using SAME dropout map
            auto c_field = source_->get_field_chroma(field_id);
            auto corrected_c = apply_corrections(c_field, dropouts, /*channel=*/CHROMA);
            corrected_chroma_fields_.put(field_id, corrected_c);
        } else {
            // Existing composite correction logic
        }
    }
};
```

**Key considerations**:
- Same dropout map applies to both channels
- Find replacement lines independently for Y and C
- Memory: 2× cache size for YC sources (840MB vs 420MB)
- Chroma phase matching works on C channel

**Testing**:
- Synthetic dropouts on Y/C files
- Verify both channels corrected
- Verify intrafield/interfield correction works
- Test overcorrect mode
- Verify highlight mode works on both channels

**Estimated effort**: 2-3 days

#### 3.3 Stacker Stage

```cpp
class StackedVideoFieldRepresentation {
    bool has_separate_channels() const override {
        return sources_.empty() ? false : sources_[0]->has_separate_channels();
    }
    
    void ensure_field_stacked(FieldID field_id) const {
        if (sources_[0]->has_separate_channels()) {
            // Stack Y samples across all sources
            std::vector<std::vector<uint16_t>> y_fields;
            for (auto& src : sources_) {
                y_fields.push_back(src->get_field_luma(field_id));
            }
            auto stacked_y = stack_samples(y_fields, method_);
            stacked_luma_cache_[field_id] = stacked_y;
            
            // Stack C samples across all sources (independently)
            std::vector<std::vector<uint16_t>> c_fields;
            for (auto& src : sources_) {
                c_fields.push_back(src->get_field_chroma(field_id));
            }
            auto stacked_c = stack_samples(c_fields, method_);
            stacked_chroma_cache_[field_id] = stacked_c;
        } else {
            // Existing composite stacking
        }
    }
};
```

**Validation**:
- All sources in the DAG must have same channel mode
- This is validated at the project/DAG level, not just stacker
- Error should be thrown when attempting to add a YC source to a project with composite sources (or vice versa)

**Testing**:
- Stack 3 Y/C sources
- Verify Y and C stacked independently
- Verify average/median/max methods work on both channels
- Test error when attempting to mix composite and YC sources in project

**Estimated effort**: 1-2 days

#### 3.4 Simple Transform Stages

**FieldInvertStage**, **MaskLineStage** - straightforward dual-channel ops:

```cpp
const uint16_t* get_line_luma(FieldID id, size_t line) const override {
    auto src_line = source_->get_line_luma(id, line);
    return apply_transform(src_line);  // Invert/mask/etc
}

const uint16_t* get_line_chroma(FieldID id, size_t line) const override {
    auto src_line = source_->get_line_chroma(id, line);
    return apply_transform(src_line);  // Same operation
}
```

**Testing**: Basic verification that operations work on both channels

**Estimated effort**: 4-8 hours

---

### Phase 4: Chroma Decoder Integration

**Goal**: Add YC decode path that bypasses comb filter Y/C separation

**Components**:

1. **SourceField extension** (`decoders/sourcefield.h`)
   ```cpp
   struct SourceField {
       orc::FieldMetadata field;
       
       // For composite sources
       std::vector<uint16_t> data;  // Combined Y+C
       
       // For YC sources
       std::vector<uint16_t> luma_data;   // Y only
       std::vector<uint16_t> chroma_data; // C only
       bool is_yc = false;
   };
   ```

2. **ChromaSinkStage::convertToSourceField** (dual path)
   ```cpp
   SourceField convertToSourceField(const VFR* vfr, FieldID id) {
       SourceField sf;
       sf.field = get_field_metadata(vfr, id);
       
       if (vfr->has_separate_channels()) {
           sf.is_yc = true;
           sf.luma_data = vfr->get_field_luma(id);
           sf.chroma_data = vfr->get_field_chroma(id);
       } else {
           sf.is_yc = false;
           sf.data = vfr->get_field(id);
       }
       return sf;
   }
   ```

3. **Comb filter YC path** (`decoders/comb.cpp`)
   ```cpp
   void Comb::decodeFrames(const std::vector<SourceField>& inputFields, ...) {
       if (inputFields[0].is_yc) {
           // YC DECODE PATH
           for (int i = 0; i < frame_count; i++) {
               auto& field1 = inputFields[startIndex + i*2];
               auto& field2 = inputFields[startIndex + i*2 + 1];
               
               // Y is already clean - direct copy
               copyLumaToFrame(field1.luma_data, field2.luma_data, 
                              componentFrames[i]);
               
               // C needs demodulation but NOT separation
               demodulateChromaOnly(field1.chroma_data, field2.chroma_data,
                                   componentFrames[i]);
               
               // Apply chroma gain/phase
               transformIQ(componentFrames[i]);
               
               // Optional chroma/luma noise reduction
               if (config.luma_nr > 0) doYNR(componentFrames[i]);
               if (config.chroma_nr > 0) doCNR(componentFrames[i]);
           }
       } else {
           // COMPOSITE DECODE PATH (existing)
           // Full comb filter: separate Y from C, then demodulate
           // ...existing code...
       }
   }
   ```

4. **YC chroma demodulation**
   - Extract existing demodulation logic from comb filter
   - Create `demodulateChromaOnly()` function
   - Input: modulated C samples from both fields
   - Output: U and V frames
   - Skip all Y/C separation (1D/2D/3D comb, phase matching)

**Testing**:
- Synthetic Y/C with known color bars
- Verify output matches expected RGB
- Compare YC vs composite decode quality
- Verify no comb artifacts on luma from YC sources
- Test chroma gain/phase adjustments work
- Test chroma/luma NR on YC sources

**Dependencies**: Phase 3 (need working pipeline to sink)  
**Estimated effort**: 3-5 days

---

### Phase 5: Preview and Visualization

**Goal**: Update UI to show Y and C channels separately

**Components**:

1. **Preview options** - channel selector
   ```cpp
   std::vector<PreviewOption> get_preview_options() const {
       if (has_separate_channels()) {
           return {
               {"preview_luma", "Luma (Y) Channel", ...},
               {"preview_chroma", "Chroma (C) Channel", ...},
               {"preview_composite", "Composite View (Y+C)", ...}
           };
       } else {
           return {{"preview_field", "Video Field", ...}};
       }
   }
   ```

2. **Preview rendering** (`preview_helpers.cpp`)
   - `render_field_luma()` - grayscale Y channel
   - `render_field_chroma()` - false-color or grayscale C
   - `render_field_component()` - decode Y+C to RGB for preview

3. **Line scope** (`gui/linescopedialog.cpp`)
   - Add channel selector: Composite / Luma / Chroma / Both Overlay
   - Plot Y in green, C in cyan
   - Overlay mode shows both on same axes

4. **Vectorscope** - use C channel directly (cleaner than composite)

5. **Dropout overlay** - mark same regions on Y and C previews

6. **SNR/Quality metrics** - separate Y and C SNR calculations

7. **VBI decoder** - use Y channel (cleaner decode)

**Testing**:
- Preview Y channel shows clean luma
- Preview C channel shows modulated chroma
- Composite preview decodes to RGB correctly
- Line scope shows both channels
- Vectorscope cleaner for component sources
- Dropout overlay on both previews

**Dependencies**: Phase 4 (for composite preview decode)  
**Estimated effort**: 3-4 days

---

### Phase 6: GUI Integration

**Goal**: User-friendly source selection and validation

**Components**:

1. **Stage parameter dialog** (`gui/stageparameterdialog.cpp`)
   - File picker for `.tbcy` (auto-discover `.tbcc` and `.tbc.db`)
   - Validation feedback (field count match, geometry match)
   - Clear error messages for mismatched files

2. **Node palette** - add new source types
   - "PAL YC" entry
   - "NTSC YC" entry
   - Icons/badges to distinguish from composite

3. **Node display** - show YC indicator
   - Badge: "YC" on Y/C source nodes
   - Different color scheme for YC vs composite

4. **Project validation**
   - **Prevent** adding YC sources to composite projects (and vice versa)
   - Validate at project level that all sources have same mode
   - Clear error message: "Cannot mix composite and YC sources in the same project. All sources must be the same type."

**Testing**:
- Create project with Y/C sources
- Save and reload project
- Verify parameters persist correctly
- Test auto-discovery of .tbcc and .tbc.db
- Verify error messages clear and helpful

**Dependencies**: Phase 2-4  
**Estimated effort**: 2-3 days

---

### Phase 7: Documentation and Examples

**Goal**: User-facing documentation and example workflows

**Components**:

1. **User guide** (`docs-user/wiki-default/stages.md`)
   - Document "PAL YC" and "NTSC YC" stages
   - Explain composite vs YC
   - When to use each source type
   - File format requirements
   - Clarify that YC ≠ component video (Y/Pb/Pr)

2. **Technical notes** (this document)
   - Architecture overview
   - Implementation details
   - Performance characteristics

3. **Example projects**
   - `project-examples/yc-pal.orcprj`
   - `project-examples/yc-ntsc.orcprj`

4. **Test data** (if possible)
   - Sample `.tbcy` / `.tbcc` files
   - Or synthetic generator script

**Estimated effort**: 1-2 days

---

## Testing Strategy

### Unit Tests

1. **Interface tests**
   - VFR interface methods
   - Wrapper forwarding
   - Channel mode propagation

2. **Source tests**
   - Y/C file reading
   - Field count validation
   - Geometry validation
   - Metadata sharing

3. **Transform tests**
   - Field map with Y/C
   - Dropout correction on Y/C
   - Stacker with Y/C

### Integration Tests

1. **End-to-end pipeline**
   - Y/C source → field_map → dropout_correct → chroma_sink
   - Verify output quality
   - Compare composite vs component decode quality

2. **Mixed scenarios**
   - Multiple Y/C sources to stacker
   - Verify error when attempting to add composite source to YC project
   - Verify error when attempting to add YC source to composite project

### Quality Validation

1. **Visual inspection**
   - No comb artifacts on luma from Y/C sources
   - Chroma decoding correct
   - Dropouts corrected on both channels

2. **Metrics**
   - SNR measurements on Y and C
   - Dropout correction effectiveness
   - Decode time comparison (component should be faster)

3. **Regression tests**
   - Existing composite sources still work
   - No performance degradation on composite path

---

## Performance Considerations

### Memory

**YC sources use more cache memory**:
- Dropout correction: 840MB (2× channels) vs 420MB (composite)
- Stacker: 2× cached fields
- Preview: 3× cached previews (Y, C, composite views)

**Total estimated**: +400-500MB for YC sources vs composite

### CPU

**YC decode is FASTER**:
- Skips entire comb filter Y/C separation (expensive)
- Only demodulation needed (simpler DSP)
- Estimated 30-50% faster decode for YC sources

### I/O

**YC sources**:
- 2 file descriptors instead of 1
- 2× pread() calls per line
- Mitigated by: OS page cache, LRU caching

**Expected impact**: Minimal (<5% overhead)

---

## Backward Compatibility

### Project Files

**Existing projects** with composite sources (`LDPALSource`, `LDNTSCSource`):
- Continue to work unchanged
- No migration needed

**Stage registry**:
- Consider aliasing old names to new "Composite" names
- Or keep old names as-is for backward compat

### API

**New interface methods are additive**:
- Existing composite path unchanged
- Default implementations return false/nullptr
- Wrappers auto-forward

**No breaking changes** to existing code.

---

## Future Enhancements

### Phase 8+ (Post-Initial Release)

1. **Separate Y/C processing in more stages**
   - Noise reduction tuned per channel
   - Different filters for Y vs C

2. **Performance optimizations**
   - SIMD for demodulation
   - Multi-threaded Y and C processing
   - Prefetch both channels together

3. **Advanced previews**
   - U/V plane visualization
   - Chroma amplitude/phase plots
   - Diff view (Y/C alignment check)

---

## Success Criteria

### Minimum Viable Product (MVP)

- [ ] Y/C source stages load `.tbcy` and `.tbcc` files
- [ ] Basic transforms work (field_map, dropout_correct)
- [ ] Chroma decoder produces correct output
- [ ] Preview shows Y and C separately
- [ ] No regression on composite sources

### Full Release

- [ ] All transform stages support dual-channel
- [ ] Stacker works with Y/C sources
- [ ] Full UI integration (channel selector, previews)
- [ ] Documentation complete
- [ ] Example projects available
- [ ] Quality validation passed (no comb artifacts on Y)
- [ ] Performance acceptable (YC decode faster than composite)
- [ ] DAG validation prevents mixing composite and YC sources

---

## Risk Assessment

### High Risk

1. **Comb filter modification** (Phase 4)
   - Complex existing code
   - Risk of breaking composite decode
   - **Mitigation**: Thorough testing, keep composite path separate

2. **Dropout correction** (Phase 3.2)
   - Critical for quality
   - Dual-channel logic complex
   - **Mitigation**: Incremental implementation, synthetic test cases

### Medium Risk

1. **Stacker channel validation**
   - Edge cases in mixed sources
   - **Mitigation**: Clear error messages, early validation

2. **Preview performance**
   - 3× cache usage
   - **Mitigation**: Monitor memory, adjust cache sizes

### Low Risk

1. **Interface extensions** (Phase 1)
   - Additive, backward compatible

2. **Simple transforms** (Phase 3.4)
   - Straightforward dual-channel ops

---

## Timeline Estimate

**Assuming single developer, part-time work:**

| Phase | Effort | Calendar Time |
|-------|--------|---------------|
| Phase 1: Interface | 2-4 hours | 1 day |
| Phase 2: Sources | 1-2 days | 1 week |
| Phase 3: Transforms | 4-7 days | 2-3 weeks |
| Phase 4: Decoder | 3-5 days | 1-2 weeks |
| Phase 5: Preview | 3-4 days | 1-2 weeks |
| Phase 6: GUI | 2-3 days | 1 week |
| Phase 7: Docs | 1-2 days | 3-4 days |
| **Total** | **16-27 days** | **8-12 weeks** |

**Accelerated timeline** (full-time, focused): 3-4 weeks

---

## Conclusion

Supporting YC sources is a significant enhancement that provides:
- **Better quality** for color-under tape sources (VHS, Betamax, etc.)
- **Faster decode** (no comb filter on luma)
- **Cleaner previews** and analysis
- **Architectural flexibility** for future enhancements

The implementation is well-scoped with clear phases and minimal risk to existing functionality. The dual-channel architecture is clean and maintainable, with automatic propagation through the wrapper chain reducing the implementation burden.

**Important constraint**: Projects must be exclusively composite OR exclusively YC - no mixing allowed. This simplifies the implementation and avoids quality degradation from re-modulating separated channels.

**Recommendation**: Proceed with phased implementation, starting with Phase 1-2 to establish foundation, then Phase 3-4 for core functionality.
