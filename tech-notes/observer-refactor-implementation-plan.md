# Implementation Plan: Observer Refactor to Observation Context Architecture

This document provides a comprehensive implementation plan for refactoring the observer system based on the architecture described in [observer-refactor.md](observer-refactor.md).

## Overview

The refactor transforms observers from returning observation vectors to populating a shared `ObservationContext` that flows alongside the VFR through the pipeline. This eliminates the false dichotomy of "observers as inputs vs outputs" and enables explicit stage contracts for observation dependencies.

**Implementation Strategy**: This is a **clean break refactor** with no backward compatibility. All existing observation-related code will be removed and replaced with the new architecture. This allows for a cleaner implementation and faster development timeline.

---

## Phase 1: Core Infrastructure & Contracts

### 1.1 Create ObservationContext Class

**Files**: `orc/core/include/observation_context.h` and `.cpp`

**Purpose**: Central storage for all observations flowing through pipeline

**Key Features**:
- Namespaced key-value storage: `(namespace, key) → ObservationValue`
- Support for typed values (int, double, string, bool, custom types)
- Thread-safe if needed for parallel processing
- Query methods: `get()`, `has()`, `set()`
- Namespace management to avoid collisions

**Dependencies**: None (foundational)

**Example API**:
```cpp
class ObservationContext {
public:
    void set(const std::string& namespace_, 
             const std::string& key, 
             const ObservationValue& value);
    
    std::optional<ObservationValue> get(const std::string& namespace_, 
                                        const std::string& key) const;
    
    bool has(const std::string& namespace_, 
             const std::string& key) const;
    
    void clear();
    
    std::vector<std::string> get_namespaces() const;
    std::vector<std::string> get_keys(const std::string& namespace_) const;
};
```

### 1.2 Define Observation Schema Interface

**File**: `orc/core/include/observation_schema.h`

**Purpose**: Define what observations an observer can provide

**Key Features**:
- Observer declares provided observation keys
- Type information for each observation
- Documentation/description per observation
- Validation helpers

**Example**:
```cpp
enum class ObservationType {
    INT32,
    DOUBLE,
    STRING,
    BOOL,
    TIMECODE,
    CUSTOM
};

struct ObservationKey {
    std::string namespace_;
    std::string name;
    ObservationType type;
    std::string description;
    bool optional;  // May not be present for every field
};
```

### 1.3 Refactor Observer Base Class

**File**: `orc/core/observers/observer.h`

**Changes**:
- Replace return of `std::vector<std::shared_ptr<Observation>>` with void
- Add `ObservationContext& context` parameter to `process_field()`
- Add `std::vector<ObservationKey> get_provided_observations() const`
- Add configuration schema method: `get_configuration_schema()`

**New Signature**:
```cpp
class Observer {
public:
    virtual ~Observer() = default;
    
    virtual std::string observer_name() const = 0;
    virtual std::string observer_version() const = 0;
    
    // New signature - writes to context instead of returning
    virtual void process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        ObservationContext& context) = 0;
    
    // Declare what observations this observer provides
    virtual std::vector<ObservationKey> get_provided_observations() const = 0;
    
    // Declare configuration schema
    virtual std::vector<ParameterDescriptor> get_configuration_schema() const {
        return {}; // Default: no configuration
    }
    
    // Set configuration (validated against schema)
    virtual void set_configuration(const std::map<std::string, ParameterValue>& config) {
        // Default: no configuration
    }
};
```

### 1.4 Create Observer Configuration Schema

**File**: `orc/core/include/observer_config.h`

**Purpose**: Define and validate observer configuration

**Key Features**:
- Parameter descriptors (name, type, required/optional, defaults, ranges)
- Schema validation
- JSON/map parsing
- Similar to existing `ParameterDescriptor` pattern in stages

**Example**:
```cpp
class ObserverConfiguration {
public:
    static bool validate(
        const std::vector<ParameterDescriptor>& schema,
        const std::map<std::string, ParameterValue>& config,
        std::string& error_message);
    
    static std::map<std::string, ParameterValue> apply_defaults(
        const std::vector<ParameterDescriptor>& schema);
};
```

### 1.5 Update All Existing Observers

**Files**: All observer implementations in `orc/core/observers/`

**Changes for each observer**:
1. Implement `get_provided_observations()` listing all observations written
2. Implement `get_configuration_schema()` declaring parameters
3. Update `process_field()` to write to `ObservationContext` instead of returning
4. Convert `set_parameters()` to validate against schema
5. Add instance identification for multi-instance support

**Priority Order**:
1. **Simple observers first** (proof of concept):
   - `burst_level_observer.cpp` - Minimal configuration
   - `white_flag_observer.cpp` - Simple detection
   
2. **Medium complexity**:
   - `fm_code_observer.cpp` - Basic VBI decoding
   - `biphase_observer.cpp` - Multiple VBI data types
   - `closed_caption_observer.cpp` - Stateful decoding
   
3. **Specialized/Advanced**:
   - `snr_analysis_observer.cpp` - Quality metrics
   - `vits_observer.cpp` - Test signal analysis

**Observers to Remove**:
- `pulldown_observer.cpp` - **Not actually an observer**, should be removed from observer system

