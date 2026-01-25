# Analysis Presenters Refactoring Plan

**Status:** Phase 1 Complete  
**Version:** 1.1  
**Date:** 2026-01-25  
**Updated:** 2026-01-25

## Executive Summary

Currently, analysis tools are called via a generic `AnalysisPresenter::runGenericAnalysis()` method that tries to handle all cases. This creates architectural problems:

- Business logic is split between core tools and GUI
- Each analysis tool has different requirements (some need DAG context, some need field counts, etc.)
- Adding new tools requires modifying generic code
- Testing is complex because of tight coupling

**Solution:** Create specialized presenters for each analysis tool, with a common base class to handle patterns common to all analysis tools.

**Benefits:**
- Business logic stays cleanly in core (tools focus on their algorithm)
- Presenters handle tool-specific "glue" (data preparation, DAG execution, result formatting)
- GUI is purely UI (calls `fieldCorruptionPresenter->runAnalysis()`, displays results)
- New analysis tools follow a clear pattern
- Easy to test each presenter independently
- Cleaner MVP architecture

---

## Analysis Tools Inventory

Currently registered tools (as of 2026-01-25):

1. **FFmpeg Preset Tool** (`ffmpeg_preset`)
   - Purpose: Generate/validate FFmpeg encoding presets
   - Applicable to: Export stages
   - Requirements: Access to stage parameters

2. **Field Corruption Generator** (`field_corruption`)
   - Purpose: Generate field mapping corruption patterns for testing
   - Applicable to: `field_map` stage
   - Requirements: Execute input node to get field count, access DAG

3. **Disc Mapper Tool** (`disc_mapper`)
   - Purpose: Generate disc field mappings
   - Applicable to: `disc_mapper` stage  
   - Requirements: Access to video format, potential DAG context

4. **Source Alignment Tool** (`source_alignment`)
   - Purpose: Analyze and align source material
   - Applicable to: Source stages
   - Requirements: Access to input data, potentially cached renders

5. **Mask Line Tool** (`mask_line`)
   - Purpose: Generate or validate mask line specifications
   - Applicable to: `mask_line` stage
   - Requirements: Video format, frame dimensions

6. **Dropout Editor Tool** (`dropout_editor`)
   - Purpose: Edit/validate dropout specifications
   - Applicable to: `dropout_analysis` stage
   - Requirements: Execute input node, access field data

---

## Current Architecture Problems

### 1. Generic Presenter Issues
```cpp
// Current approach - one method tries to handle all cases
AnalysisPresenter::runGenericAnalysis(
    tool_id, node_id, source_type, parameters,
    additional_context,  // Hack for field_corruption
    progress_callback
)
```

Problems:
- `additional_context` is Field Corruption-specific but in generic interface
- No way to pass tool-specific requirements
- Presenter doesn't have access to necessary data (DAG, project)
- Hard to add new requirements without breaking existing code

### 2. Tool Logic Split
- Seed parameter retrieval: Tool queries DAG directly
- Field count: Should come from `additional_context` (GUI responsibility)
- Result application: Tool handles applyToGraph() but GUI calls it