**Future Observers** (not yet implemented):
- `vitc_observer.cpp` - Vertical Interval Timecode (to be implemented)
- `video_id_observer.cpp` - Video ID decoding (to be implemented)

**Example Migration (BurstLevelObserver)**:
```cpp
// Before:
std::vector<std::shared_ptr<Observation>> BurstLevelObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    auto obs = std::make_shared<BurstLevelObservation>();
    obs->field_id = field_id;
    obs->burst_level = measured_level;
    
    return {obs};
}

// After:
void BurstLevelObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    ObservationContext& context) {
    
    double measured_level = measure_burst(representation, field_id);
    
    context.set("burst", "level", measured_level);
    context.set("burst", "confidence", confidence_level);
    context.set("burst", "field_id", field_id.to_sequential());
}

std::vector<ObservationKey> BurstLevelObserver::get_provided_observations() const {
    return {
        {"burst", "level", ObservationType::DOUBLE, "Burst amplitude level"},
        {"burst", "confidence", ObservationType::STRING, "Detection confidence"},
        {"burst", "field_id", ObservationType::INT32, "Field sequence number"}
    };
}
```

### 1.6 Define Stage Observation Requirements Interface

**File**: `orc/core/stages/stage.h`

**Changes**: Add optional methods to `DAGStage` base class

```cpp
class DAGStage {
public:
    // ... existing methods ...
    
    /**
     * @brief Declare observations required for this stage to operate
     * 
     * Pipeline validation will ensure these observations are available
     * before execution begins.
     * 
     * @return List of required observation keys
     */
    virtual std::vector<ObservationKey> get_required_observations() const {
        return {}; // Most stages don't require observations
    }
    
    /**
     * @brief Declare observations provided by this stage
     * 
     * Stages may own observers and populate the context. This allows
     * pipeline validation to know what observations will be available.
     * 
     * @return List of provided observation keys
     */
    virtual std::vector<ObservationKey> get_provided_observations() const {
        return {}; // Most stages don't provide observations
    }
};
```

### 1.7 Implement Pipeline Validation

**File**: `orc/core/pipeline_validator.h` (new)

**Purpose**: Validate observation dependencies before execution

**Features**:
- Collect all required observations from all stages
- Collect all provided observations from all stages
- Fail early if requirements can't be met
- Generate helpful error messages showing missing observations

**Example**:
```cpp
class PipelineValidator {
public:
    struct ValidationResult {
        bool valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    
    static ValidationResult validate_observation_dependencies(
        const std::vector<DAGStagePtr>& stages);
    
private:
    static std::set<ObservationKey> collect_required_observations(
        const std::vector<DAGStagePtr>& stages);
    
    static std::set<ObservationKey> collect_provided_observations(
        const std::vector<DAGStagePtr>& stages);
};
```

**Validation Logic**:
1. Walk through stages in execution order
2. Track accumulated provided observations
3. For each stage, check if required observations are available
4. Report missing observations with stage names and observation keys

---

## Phase 2: Stage Integration & Configuration

### 2.1 Update DAGStage Execute Signature

**File**: `orc/core/stages/stage.h`

**Change**: Add `ObservationContext&` parameter to execute method

```cpp
class DAGStage {
public:
    /**
     * @brief Execute stage transformation
     * 
     * @param inputs Input artifacts
     * @param parameters Configuration parameters
     * @param observation_context Shared observation context for pipeline
     * @return Output artifacts
     */
    virtual std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) = 0;
};
```

**Impact**: All stage implementations must be updated (breaking change)

### 2.2 Update Source Stages

**Files**: 
- `orc/core/stages/pal_yc_source/pal_yc_source_stage.cpp`
- `orc/core/stages/ntsc_composite_source/ntsc_composite_source_stage.cpp`
- Similar source stages

**Changes**:
1. Accept `ObservationContext&` in execute
2. Instantiate and configure source-specific observers (if any)
3. Run observers during field processing
4. Pass context through

**Example**:
```cpp
std::vector<ArtifactPtr> PALYCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    
    // Create source-specific observers (if configured)
    // Most observers will be instantiated by sink stages
    
    // Process fields
    for (FieldID field_id : field_range) {
        // ... field processing ...
        // Observers typically run at sink, not source
    }
    
    return {vfr};
}
```

### 2.3 Update Transform Stages

**Files**: 
- `orc/core/stages/video_params/video_params_stage.cpp`
- Dropout correction stages
- Other transform stages

**Changes**:
1. Accept `ObservationContext&` in execute
2. Declare required observations if needed (via `get_required_observations()`)
3. Pass context through unchanged (or add observations if needed)

**Example** (transform that requires observations):
```cpp
// Hypothetical example - most transforms won't require observations
class SignalQualityFilterStage : public DAGStage {
public:
    std::vector<ObservationKey> get_required_observations() const override {
        return {
            {"burst", "level", ObservationType::DOUBLE, "Burst amplitude level"},
            {"snr", "value", ObservationType::DOUBLE, "Signal-to-noise ratio"}
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override {
        
        // Read required observations
        auto burst_level = observation_context.get("burst", "level");
        if (!burst_level) {
            throw DAGExecutionError("Required observation 'burst.level' not available");
        }
        
        // Use observation data for processing decisions
        // ...
        
        return {output};
    }
};
```

### 2.4 Update Sink Stages

**Files**: 
- `orc/core/stages/ld_sink/ld_sink_stage.cpp`
- `orc/core/stages/chroma_sink/chroma_sink_stage.cpp`
- Other sink stages

**Changes**:
1. Accept `ObservationContext&` in execute
2. Instantiate configured observers for metadata extraction
3. Run observers and populate context
4. Add configuration for which observations to emit
5. Read from context and write to output metadata

**Example**:
```cpp
std::vector<ArtifactPtr> LdSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    
    // Instantiate observers based on configuration
    std::vector<std::unique_ptr<Observer>> observers;
    if (enable_biphase_) {
        observers.push_back(std::make_unique<BiphaseObserver>());
    }
    if (enable_closed_caption_) {
        observers.push_back(std::make_unique<ClosedCaptionObserver>());
    }
    if (enable_burst_level_) {
        observers.push_back(std::make_unique<BurstLevelObserver>());
    }
    // Note: VITC and VideoID observers not yet implemented
    
    // Process fields
    for (FieldID field_id : field_range) {
        // Run all observers
        for (auto& observer : observers) {
            observer->process_field(*vfr, field_id, observation_context);
        }
    }
    
    // Write metadata
    write_metadata(observation_context, output_observations_);
    
    return {};
}
```

### 2.5 Add Observation Output Configuration to Sinks

**Files**: All sink stage implementations

**New Parameters**:
```cpp
std::vector<ParameterDescriptor> LdSinkStage::get_parameter_descriptors(...) const {
    return {
        // ... existing parameters ...
        
        {
            "output_observations",
            "Observations to Output",
            "List of observations to include in metadata output",
            ParameterType::StringList,
            std::vector<std::string>{"biphase.picture_number", "burst.level"}
        },
        {
            "observation_format",
            "Observation Format",
            "Format for observation output",
            ParameterType::String,
            std::string("json"),
            {"json", "tbc-metadata", "csv"}
        }
    };
}
```

**Configuration Example** (JSON project file):
```json
{
  "stage": "ld_sink",
  "parameters": {
    "output_path": "/path/to/output.tbc",
    "observers": {
      "biphase": {
        "enabled": true
      },
      "closed_caption": {
        "enabled": true
      },
      "burst_level": {
        "enabled": true
      }
    },
    "output_observations": [
      "biphase.picture_number",
      "biphase.chapter",
      "biphase.side",
      "closed_caption.text",
      "burst.level"
    ],
    "observation_format": "json"
  }
}
```

**Note**: VITC and VideoID observers will be added in the future.

### 2.6 Implement Observation Serialization

**File**: `orc/core/observation_serializer.h` (new)

**Purpose**: Convert ObservationContext to output formats

**Formats**:
- **JSON**: Standard key-value with namespaces
- **TBC metadata**: Compatible with existing ld-decode JSON format
- **CSV/TSV**: For batch analysis and plotting

**Example**:
```cpp
class ObservationSerializer {
public:
    static std::string to_json(
        const ObservationContext& context,
        const std::vector<std::string>& selected_keys = {});
    
    static std::string to_tbc_metadata(
        const ObservationContext& context);
    
    static std::string to_csv(
        const ObservationContext& context,
        const std::vector<std::string>& selected_keys = {});
};
```

**JSON Output Example**:
```json
{
  "fields": [
    {
      "field_id": 0,
      "observations": {
        "biphase": {
          "picture_number": 12345,
          "chapter": 5,
          "side": 1
        },
        "closed_caption": {
          "text": "Hello world",
          "confidence": "high"
        },
        "burst": {
          "level": 0.85
        }
      }
    }
  ]
}
```

---

## Phase 3: Pipeline Execution & UI Integration

### 3.1 Update DAG Execution Engine

**File**: `orc/core/dag_executor.cpp` or equivalent

**Changes**:
1. Create `ObservationContext` at pipeline start
2. Pass context to all stage execute calls
3. Maintain context across entire pipeline execution

**Example**:
```cpp
class DAGExecutor {
public:
    std::vector<ArtifactPtr> execute_pipeline(
        const std::vector<DAGStagePtr>& stages,
        const std::vector<ArtifactPtr>& initial_inputs) {
        
        // Create observation context for this pipeline execution
        ObservationContext observation_context;
        
        // Validate observation dependencies
        auto validation = PipelineValidator::validate_observation_dependencies(stages);
        if (!validation.valid) {
            throw DAGExecutionError("Observation dependencies not satisfied: " + 
                                    validation.errors[0]);
        }
        
        // Execute stages
        std::vector<ArtifactPtr> current_outputs = initial_inputs;
        for (const auto& stage : stages) {
            current_outputs = stage->execute(
                current_outputs,
                stage->get_parameters(),
                observation_context  // Pass context through
            );
        }
        
        return current_outputs;
    }
};
```

### 3.2 Remove ObservationHistory Flow Through VFR

**File**: `orc/core/include/video_field_representation.h`

**Current**: `VideoFieldRepresentation::get_observations()` returns per-field observations