### 3. GUI/Core Coupling
- GUI doesn't include core headers but still needs to pass project data
- Presenters have access to project but not to DAG (it's in project)
- Analysis tools in core can execute DAG but don't have it in context

---

## Proposed Architecture

### Base Class: `AnalysisToolPresenter`

```cpp
namespace orc::presenters {

/**
 * @brief Base class for all analysis tool presenters
 * 
 * Handles common concerns:
 * - Progress reporting
 * - Error handling
 * - Result display formatting
 * - Apply-to-graph coordination
 */
class AnalysisToolPresenter {
protected:
    explicit AnalysisToolPresenter(orc::Project* project);
    virtual ~AnalysisToolPresenter();
    
    // For subclasses to implement
    virtual std::string toolId() const = 0;
    virtual std::string toolName() const = 0;
    
    // Common utilities available to subclasses
    std::shared_ptr<orc::DAG> buildDAG() const;
    const std::vector<ProjectDAGNode>& getNodes() const;
    bool hasNodeInput(NodeID node_id) const;
    std::vector<ArtifactPtr> executeToNode(NodeID node_id) const;
    
    // Result handling
    void applyResultToGraph(const AnalysisResult& result, NodeID node_id);
    
    // Progress
    void reportProgress(int percentage, const std::string& status);
    
private:
    orc::Project* project_;  // Not owned
    std::shared_ptr<orc::DAG> cached_dag_;
};

} // namespace orc::presenters
```

### Specialized Presenters: Example `FieldCorruptionPresenter`

```cpp
namespace orc::presenters {

/**
 * @brief Presenter for Field Corruption Generator analysis tool
 * 
 * Handles:
 * - Preparing DAG context for the tool
 * - Executing input node to get field count
 * - Formatting results for display
 * - Applying generated pattern to field_map stage
 */
class FieldCorruptionPresenter : public AnalysisToolPresenter {
public:
    explicit FieldCorruptionPresenter(orc::Project* project);
    
    /**
     * @brief Run field corruption analysis
     * @param node_id The field_map node to analyze
     * @param parameters User-selected parameters (pattern type, etc.)
     * @param progress_callback Optional progress updates
     * @return Analysis result with generated pattern
     */
    orc::public_api::AnalysisResult runAnalysis(
        NodeID node_id,
        const std::map<std::string, orc::ParameterValue>& parameters,
        std::function<void(int, const std::string&)> progress_callback = nullptr
    );
    
protected:
    std::string toolId() const override;
    std::string toolName() const override;
};

} // namespace orc::presenters
```

### GUI Usage

```cpp
// Current (generic and confusing)
auto result = analysis_presenter_->runGenericAnalysis(
    "field_corruption", node_id, source_type, params, 
    additional_context, callback);

// Proposed (clear and type-safe)
auto fc_presenter = std::make_unique<FieldCorruptionPresenter>(project);
auto result = fc_presenter->runAnalysis(node_id, params, callback);
```

---

## Implementation Phases

### Phase 1: Base Class and Infrastructure (2 days)

**Goals:**
- Create `AnalysisToolPresenter` base class with common utilities
- Establish presenter pattern for analysis tools
- Set up testing infrastructure

**Tasks:**
1. Create `/orc/presenters/include/analysis_tool_presenter.h`
   - Base class with protected utilities
   - Common progress/error handling
   - DAG building, node execution helpers

2. Create `/orc/presenters/src/analysis_tool_presenter.cpp`
   - Implement DAG building from project
   - Implement node execution
   - Implement progress/error reporting

3. Update `/orc/presenters/CMakeLists.txt`
   - Include new base presenter

4. Create unit tests:
   - Test DAG building from project
   - Test node input detection
   - Test artifact extraction

**Deliverables:**
- ✅ Base `AnalysisToolPresenter` class - COMPLETED 2026-01-25
- ✅ Common utilities for all presenters - COMPLETED 2026-01-25
- ⚠️ Unit tests passing - DEFERRED (no test framework set up yet)
- ✅ Architecture documentation - COMPLETED 2026-01-25

**Implementation Notes (2026-01-25):**
- Created `/orc/presenters/include/analysis_tool_presenter.h` with:
  - Protected constructor taking `Project*` and `ErrorReporter*`
  - Virtual interface: `toolId()`, `toolName()`
  - Common utilities: `getOrBuildDAG()`, `hasNodeInput()`, `getFirstInputNodeId()`, `executeToNode()`, `applyResultToGraph()`
  - DAG caching with `invalidateDAGCache()`
  
- Created `/orc/presenters/src/analysis_tool_presenter.cpp` with:
  - DAG building via `project_to_dag()` utility (reuses existing core infrastructure)
  - Node execution via `DAGExecutor`
  - Result application to graph
  - Progress reporting framework
  
- Updated `/orc/presenters/CMakeLists.txt` to include new presenter source

- Fixed issues during implementation:
  - Used existing `project_to_dag()` utility instead of manually building DAG
  - Handled const correctness for artifact vectors
  - Simplified result conversion to use only `graphData` field
  - Fixed `GenericAnalysisDialog` constructor call in mainwindow.cpp

**Build Status:** ✅ All code compiles successfully (orc-gui, orc-cli, orc-presenters, orc-core)

---

### Phase 2: Field Corruption Presenter (2 days)

**Goals:**
- Refactor Field Corruption tool to use specialized presenter
- Establish pattern for other tools to follow
- Verify architecture works end-to-end

**Tasks:**
1. Create `FieldCorruptionPresenter`
   - Implement `runAnalysis(node_id, parameters, callback)`
   - Build DAG context and pass to tool
   - Execute input node and extract field count
   - Format results

2. Update core `FieldCorruptionAnalysisTool`
   - Tool now receives fully-prepared context
   - Can focus on algorithm (generating pattern)
   - Calls `applyToGraph()` handled by presenter

3. Update GUI `GenericAnalysisDialog`
   - Create `FieldCorruptionPresenter` instance
   - Call `presenter->runAnalysis()` directly
   - No longer passes `additional_context` hacks

4. Update analysis registry (if needed)
   - Verify tool still registers and is found
   - Ensure backward compatibility

5. Integration testing
   - GUI → Presenter → Core Tool → Result → GUI
   - Verify Apply button works correctly

**Deliverables:**
- ✓ `FieldCorruptionPresenter` fully functional
- ✓ Field Corruption analysis works end-to-end
- ✓ Generic dialog refactored for this tool
- ✓ Integration tests passing

**Note:** Field Corruption tool execution already properly extracts field count from input node - this phase mainly moves that responsibility from tool to presenter where it belongs.

---

### Phase 3: Remaining Presenters (3-4 days)

Repeat Phase 2 pattern for each tool:

3.1. **Disc Mapper Presenter** (1 day)
- Determine what data it needs (likely: video format, project metadata)
- Implement presenter following Field Corruption pattern
- Refactor tool and GUI

3.2. **Source Alignment Presenter** (1 day) ✅ **COMPLETED 2026-01-25**
- ✅ Created `/orc/presenters/include/source_alignment_presenter.h`
- ✅ Created `/orc/presenters/src/source_alignment_presenter.cpp`
- ✅ Updated `/orc/presenters/CMakeLists.txt`
- ✅ Updated `GenericAnalysisDialog` to use `SourceAlignmentPresenter`
- ✅ Builds successfully - all targets compile
- ✅ Fully integrated with GUI

**Implementation Notes:**
- Presenter provides DAG context to the core tool
- Tool executes input nodes to get all source artifacts for VBI analysis
- Progress adapter translates core AnalysisProgress to GUI callback
- Result conversion handles Success/Failed/Cancelled status mapping
- Note: ctx.project not set due to shared_ptr requirement; tool uses DAG only
- GUI integration: Dialog instantiates SourceAlignmentPresenter when tool_id is "source_alignment"
- Uses same pattern as FieldCorruptionPresenter and DiscMapperPresenter

3.3. **Mask Line Presenter** (1 day)
- Needs: video format, frame dimensions
- Implement presenter
- Integration test

3.4. **FFmpeg Preset Presenter** (0.5 days)
- Simpler tool - just needs stage parameters
- Implement presenter
- Integration test

3.5. **Dropout Editor Presenter** (0.5 days)
- Needs: input node execution, field data
- Implement presenter
- Integration test

**Deliverables:**
- ✓ All six analysis tool presenters implemented
- ✓ Each tool working through its specialized presenter
- ✓ GUI updated to use specialized presenters instead of generic one
- ✓ All integration tests passing

---

### Phase 4: Cleanup and GUI Refactoring (1-2 days)

**Goals:**
- Remove generic `runGenericAnalysis()` method
- Update all analysis dialogs to use specialized presenters
- Clean up temporary code

**Tasks:**
1. Create specialized analysis dialogs (if needed)
   - Instead of `GenericAnalysisDialog`, have tool-specific dialogs
   - Or keep generic dialog but have it instantiate right presenter

2. Update `MainWindow`
   - Replace generic presenter with specialized ones
   - Each analysis action creates appropriate presenter

3. Remove old code
   - Delete generic `runGenericAnalysis()`
   - Remove `additional_context` from `AnalysisContext`
   - Clean up temporary hacks

4. Documentation
   - Update architecture docs
   - Document how to add new analysis tools
   - Update contributor guide

**Deliverables:**
- ✓ No more generic `runGenericAnalysis()`
- ✓ All analysis tools use specialized presenters
- ✓ Code is cleaner and more maintainable
- ✓ Documentation updated

---

## How to Add a New Analysis Tool

After this refactoring, adding a new analysis tool follows a clear pattern:

1. **Implement core tool** in `/orc/core/analysis/your_tool/`
   - Extend `AnalysisTool` base class
   - Implement `analyze()` with your algorithm
   - Optionally implement `applyToGraph()`

2. **Create presenter** in `/orc/presenters/src/your_tool_presenter.cpp`
   - Extend `AnalysisToolPresenter`
   - Implement `runAnalysis()` with tool-specific logic
   - Use base class utilities for DAG/node access

3. **Update GUI** to instantiate and use presenter
   - Create presenter with project pointer
   - Call `presenter->runAnalysis()`
   - Display results

4. **Register tool** in `analysis_init.cpp`
   - Add force-link function

Done! No changes to generic code needed.

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Refactoring breaks existing tools | High | Phase-by-phase approach, comprehensive tests at each phase |
| GUI needs multiple presenter types | Medium | Use polymorphic container or factory |
| DAG building is expensive | Medium | Cache DAG in presenter |
| Tools have conflicting requirements | Low | Base class provides common patterns |
| Adding new tools is still complex | Medium | Document pattern clearly with examples |

---

## Timeline

- **Phase 1:** 2 days → Jan 29
- **Phase 2:** 2 days → Jan 31  
- **Phase 3:** 3-4 days → Feb 4
- **Phase 4:** 1-2 days → Feb 5

**Total:** 8-9 days  
**Target completion:** Early February 2026

---

## Alternatives Considered

### 1. Keep Generic Presenter
**Pros:** Less refactoring work  
**Cons:** Architecture remains messy, hard to add tools, unclear responsibilities

### 2. One Presenter Per UI (not per tool)
**Pros:** Smaller refactoring  
**Cons:** Still mixing concerns, doesn't solve the "what data does the tool need" problem

### 3. Analysis Tools in GUI (not core)
**Pros:** No presenter needed  
**Cons:** Core can't be used without GUI, violates architecture

**Decision:** Specialized presenters with base class (proposed solution) is best balance of clarity, maintainability, and architectural cleanliness.

---

## Questions/Discussion

1. Should analysis dialogs be tool-specific or keep generic dialog?
2. Should presenters own the UI widgets or just provide data?
3. How should we handle tools that need real-time preview (e.g., vectorscope)?
4. Should analysis results be cacheable/stored in project?
5. Do we need transaction semantics for "apply result to graph"?