**Change**: Directly remove this method and all related observation storage in VFR - observations now live exclusively in `ObservationContext`

**Deletions**:
1. Remove `get_observations(FieldID)` method from VFR interface
2. Remove any observation storage in concrete VFR implementations
3. Remove `ObservationHistory` class (replaced by ObservationContext)
4. Update all callers to use `ObservationContext` instead

**Impact**: Breaking change - requires updating all code that queries observations

**Migration**:
```cpp
// Before:
auto observations = vfr->get_observations(field_id);
for (const auto& obs : observations) {
    if (obs->observation_type() == "VITC") {
        // Use observation
    }
}

// After:
auto timecode = observation_context.get("vitc", "timecode");
if (timecode) {
    // Use observation
}
```

### 3.3 Update DAGFieldRenderer

**File**: `orc/core/dag_field_renderer.cpp`

**Current**: Has `attach_observations()` method that creates observers

**Changes**:
1. Remove `attach_observations()` method
2. Observers now called by stages, not renderer
3. Pass observation context through field rendering

**Before**:
```cpp
std::shared_ptr<VideoFieldRepresentation> DAGFieldRenderer::attach_observations(
    std::shared_ptr<VideoFieldRepresentation> representation,
    FieldID field_id) {
    
    std::vector<std::unique_ptr<Observer>> observers;
    observers.push_back(std::make_unique<BiphaseObserver>());
    // ... create all observers ...
    
    // Run observers
    // ...
}
```

**After**: Remove this entirely - stages own their observers

### 3.4 Update GUI/CLI to Use New System

**Files**: 
- `orc/gui/qualitymetricsdialog.cpp`
- Analysis dialogs
- Metadata viewers
- CLI output formatters

**Changes**:
1. Query `ObservationContext` instead of VFR observations
2. Update displays to show namespace.key format
3. Add configuration UI for observation output selection

**Example GUI Update**:
```cpp
// Before:
void QualityMetricsDialog::update_from_vfr(
    std::shared_ptr<const VideoFieldRepresentation> vfr,
    FieldID field_id) {
    
    auto observations = vfr->get_observations(field_id);
    // Display observations
}

// After:
void QualityMetricsDialog::update_from_context(
    const ObservationContext& context,
    FieldID field_id) {
    
    auto vitc_timecode = context.get("vitc", "timecode");
    auto burst_level = context.get("burst", "level");
    // Display observations
}
```

---

## Phase 4: Complete Core Implementation

### Status: ⏳ IN PROGRESS (as of Jan 20, 2026)

This phase addresses missing functionality from Phases 1-3 that were structurally completed but functionally incomplete. Analysis decoders and VBI extraction were restored with proper method signatures but return no data. GUI components have placeholders. Critical data flow mechanisms are missing.

### 4.1 ObservationContext Core Data Flow

**Status**: ⏳ NOT STARTED

**Objective**: Implement the actual mechanism for observations to populate the context during stage execution.

**Deliverables**:
1. **ObservationContext Population Interface**
   - Methods for stages to write observation data
   - Proper type safety with std::variant
   - Support for namespaced key-value storage with type preservation
   - Observation schema validation on write

2. **Stage Execution Integration**
   - Modify DAGExecutor to pass context to all stages
   - Ensure observers populate context during execute()
   - Add error handling for missing required observations
   - Implement observation dependency validation before execution

3. **Test Coverage**
   - Unit tests for context set/get operations
   - Test namespace isolation
   - Test type safety and variant handling
   - Test missing observation detection

**Files to Modify/Create**:
- `orc/core/include/observation_context.h` - Complete implementation with population API
- `orc/core/observation_context.cpp` - Full implementation with type checking
- `orc/core/include/dag_executor.h` - Update signature to pass context
- `orc/core/dag_executor.cpp` - Update execution to populate context
- `orc/core/tests/observation_context_test.cpp` - Add data flow tests

### 4.2 Analysis Decoder Implementation

**Status**: ⏳ NOT STARTED (Structural restore complete, functional implementation needed)

**Objective**: Implement actual data extraction in DropoutAnalysisDecoder, SNRAnalysisDecoder, and BurstLevelAnalysisDecoder.

**Deliverables**:
1. **DropoutAnalysisDecoder**
   - Extract actual dropout information from field data
   - Populate `dropout.count`, `dropout.percentage` observations
   - Handle edge cases (corrupt fields, missing data)
   - Implement proper error logging

2. **SNRAnalysisDecoder**
   - Extract signal-to-noise ratio from biphase data
   - Calculate SNR per field and overall
   - Populate `snr.value`, `snr.per_field` observations
   - Handle silent fields and noise floor detection

3. **BurstLevelAnalysisDecoder**
   - Extract color burst amplitude and measurements
   - Populate `burst.level`, `burst.frequency` observations
   - Validate burst measurements
   - Handle missing or invalid bursts

**Implementation Details**:
```cpp
// Example for DropoutAnalysisDecoder
std::vector<std::string> DropoutAnalysisDecoder::get_analysis_keys() const {
    return {
        "dropout.count",
        "dropout.percentage",
        "dropout.lines_affected"
    };
}

void DropoutAnalysisDecoder::analyze_field(
    const std::shared_ptr<VideoFieldRepresentation>& field,
    ObservationContext& context) {
    
    // Extract actual dropout data from field
    uint32_t dropout_count = count_dropouts_in_field(field);
    double percentage = calculate_dropout_percentage(field, dropout_count);
    
    context.set("dropout", "count", static_cast<uint64_t>(dropout_count));
    context.set("dropout", "percentage", percentage);
    
    // More analysis...
}
```

**Files to Modify**:
- `orc/core/observers/dropout_analysis_decoder.h/cpp` - Implement analyze_field()
- `orc/core/observers/snr_analysis_decoder.h/cpp` - Implement analyze_field()
- `orc/core/observers/burst_level_analysis_decoder.h/cpp` - Implement analyze_field()
- `orc/core/include/video_field_representation.h` - Add methods to access raw field data
- Unit tests for each decoder

### 4.3 VBI Data Extraction

**Status**: ⏳ NOT STARTED (Structural restore complete, data extraction needed)

**Objective**: Implement actual VBI observation extraction from VideoFieldRepresentation.

**Deliverables**:
1. **VBIDecoder::decode_vbi() Implementation**
   - Extract actual VBI data from field representation
   - Populate observations for:
     - VITC (vertical interval timecode)
     - Closed captions
     - WSS (widescreen signaling)
     - Other VBI lines
   - Handle corrupted or missing VBI data

2. **VBI Observation Schema**
   - `vitc.timecode` - Timecode string (HH:MM:SS:FF)
   - `vitc.frame_number` - Frame number from VITC
   - `cc.data` - Closed caption data
   - `wss.data` - Widescreen signaling data

**Files to Modify**:
- `orc/core/observers/vbi_decoder.h/cpp` - Implement decode_vbi() with real extraction
- `orc/core/render_coordinator.cpp` - Verify context is properly passed and populated
- Unit tests for VBI extraction

### 4.4 GUI Observation Display Implementation

**Status**: ⏳ NOT STARTED (Placeholder restore complete, actual display needed)

**Objective**: Implement GUI dialogs to display extracted observation data.

**Deliverables**:
1. **QualityMetricsDialog**
   - Display dropout, SNR, burst level observations
   - Show analysis decoder results
   - Visualize metrics over time (graphs)
   - Replace placeholder comment with actual data extraction

2. **RenderCoordinator Integration**
   - Create properly populated ObservationContext before passing to decoders
   - Verify observations are accessible from GUI components
   - Test data flow from render pipeline to UI

**Implementation Details**:
```cpp
// Example for PulldownDialog
void PulldownDialog::updatePulldownObservation(
    const ObservationContext& context) {
    
    // Extract actual pulldown data
    auto pulldown_info = context.get("pulldown", "pattern");
    auto motion_adaptive = context.get("pulldown", "motion_adaptive");
    
    if (pulldown_info.has_value()) {
        // Update UI with real data instead of placeholder
        std::string pattern = std::get<std::string>(*pulldown_info);
        ui->pulldownLabel->setText(QString::fromStdString(pattern));
    }
    
    if (motion_adaptive.has_value()) {
        bool is_adaptive = std::get<bool>(*motion_adaptive);
        ui->motionAdaptiveCheckbox->setChecked(is_adaptive);
    }
}
```

**Files to Modify**:
- `orc/gui/dialogs/pulldowndialog.h/cpp` - Implement real updatePulldownObservation()
- `orc/gui/dialogs/qualitymetricsdialog.h/cpp` - Implement real observation extraction
- `orc/gui/render_coordinator.cpp` - Ensure ObservationContext properly populated
- Integration tests for GUI observation display

### 4.5 Closed Caption Processing Restoration

**Status**: ⏳ NOT STARTED (Stubbed in ffmpeg_output_backend)

**Objective**: Re-enable closed caption extraction and output.

**Deliverables**:
1. Implement `ffmpeg_output_backend.cpp` closed caption export functions
2. Extract CC data from VBI observations
3. Write CC data to output files in proper format
4. Add configuration for CC output control

**Files to Modify**:
- `orc/core/observers/ffmpeg_output_backend.cpp` - Implement CC export

### 4.6 Field Mapping Restoration

**Status**: ⏳ NOT STARTED

**Objective**: Restore field mapping functionality for field management.

**Deliverables**:
1. Restore field mapping implementation
2. Integrate with observation system
3. Test field association and tracking

**Files to Modify**:
- `orc/core/stages/field_mapping_stage.h/cpp` - Restore implementation
- Update with ObservationContext integration

### 4.7 Source Align VBI Frame Number Extraction

**Status**: ⏳ NOT STARTED (get_frame_number_from_vbi() stubbed)

**Objective**: Implement frame number extraction from VBI data for source alignment.

**Deliverables**:
1. Extract frame number from VITC VBI data
2. Populate observation for frame alignment
3. Handle missing or corrupted frame numbers
4. Validate frame number continuity

**Files to Modify**:
- `orc/core/stages/source_align_stage.cpp` - Implement get_frame_number_from_vbi()
- Integrate with VBI observation system

### 4.8 TBC Metadata Writer Implementation

**Status**: ⏳ NOT STARTED (Stubbed, always returns true without writing)

**Objective**: Implement actual metadata writing to TBC format.

**Deliverables**:
1. Extract observation data from context
2. Format according to TBC metadata specification
3. Write to metadata file or embedded in video
4. Handle multiple observation types

**Files to Modify**:
- `orc/core/observers/tbc_metadata_writer.cpp` - Implement write_observations()

### 4.9 Analysis Tool Instantiation

**Status**: ⏳ NOT STARTED (Still disabled in analysis_init.cpp)

**Objective**: Re-enable and test analysis tool creation.

**Deliverables**:
1. Uncomment analysis decoder instantiation in analysis_init.cpp
2. Verify all analysis tools properly initialized with ObservationContext
3. Add configuration control for which analysis tools are active
4. Test with full pipeline

**Files to Modify**:
- `orc/core/stages/analysis_init.cpp` - Re-enable analysis tool instantiation
- Add analysis tool configuration options

### 4.10 Observer Configuration Schema System

**Status**: ⏳ NOT STARTED

**Objective**: Implement complete observer configuration and schema validation.

**Deliverables**:
1. Observer registration system with configuration validation
2. Schema definition for each observer type
3. Configuration application at pipeline build time
4. Error messages for invalid configurations

### 4.11 Phase 4 Acceptance Criteria

- ✅ All analysis decoders return real data, not empty vectors
- ✅ VBI extraction populates observations properly
- ✅ GUI dialogs display real observation data
- ✅ Closed caption processing functional
- ✅ Field mapping restored and tested
- ✅ TBC metadata writer functional
- ✅ Source align VBI frame extraction working
- ✅ Analysis tools instantiating properly
- ✅ No placeholder implementations or logging remaining
- ✅ All unit tests pass
- ✅ Integration tests show data flowing through entire pipeline

---

## Phase 5: Testing & Documentation

### 5.1 Unit Tests

**Test Files**: `orc/core/tests/observation_context_test.cpp` (new)

**Test Coverage**:
1. **ObservationContext**: Storage and retrieval
   - Set/get with different types
   - Namespace isolation
   - Missing key handling
   - Clear functionality

2. **Observer Configuration**: Validation
   - Valid configurations accepted
   - Invalid configurations rejected with clear errors
   - Defaults applied correctly
   - Required parameters enforced

3. **Pipeline Validation**: Missing observations detected
   - Required observations not provided → error
   - Required observations provided → success
   - Helpful error messages

4. **Each Observer**: Writes correct keys
   - All declared observations are written
   - Observation types match schema
   - Namespaces are correct

**Example Test**:
```cpp
TEST(ObservationContextTest, SetAndGet) {
    ObservationContext context;
    
    context.set("vitc", "timecode", std::string("01:23:45:12"));
    context.set("burst", "level", 0.85);
    
    auto timecode = context.get("vitc", "timecode");
    ASSERT_TRUE(timecode.has_value());
    EXPECT_EQ(std::get<std::string>(*timecode), "01:23:45:12");
    
    auto level = context.get("burst", "level");
    ASSERT_TRUE(level.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*level), 0.85);
    
    // Wrong namespace
    auto missing = context.get("wrong", "timecode");
    EXPECT_FALSE(missing.has_value());
}
```

### 5.2 Integration Tests

**Test Files**: `orc/core/tests/pipeline_integration_test.cpp`

**Test Scenarios**:

1. **Simple Pipeline**: `source → sink` with observers
   ```cpp
   TEST(PipelineIntegrationTest, SourceToSinkWithObservers) {
       auto source = std::make_shared<PALYCSourceStage>();
       auto sink = std::make_shared<LdSinkStage>();
       
       // Configure sink with observers
       sink->set_parameters({
           {"observers.vitc.enabled", true},
           {"observers.biphase.enabled", true}
       });
       
       ObservationContext context;
       auto vfr = source->execute({}, {}, context);
       auto output = sink->execute(vfr, {}, context);
       
       // Verify observations were collected
       EXPECT_TRUE(context.has("vitc", "timecode"));
       EXPECT_TRUE(context.has("biphase", "picture_number"));
   }
   ```

2. **Transform Pipeline**: `source → transform → sink` with requirements
   - Transform requires pulldown observations
   - Sink provides pulldown observations
   - Pipeline validation succeeds

3. **Multiple Observer Instances**: Same observer type, different configs
   - Two VITC observers with different line numbers
   - Both write to context with unique keys
   - No collision or data corruption

4. **Namespace Isolation**: Different observers don't interfere
   - Write to different namespaces
   - Read doesn't cross namespace boundaries

### 5.3 Regression Tests

**Test Coverage**:
1. **Existing Pipelines**: Still work after refactor
   - Load existing project files
   - Execute pipelines
   - Verify output matches old system

2. **Observation Output**: Matches old format
   - Compare JSON metadata output
   - Verify all fields present
   - Check value accuracy

3. **VBI Decoding Quality**: Unchanged
   - Run on known test files
   - Compare decoded VITC/biphase data
   - Verify detection rates match baseline

**Test Data**: Use existing test TBC files from project-examples/

### 5.4 Architecture Documentation

**File**: `tech-notes/observation-context-implementation.md` (new)

**Content**:
- Implementation details and rationale
- ObservationContext API reference
- Namespace naming conventions
- Performance characteristics
- Thread safety considerations

**Namespace Conventions**:
```
Naming Pattern: <observer-type>.<data-field>

Examples:
  vitc.timecode
  vitc.confidence
  vitc.line_number
  biphase.picture_number
  biphase.chapter
  biphase.side
  burst.level
  pulldown.pattern
  pulldown.phase
```

### 5.5 API Documentation

**Files**: Doxygen comments in all new/modified headers

**Key Documentation**:

1. **ObservationContext**: Full API reference
   ```cpp
   /**
    * @brief Pipeline-scoped observation storage
    * 
    * ObservationContext stores typed, namespaced observations collected
    * throughout pipeline execution. It flows alongside the VFR through
    * all stages.
    * 
    * Namespaces prevent collisions between different observer types.
    * Keys within a namespace identify specific data fields.
    * 
    * @example
    * ObservationContext context;
    * context.set("vitc", "timecode", std::string("01:23:45:12"));
    * auto tc = context.get("vitc", "timecode");
    */
   ```

2. **Stage Contracts**: Document new methods
   ```cpp
   /**
    * @brief Declare observations required for this stage
    * 
    * Pipeline validation ensures these observations are available
    * before execution. If required observations are missing, the
    * pipeline will fail to construct with a clear error message.
    * 
    * @return Vector of required observation keys
    */
   virtual std::vector<ObservationKey> get_required_observations() const;
   ```

3. **Observer Configuration**: Schema format and validation
   ```cpp
   /**
    * @brief Get configuration schema for this observer
    * 
    * Returns parameter descriptors that define valid configuration.
    * Used for validation and default value application.
    * 
    * @return Vector of parameter descriptors
    */
   virtual std::vector<ParameterDescriptor> get_configuration_schema() const;
   ```

**Migration Guide** (`docs/migration/observer-refactor.md`):
- Step-by-step guide for updating external code
- Before/after code examples
- Common migration issues and solutions

### 5.6 User Documentation

**File**: `docs-user/wiki-default/observations.md` (update)

**Content**:

1. **Sink Configuration**: How to configure observation output
   ```yaml
   # Example: Configure LD sink with observations
   ld_sink:
     output_path: /path/to/output.tbc
     observers:
       biphase:
         enabled: true
       closed_caption:
         enabled: true
       burst_level:
         enabled: true
     output_observations:
       - biphase.picture_number
       - biphase.chapter
       - biphase.side
       - closed_caption.text
       - burst.level
     observation_format: json
   ```
   
   **Note**: VITC and VideoID observers will be available in a future release.

2. **Available Observations**: Per-observer documentation
   
   | Observer | Namespace | Observation Keys | Description | Status |
   |----------|-----------|------------------|-------------|--------|
   | Biphase | biphase | picture_number, chapter, side | LaserDisc VBI data | **Implemented** |
   | Burst Level | burst | level, confidence | Color burst amplitude | **Implemented** |
   | Closed Caption | closed_caption | text, confidence | CC line 21 data | **Implemented** |
   | White Flag | white_flag | present | White flag detection | **Implemented** |
   | FM Code | fm_code | code | FM code data | **Implemented** |
   | VITS | vits | quality_metrics | Test signal analysis | **Implemented** |
   | SNR Analysis | snr | value, field_snr | Signal quality | **Implemented** |
   | VITC | vitc | timecode, confidence, line_number | Vertical Interval Timecode | *Future* |
   | Video ID | video_id | id, confidence | Video ID code | *Future* |
   
   **Note**: Pulldown observer removed (not an observation, should be part of transform logic)

3. **Examples**: Common observation output patterns
   - Metadata for video editing
   - Quality analysis datasets
   - Chapter/timecode extraction

---

## Implementation Order (Recommended)

### Milestone 1: Foundation (Week 1)
1. ✅ Phase 1.1: Create ObservationContext
2. ✅ Phase 1.2: Define observation schema
3. ✅ Phase 1.3: Refactor Observer base class
4. ✅ Phase 1.4: Observer configuration schema
5. ✅ Phase 1.6-1.7: Stage contracts and pipeline validation
6. ✅ Phase 3.2: Remove VFR observation methods (do early to force new pattern)
7. ✅ Phase 4.1: Unit tests for ObservationContext

### Milestone 2: Proof of Concept (Week 2)
8. ✅ Phase 1.5: Update one simple observer (BurstLevelObserver)
9. ✅ Phase 2.4: Update one sink (LdSinkStage) - basic integration
10. ✅ Phase 4.2: Integration test for simple source→sink pipeline
11. ✅ Verify end-to-end flow works

### Milestone 3: Full Implementation (Week 3-6)
12. ✅ Phase 1.5: Migrate all observers in parallel
13. ✅ Phase 2.1: Update DAGStage execute signature
14. ✅ Phase 2.2-2.4: Update all stages (source, transform, sink)
15. ✅ Phase 2.5-2.6: Sink output configuration and serialization
16. ✅ Phase 3.1: Update DAG executor
17. ✅ Phase 3.3: Update DAGFieldRenderer
18. ✅ Phase 4.1: Unit tests for all observers

### Milestone 4: Complete Core Implementation (Week 7-10)
19. ✅ Phase 3.4: Update GUI/CLI to use ObservationContext
20. ⏳ Phase 4.1-4.11: Complete all data extraction and GUI integration
   - Analysis decoder implementations
   - VBI data extraction
   - GUI observation display
   - Closed caption processing
   - Field mapping restoration
   - TBC metadata writer
   - Source align VBI frame extraction
   - Analysis tool instantiation

### Milestone 5: Testing & Documentation (Week 11-12)
21. ⏳ Phase 5.1: Full integration tests
22. ⏳ Phase 5.2: Regression tests (ensure output quality unchanged)
23. ⏳ Phase 5.3-5.6: Complete all documentation
24. ⏳ Final review and release

**Total Time: 10-12 weeks** (phased approach with all infrastructure verified before testing)

---

## Key Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing pipelines | High | **Acceptable** - Clean break allows better architecture |
| Complex multi-observer configuration | Medium | Start with simple cases, add complexity gradually, validate early |
| Performance overhead of ObservationContext | Medium | Make context lightweight, use efficient storage, profile early |
| Configuration complexity for users | Medium | Provide sensible defaults, clear validation errors, examples |
| Observer namespace collisions | Low | Enforce naming conventions, validate at registration |
| Missing observation errors hard to debug | Medium | Clear error messages with stage names and observation keys |
| Thread safety issues | Low | Document thread safety requirements, test with parallel pipelines |

---

## Success Criteria

### Functional Requirements
- ✅ Simple `source → sink` pipeline works with observers
- ✅ Transform stages can declare observation requirements
- ✅ Sinks can select which observations to output
- ✅ Multiple instances of same observer can coexist with different configs
- ✅ Pipeline validation catches missing observations before execution
- ✅ All existing observers migrated and tested

### Non-Functional Requirements
- ✅ No global observer state or project-level execution
- ✅ No performance regression (< 5% overhead)
- ✅ All existing test cases pass
- ✅ Code coverage maintained or improved
- ✅ Documentation complete and accurate

### Quality Metrics
- ✅ Zero P0 bugs in refactored code
- ✅ All observers have unit tests
- ✅ Integration tests cover common pipelines
- ✅ User documentation with examples
- ✅ Migration path tested with real projects

---

## Appendix A: Code Organization

### New Files
```
orc/core/include/
  observation_context.h       - ObservationContext class
  observation_schema.h        - ObservationKey and schema types
  observer_config.h           - Configuration validation
  pipeline_validator.h        - Pipeline dependency validation

orc/core/
  observation_context.cpp
  observation_serializer.h/cpp
  pipeline_validator.cpp

orc/core/tests/
  observation_context_test.cpp
  pipeline_integration_test.cpp
  observer_configuration_test.cpp
```

### Modified Files (Major)
```
orc/core/observers/
  observer.h                  - Refactored base class
  *_observer.cpp              - All observers updated

orc/core/stages/
  stage.h                     - New observation methods
  */execute()                 - All stages updated

orc/core/include/
  video_field_representation.h - Remove get_observations()

orc/gui/
  qualitymetricsdialog.cpp    - Use ObservationContext
```

### Deleted Files
```
orc/core/observers/
  observation_history.h       - Replaced by ObservationContext
  observation_history.cpp     - Replaced by ObservationContext
```

---

## Appendix B: Example Complete Pipeline

```cpp
// Create pipeline stages
auto source = std::make_shared<PALYCSourceStage>();
auto video_params = std::make_shared<VideoParamsStage>();
auto ld_sink = std::make_shared<LdSinkStage>();

// Configure sink with observers
ld_sink->set_parameters({
    {"output_path", "/path/to/output.tbc"},
    {"observers.biphase.enabled", true},
    {"observers.closed_caption.enabled", true},
    {"observers.burst_level.enabled", true},
    {"output_observations", std::vector<std::string>{
        "biphase.picture_number",
        "biphase.chapter",
        "closed_caption.text",
        "burst.level"
    }},
    {"observation_format", "json"}
});

// Note: VITC and VideoID observers not yet implemented

// Build pipeline
std::vector<DAGStagePtr> stages = {source, video_params, ld_sink};

// Validate observation dependencies
auto validation = PipelineValidator::validate_observation_dependencies(stages);
if (!validation.valid) {
    std::cerr << "Pipeline validation failed: " << validation.errors[0] << std::endl;
    return 1;
}

// Execute pipeline
ObservationContext observation_context;
DAGExecutor executor;
auto result = executor.execute_pipeline(stages, {}, observation_context);

// Observation context now contains all observations from pipeline execution
// Sink has already written selected observations to output metadata
```

---

## Conclusion

This implementation plan provides a structured, phased approach to refactoring the observer system. By starting with a small proof-of-concept (Milestone 2) before committing to the full migration, the plan reduces risk and allows for early validation of the architecture.

The resulting system will:
- Eliminate the "observers as inputs vs outputs" confusion
- Support explicit observation dependencies
- Enable flexible, stage-scoped observer configuration
- Maintain clean separation between observation collection and output
- Support both simple and complex pipelines equally well
