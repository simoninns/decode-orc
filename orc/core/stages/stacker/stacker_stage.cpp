/*
 * File:        stacker_stage.cpp
 * Module:      orc-core
 * Purpose:     Multi-source TBC stacking stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <stacker_stage.h>
#include <stage_registry.h>
#include <preview_helpers.h>
#include <logging.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace orc {

// ============================================================================
// StackedVideoFieldRepresentation implementation
// ============================================================================

StackedVideoFieldRepresentation::StackedVideoFieldRepresentation(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    StackerStage* stage)
    : VideoFieldRepresentationWrapper(
        sources.empty() ? nullptr : sources[0],
        ArtifactID("stacked_field"),
        Provenance{})
    , sources_(sources)
    , stage_(stage)
    , stacked_fields_(MAX_CACHED_FIELDS)
    , stacked_luma_fields_(MAX_CACHED_FIELDS)
    , stacked_chroma_fields_(MAX_CACHED_FIELDS)
    , stacked_dropouts_(MAX_CACHED_FIELDS)
    , stacked_audio_(MAX_CACHED_FIELDS)
    , stacked_efm_(MAX_CACHED_FIELDS)
    , best_field_index_(MAX_CACHED_FIELDS)
{
    // Validate that all sources have the same channel mode (all composite or all YC)
    if (!sources_.empty()) {
        bool first_has_separate = sources_[0]->has_separate_channels();
        for (size_t i = 1; i < sources_.size(); ++i) {
            if (sources_[i]->has_separate_channels() != first_has_separate) {
                throw std::runtime_error(
                    "StackerStage: Cannot mix composite and YC sources. "
                    "All sources must have the same channel mode (all composite or all YC).");
            }
        }
    }
}

void StackedVideoFieldRepresentation::ensure_field_stacked(FieldID field_id) const {
    // Note: Caller must already hold cache_mutex_
    
    if (sources_.empty() || !sources_[0]) {
        return;
    }
    
    // Check channel mode
    if (sources_[0]->has_separate_channels()) {
        // YC mode - check if BOTH luma AND chroma AND dropout data are in cache
        if (stacked_luma_fields_.contains(field_id) && 
            stacked_chroma_fields_.contains(field_id) && 
            stacked_dropouts_.contains(field_id)) {
            return;
        }
        
        ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking YC field {} (NOT fully cached)", field_id.value());
        
        // Stack the YC field from all sources
        std::vector<uint16_t> stacked_luma;
        std::vector<uint16_t> stacked_chroma;
        std::vector<DropoutRegion> stacked_dropouts;
        
        stage_->stack_field_yc(field_id, sources_, stacked_luma, stacked_chroma, stacked_dropouts);
        
        // Cache the results
        stacked_luma_fields_.put(field_id, std::move(stacked_luma));
        stacked_chroma_fields_.put(field_id, std::move(stacked_chroma));
        stacked_dropouts_.put(field_id, std::move(stacked_dropouts));
        
        ORC_LOG_DEBUG("  -> YC field {} stacked and cached with {} dropout regions", 
                      field_id.value(), stacked_dropouts.size());
    } else {
        // Composite mode - check if BOTH field data AND dropout data are in cache
        if (stacked_fields_.contains(field_id) && stacked_dropouts_.contains(field_id)) {
            return;
        }
        
        ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking field {} (NOT fully cached)", field_id.value());
        
        // Stack the field from all sources (alignment is done by preceding field_map stages)
        std::vector<uint16_t> stacked_samples;
        std::vector<DropoutRegion> stacked_dropouts;
        
        stage_->stack_field(field_id, sources_, stacked_samples, stacked_dropouts);
        
        // Cache the result
        stacked_fields_.put(field_id, std::move(stacked_samples));
        stacked_dropouts_.put(field_id, std::move(stacked_dropouts));
        
        ORC_LOG_DEBUG("  -> Field {} stacked and cached with {} dropout regions", 
                      field_id.value(), stacked_dropouts.size());
    }
}

FieldIDRange StackedVideoFieldRepresentation::field_range() const {
    return source_ ? source_->field_range() : FieldIDRange{};
}

size_t StackedVideoFieldRepresentation::get_source_count(FieldID field_id) const {
    // Count how many sources have this field
    size_t count = 0;
    for (const auto& source : sources_) {
        if (source && source->has_field(field_id)) {
            count++;
        }
    }
    return count;
}

const uint16_t* StackedVideoFieldRepresentation::get_line(FieldID id, size_t line) const {
    // Ensure this field has been stacked (will use lock internally)
    // We can't use the double-check pattern here because we return a pointer
    // So we must ensure the field stays in cache
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    ensure_field_stacked(id);
    
    // Check if we have a stacked version of this field in LRU cache
    const auto* cached_field = stacked_fields_.get_ptr(id);
    if (cached_field && !cached_field->empty()) {
        // Return pointer to line within the cached stacked field data
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_field)[line * descriptor->width];
        }
    }
    
    // Fallback to source (should not happen)
    return source_->get_line(id, line);
}

std::vector<uint16_t> StackedVideoFieldRepresentation::get_field(FieldID id) const {
    // First check if already cached (fast path with minimal lock time)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (stacked_fields_.contains(id) && stacked_dropouts_.contains(id)) {
            const auto* cached_field = stacked_fields_.get_ptr(id);
            if (cached_field) {
                return *cached_field;
            }
        }
    }
    
    // Not cached - stack the field WITHOUT holding the lock (slow path)
    std::vector<uint16_t> stacked_samples;
    std::vector<DropoutRegion> stacked_dropouts;
    
    ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking field {} (NOT fully cached)", id.value());
    stage_->stack_field(id, sources_, stacked_samples, stacked_dropouts);
    
    // Re-acquire lock and cache the result
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        // Check again if another thread already stacked it while we were computing
        if (stacked_fields_.contains(id) && stacked_dropouts_.contains(id)) {
            // Another thread beat us to it - return their result and discard ours
            const auto* cached_field = stacked_fields_.get_ptr(id);
            if (cached_field) {
                return *cached_field;
            }
        }
        
        // Store our result in cache
        stacked_fields_.put(id, stacked_samples);
        stacked_dropouts_.put(id, stacked_dropouts);
        
        ORC_LOG_DEBUG("  -> Field {} stacked and cached with {} dropout regions", 
                      id.value(), stacked_dropouts.size());
        
        return stacked_samples;
    }
}

std::vector<DropoutRegion> StackedVideoFieldRepresentation::get_dropout_hints(FieldID id) const {
    // First check if already cached (fast path with minimal lock time)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (stacked_fields_.contains(id) && stacked_dropouts_.contains(id)) {
            const auto* cached_dropouts = stacked_dropouts_.get_ptr(id);
            if (cached_dropouts) {
                return *cached_dropouts;
            }
        }
    }
    
    // Not cached - stack the field WITHOUT holding the lock (slow path)
    std::vector<uint16_t> stacked_samples;
    std::vector<DropoutRegion> stacked_dropouts;
    
    ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking field {} for dropout hints (NOT fully cached)", id.value());
    stage_->stack_field(id, sources_, stacked_samples, stacked_dropouts);
    
    // Re-acquire lock and cache the result
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        // Check again if another thread already stacked it while we were computing
        if (stacked_fields_.contains(id) && stacked_dropouts_.contains(id)) {
            // Another thread beat us to it - return their result and discard ours
            const auto* cached_dropouts = stacked_dropouts_.get_ptr(id);
            if (cached_dropouts) {
                return *cached_dropouts;
            }
        }
        
        // Store our result in cache
        stacked_fields_.put(id, std::move(stacked_samples));
        stacked_dropouts_.put(id, stacked_dropouts);
        
        ORC_LOG_DEBUG("  -> Field {} stacked and cached with {} dropout regions", 
                      id.value(), stacked_dropouts.size());
        
        return stacked_dropouts;
    }
}

size_t StackedVideoFieldRepresentation::get_best_source_index(FieldID field_id) const {
    // Lock mutex to ensure thread-safe cache access
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Check cache first
    const auto* cached_index = best_field_index_.get_ptr(field_id);
    if (cached_index) {
        return *cached_index;
    }
    
    // Find the source with the fewest dropouts for this field
    size_t best_index = 0;
    size_t min_dropout_count = SIZE_MAX;
    
    for (size_t i = 0; i < sources_.size(); ++i) {
        if (sources_[i] && sources_[i]->has_field(field_id)) {
            auto dropouts = sources_[i]->get_dropout_hints(field_id);
            size_t dropout_count = 0;
            for (const auto& region : dropouts) {
                dropout_count += (region.end_sample - region.start_sample);
            }
            
            if (dropout_count < min_dropout_count) {
                min_dropout_count = dropout_count;
                best_index = i;
            }
        }
    }
    
    // Cache the result
    best_field_index_.put(field_id, best_index);
    return best_index;
}

uint32_t StackedVideoFieldRepresentation::get_audio_sample_count(FieldID id) const {
    // Check if any source has audio
    if (!has_audio()) {
        return 0;
    }
    
    // Return sample count from first source that has this field with audio
    for (const auto& source : sources_) {
        if (source && source->has_field(id) && source->has_audio()) {
            return source->get_audio_sample_count(id);
        }
    }
    
    return 0;
}

std::vector<int16_t> StackedVideoFieldRepresentation::get_audio_samples(FieldID id) const {
    if (!has_audio()) {
        return {};
    }
    
    // Check audio cache first
    const auto* cached_audio = stacked_audio_.get_ptr(id);
    if (cached_audio) {
        return *cached_audio;
    }
    
    // Get best source index for this field
    size_t best_index = get_best_source_index(id);
    
    // Stack the audio samples
    auto stacked_audio = stage_->stack_audio(id, sources_, best_index);
    
    // Cache the result
    stacked_audio_.put(id, stacked_audio);
    
    return stacked_audio;
}

bool StackedVideoFieldRepresentation::has_audio() const {
    // Check if any source has audio
    for (const auto& source : sources_) {
        if (source && source->has_audio()) {
            return true;
        }
    }
    return false;
}

uint32_t StackedVideoFieldRepresentation::get_efm_sample_count(FieldID id) const {
    // Check if any source has EFM
    if (!has_efm()) {
        return 0;
    }
    
    // Return sample count from first source that has this field with EFM
    for (const auto& source : sources_) {
        if (source && source->has_field(id) && source->has_efm()) {
            return source->get_efm_sample_count(id);
        }
    }
    
    return 0;
}

std::vector<uint8_t> StackedVideoFieldRepresentation::get_efm_samples(FieldID id) const {
    if (!has_efm()) {
        return {};
    }
    
    // Check EFM cache first
    const auto* cached_efm = stacked_efm_.get_ptr(id);
    if (cached_efm) {
        return *cached_efm;
    }
    
    // Get best source index for this field
    size_t best_index = get_best_source_index(id);
    
    // Stack the EFM samples
    auto stacked_efm = stage_->stack_efm(id, sources_, best_index);
    
    // Cache the result
    stacked_efm_.put(id, stacked_efm);
    
    return stacked_efm;
}

bool StackedVideoFieldRepresentation::has_efm() const {
    // Check if any source has EFM
    for (const auto& source : sources_) {
        if (source && source->has_efm()) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Dual-channel support for YC sources
// ============================================================================

bool StackedVideoFieldRepresentation::has_separate_channels() const {
    return sources_.empty() ? false : sources_[0]->has_separate_channels();
}

const uint16_t* StackedVideoFieldRepresentation::get_line_luma(FieldID id, size_t line) const {
    if (!has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_luma(id, line);
    }
    
    // Ensure this field has been stacked (will use lock internally)
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    ensure_field_stacked(id);
    
    // Return pointer to line within the cached stacked luma field data
    const auto* cached_luma = stacked_luma_fields_.get_ptr(id);
    if (cached_luma && !cached_luma->empty()) {
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_luma)[line * descriptor->width];
        }
    }
    
    // Fallback to source (should not happen)
    return source_->get_line_luma(id, line);
}

const uint16_t* StackedVideoFieldRepresentation::get_line_chroma(FieldID id, size_t line) const {
    if (!has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_chroma(id, line);
    }
    
    // Ensure this field has been stacked (will use lock internally)
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    ensure_field_stacked(id);
    
    // Return pointer to line within the cached stacked chroma field data
    const auto* cached_chroma = stacked_chroma_fields_.get_ptr(id);
    if (cached_chroma && !cached_chroma->empty()) {
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_chroma)[line * descriptor->width];
        }
    }
    
    // Fallback to source (should not happen)
    return source_->get_line_chroma(id, line);
}

std::vector<uint16_t> StackedVideoFieldRepresentation::get_field_luma(FieldID id) const {
    if (!has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_luma(id);
    }
    
    // First check if already cached (fast path with minimal lock time)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (stacked_luma_fields_.contains(id) && 
            stacked_chroma_fields_.contains(id) && 
            stacked_dropouts_.contains(id)) {
            const auto* cached_luma = stacked_luma_fields_.get_ptr(id);
            if (cached_luma) {
                return *cached_luma;
            }
        }
    }
    
    // Not cached - stack the YC field WITHOUT holding the lock (slow path)
    std::vector<uint16_t> stacked_luma;
    std::vector<uint16_t> stacked_chroma;
    std::vector<DropoutRegion> stacked_dropouts;
    
    ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking YC field {} for luma (NOT fully cached)", id.value());
    stage_->stack_field_yc(id, sources_, stacked_luma, stacked_chroma, stacked_dropouts);
    
    // Re-acquire lock and cache the result
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        // Check again if another thread already stacked it while we were computing
        if (stacked_luma_fields_.contains(id) && 
            stacked_chroma_fields_.contains(id) && 
            stacked_dropouts_.contains(id)) {
            // Another thread beat us to it - return their result and discard ours
            const auto* cached_luma = stacked_luma_fields_.get_ptr(id);
            if (cached_luma) {
                return *cached_luma;
            }
        }
        
        // Store our result in cache
        stacked_luma_fields_.put(id, stacked_luma);
        stacked_chroma_fields_.put(id, stacked_chroma);
        stacked_dropouts_.put(id, stacked_dropouts);
        
        ORC_LOG_DEBUG("  -> YC field {} stacked and cached with {} dropout regions", 
                      id.value(), stacked_dropouts.size());
        
        return stacked_luma;
    }
}

std::vector<uint16_t> StackedVideoFieldRepresentation::get_field_chroma(FieldID id) const {
    if (!has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_chroma(id);
    }
    
    // First check if already cached (fast path with minimal lock time)
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (stacked_luma_fields_.contains(id) && 
            stacked_chroma_fields_.contains(id) && 
            stacked_dropouts_.contains(id)) {
            const auto* cached_chroma = stacked_chroma_fields_.get_ptr(id);
            if (cached_chroma) {
                return *cached_chroma;
            }
        }
    }
    
    // Not cached - stack the YC field WITHOUT holding the lock (slow path)
    std::vector<uint16_t> stacked_luma;
    std::vector<uint16_t> stacked_chroma;
    std::vector<DropoutRegion> stacked_dropouts;
    
    ORC_LOG_DEBUG("StackedVideoFieldRepresentation: stacking YC field {} for chroma (NOT fully cached)", id.value());
    stage_->stack_field_yc(id, sources_, stacked_luma, stacked_chroma, stacked_dropouts);
    
    // Re-acquire lock and cache the result
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        
        // Check again if another thread already stacked it while we were computing
        if (stacked_luma_fields_.contains(id) && 
            stacked_chroma_fields_.contains(id) && 
            stacked_dropouts_.contains(id)) {
            // Another thread beat us to it - return their result and discard ours
            const auto* cached_chroma = stacked_chroma_fields_.get_ptr(id);
            if (cached_chroma) {
                return *cached_chroma;
            }
        }
        
        // Store our result in cache
        stacked_luma_fields_.put(id, stacked_luma);
        stacked_chroma_fields_.put(id, stacked_chroma);
        stacked_dropouts_.put(id, stacked_dropouts);
        
        ORC_LOG_DEBUG("  -> YC field {} stacked and cached with {} dropout regions", 
                      id.value(), stacked_dropouts.size());
        
        return stacked_chroma;
    }
}

// ============================================================================
// StackerStage implementation
// ============================================================================

// Register the stage
ORC_REGISTER_STAGE(StackerStage)
// Force linker to include this object file
void force_link_StackerStage() {}
StackerStage::StackerStage()
    : m_mode(-1)              // Auto mode
    , m_smart_threshold(15)   // Default threshold
    , m_no_diff_dod(false)
    , m_passthrough(false)
    , m_thread_count(0)       // Auto (use all available cores)
    , m_audio_stacking_mode(AudioStackingMode::MEAN)  // Default: mean averaging
    , m_efm_stacking_mode(EFMStackingMode::MEAN)      // Default: mean averaging
{
}

std::vector<ArtifactPtr> StackerStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("StackerStage requires at least 1 input");
    }
    
    if (inputs.size() > 16) {
        throw DAGExecutionError("StackerStage supports maximum 16 inputs");
    }
    
    ORC_LOG_DEBUG("StackerStage: Processing {} input source(s)", inputs.size());
    
    // Update parameters
    if (!parameters.empty()) {
        set_parameters(parameters);
        ORC_LOG_DEBUG("StackerStage: Parameters updated - mode={}, smart_threshold={}, no_diff_dod={}, passthrough={}",
                     m_mode, m_smart_threshold, m_no_diff_dod, m_passthrough);
        // Parameters changed - invalidate cache
        cached_output_.reset();
    }
    
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> sources;
    for (const auto& input : inputs) {
        auto field_rep = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input);
        if (!field_rep) {
            throw DAGExecutionError("StackerStage input is not a VideoFieldRepresentation");
        }
        sources.push_back(field_rep);
    }
    
    // Check if we can reuse cached output
    // We need to verify the sources match to safely reuse
    bool can_reuse_cache = false;
    if (cached_output_ && cached_sources_.size() == sources.size()) {
        can_reuse_cache = true;
        for (size_t i = 0; i < sources.size(); ++i) {
            if (cached_sources_[i] != sources[i]) {
                can_reuse_cache = false;
                break;
            }
        }
    }
    
    std::shared_ptr<const VideoFieldRepresentation> result;
    
    if (can_reuse_cache) {
        ORC_LOG_DEBUG("StackerStage: Reusing cached StackedVideoFieldRepresentation");
        result = cached_output_;
    } else {
        ORC_LOG_DEBUG("StackerStage: Creating new StackedVideoFieldRepresentation");
        // Process the fields
        result = process(sources);
        
        // Cache the result and sources
        cached_output_ = result;
        cached_sources_ = sources;
    }
    
    // Return as artifact
    return {std::const_pointer_cast<VideoFieldRepresentation>(
        std::const_pointer_cast<const VideoFieldRepresentation>(result))};
}

std::shared_ptr<const VideoFieldRepresentation> 
StackerStage::process(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    if (sources.empty()) {
        ORC_LOG_DEBUG("StackerStage::process - No sources provided");
        return nullptr;
    }
    
    // Passthrough mode: single input
    if (sources.size() == 1) {
        ORC_LOG_INFO("StackerStage::process - Passthrough mode (single source), returning source directly");
        return sources[0];
    }
    
    ORC_LOG_INFO("StackerStage::process - Creating StackedVideoFieldRepresentation for {} sources", sources.size());
    
    // Create stacked representation - will build frame alignment and process fields on-demand
    auto stacked = std::make_shared<StackedVideoFieldRepresentation>(
        sources,
        const_cast<StackerStage*>(this)
    );
    
    ORC_LOG_DEBUG("StackerStage::process - Returning StackedVideoFieldRepresentation with type: {}", stacked->type_name());
    return stacked;
}

void StackerStage::stack_field(
    FieldID field_id,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    std::vector<uint16_t>& output_samples,
    std::vector<DropoutRegion>& output_dropouts) const
{
    ORC_LOG_DEBUG("StackerStage::stack_field - Processing field {} from {} sources", 
                 field_id.value(), sources.size());
    
    // Get descriptor from first valid source
    std::optional<FieldDescriptor> descriptor;
    size_t reference_idx = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i]->has_field(field_id)) {
            descriptor = sources[i]->get_descriptor(field_id);
            if (descriptor) {
                reference_idx = i;
                break;
            }
        }
    }
    
    if (!descriptor) {
        ORC_LOG_ERROR("StackerStage: No valid field descriptor available for field {}", field_id.value());
        throw DAGExecutionError("StackerStage: No valid field descriptor available");
    }
    
    size_t width = descriptor->width;
    size_t height = descriptor->height;
    
    ORC_LOG_DEBUG("StackerStage::stack_field - Field dimensions: {}x{}", width, height);
    
    // Get video parameters for black level
    auto video_params = sources[reference_idx]->get_video_parameters();
    if (!video_params) {
        ORC_LOG_ERROR("StackerStage: Video parameters not available");
        throw DAGExecutionError("StackerStage: Video parameters not available");
    }
    
    // Resize output
    output_samples.resize(width * height);
    output_dropouts.clear();
    
    size_t total_dropouts = 0;
    size_t total_diff_dod_recoveries = 0;
    size_t total_stacked_pixels = 0;
    
    // Pre-load all source fields into memory to avoid repeated get_line() calls
    // This dramatically improves performance by eliminating wrapper call overhead
    std::vector<std::vector<uint16_t>> all_fields;
    std::vector<bool> field_valid;
    all_fields.reserve(sources.size());
    field_valid.reserve(sources.size());
    
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i]->has_field(field_id)) {
            all_fields.push_back(sources[i]->get_field(field_id));
            field_valid.push_back(!all_fields.back().empty());
        } else {
            all_fields.push_back({});
            field_valid.push_back(false);
        }
    }
    
    // Pre-collect all dropout maps for fast lookup
    std::vector<std::vector<DropoutRegion>> all_dropouts;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (field_valid[i]) {
            auto dropouts = sources[i]->get_dropout_hints(field_id);
            all_dropouts.push_back(std::move(dropouts));
        } else {
            all_dropouts.push_back({}); // Empty dropout list for sources without this field
        }
    }
    
    // Determine number of threads to use
    size_t num_threads = m_thread_count;
    if (num_threads == 0) {
        // Auto: use all available hardware threads
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4; // Fallback if detection fails
    }
    
    // For small fields or single-threaded mode, don't use threading
    if (num_threads == 1 || height < num_threads * 4) {
        num_threads = 1;
    }
    
    ORC_LOG_DEBUG("StackerStage::stack_field - Using {} thread(s) for processing", num_threads);
    
    // Multi-threaded line processing
    if (num_threads == 1) {
        // Single-threaded path (for small fields or when explicitly set to 1 thread)
        process_lines_range(0, height, width, all_fields, field_valid, all_dropouts,
                          sources.size(), *video_params, output_samples, output_dropouts,
                          total_dropouts, total_diff_dod_recoveries, total_stacked_pixels);
    } else {
        // Multi-threaded path
        std::vector<std::thread> threads;
        std::vector<std::vector<DropoutRegion>> thread_dropouts(num_threads);
        std::vector<size_t> thread_total_dropouts(num_threads, 0);
        std::vector<size_t> thread_total_recoveries(num_threads, 0);
        std::vector<size_t> thread_total_stacked(num_threads, 0);
        
        // Calculate lines per thread
        size_t lines_per_thread = (height + num_threads - 1) / num_threads;
        
        // Launch worker threads
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start_line = t * lines_per_thread;
            size_t end_line = std::min(start_line + lines_per_thread, height);
            
            if (start_line >= height) break;
            
            threads.emplace_back([this, start_line, end_line, width, &all_fields, &field_valid,
                                 &all_dropouts, num_sources = sources.size(), &video_params,
                                 &output_samples, &thread_dropouts, &thread_total_dropouts,
                                 &thread_total_recoveries, &thread_total_stacked, t]() {
                process_lines_range(start_line, end_line, width, all_fields, field_valid,
                                  all_dropouts, num_sources, *video_params, output_samples,
                                  thread_dropouts[t], thread_total_dropouts[t],
                                  thread_total_recoveries[t], thread_total_stacked[t]);
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Merge results from all threads
        for (size_t t = 0; t < num_threads; ++t) {
            output_dropouts.insert(output_dropouts.end(),
                                 thread_dropouts[t].begin(),
                                 thread_dropouts[t].end());
            total_dropouts += thread_total_dropouts[t];
            total_diff_dod_recoveries += thread_total_recoveries[t];
            total_stacked_pixels += thread_total_stacked[t];
        }
    }
    
    ORC_LOG_DEBUG("StackerStage::stack_field - Field {}: {} dropout regions, {} pixels affected, {} diff_dod recoveries",
                 field_id.value(), output_dropouts.size(), total_dropouts, total_diff_dod_recoveries);
}

void StackerStage::stack_field_yc(
    FieldID field_id,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    std::vector<uint16_t>& output_luma,
    std::vector<uint16_t>& output_chroma,
    std::vector<DropoutRegion>& output_dropouts) const
{
    ORC_LOG_DEBUG("StackerStage::stack_field_yc - Processing YC field {} from {} sources", 
                 field_id.value(), sources.size());
    
    // Validate that all sources have separate channels
    for (const auto& source : sources) {
        if (source && source->has_field(field_id) && !source->has_separate_channels()) {
            ORC_LOG_ERROR("StackerStage::stack_field_yc - Source does not have separate channels");
            throw DAGExecutionError("StackerStage: stack_field_yc called with composite source");
        }
    }
    
    // Get descriptor from first valid source
    std::optional<FieldDescriptor> descriptor;
    size_t reference_idx = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i]->has_field(field_id)) {
            descriptor = sources[i]->get_descriptor(field_id);
            if (descriptor) {
                reference_idx = i;
                break;
            }
        }
    }
    
    if (!descriptor) {
        ORC_LOG_ERROR("StackerStage: No valid field descriptor available for YC field {}", field_id.value());
        throw DAGExecutionError("StackerStage: No valid field descriptor available");
    }
    
    size_t width = descriptor->width;
    size_t height = descriptor->height;
    
    ORC_LOG_DEBUG("StackerStage::stack_field_yc - Field dimensions: {}x{}", width, height);
    
    // Get video parameters for black level
    auto video_params = sources[reference_idx]->get_video_parameters();
    if (!video_params) {
        ORC_LOG_ERROR("StackerStage: Video parameters not available");
        throw DAGExecutionError("StackerStage: Video parameters not available");
    }
    
    // Resize outputs
    output_luma.resize(width * height);
    output_chroma.resize(width * height);
    output_dropouts.clear();
    
    // Pre-load all source luma and chroma fields into memory
    std::vector<std::vector<uint16_t>> all_luma_fields;
    std::vector<std::vector<uint16_t>> all_chroma_fields;
    std::vector<bool> field_valid;
    all_luma_fields.reserve(sources.size());
    all_chroma_fields.reserve(sources.size());
    field_valid.reserve(sources.size());
    
    for (size_t i = 0; i < sources.size(); ++i) {
        if (sources[i]->has_field(field_id)) {
            all_luma_fields.push_back(sources[i]->get_field_luma(field_id));
            all_chroma_fields.push_back(sources[i]->get_field_chroma(field_id));
            field_valid.push_back(!all_luma_fields.back().empty() && !all_chroma_fields.back().empty());
        } else {
            all_luma_fields.push_back({});
            all_chroma_fields.push_back({});
            field_valid.push_back(false);
        }
    }
    
    // Pre-collect all dropout maps (same for Y and C)
    std::vector<std::vector<DropoutRegion>> all_dropouts;
    for (size_t i = 0; i < sources.size(); ++i) {
        if (field_valid[i]) {
            auto dropouts = sources[i]->get_dropout_hints(field_id);
            all_dropouts.push_back(std::move(dropouts));
        } else {
            all_dropouts.push_back({});
        }
    }
    
    // Determine number of threads to use
    size_t num_threads = m_thread_count;
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }
    
    if (num_threads == 1 || height < num_threads * 4) {
        num_threads = 1;
    }
    
    ORC_LOG_DEBUG("StackerStage::stack_field_yc - Using {} thread(s) for processing", num_threads);
    
    size_t total_dropouts = 0;
    size_t total_diff_dod_recoveries = 0;
    size_t total_stacked_pixels = 0;
    
    // Stack luma and chroma independently using the same dropout logic
    if (num_threads == 1) {
        // Single-threaded path
        process_lines_range(0, height, width, all_luma_fields, field_valid, all_dropouts,
                          sources.size(), *video_params, output_luma, output_dropouts,
                          total_dropouts, total_diff_dod_recoveries, total_stacked_pixels);
        
        // Stack chroma (reuse dropout map)
        std::vector<DropoutRegion> chroma_dropouts;  // Will be discarded
        size_t chroma_total_dropouts = 0;
        size_t chroma_total_recoveries = 0;
        size_t chroma_total_stacked = 0;
        process_lines_range(0, height, width, all_chroma_fields, field_valid, all_dropouts,
                          sources.size(), *video_params, output_chroma, chroma_dropouts,
                          chroma_total_dropouts, chroma_total_recoveries, chroma_total_stacked);
    } else {
        // Multi-threaded path
        std::vector<std::thread> threads;
        std::vector<std::vector<DropoutRegion>> thread_dropouts(num_threads);
        std::vector<size_t> thread_total_dropouts(num_threads, 0);
        std::vector<size_t> thread_total_recoveries(num_threads, 0);
        std::vector<size_t> thread_total_stacked(num_threads, 0);
        
        size_t lines_per_thread = (height + num_threads - 1) / num_threads;
        
        // Launch worker threads for luma
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start_line = t * lines_per_thread;
            size_t end_line = std::min(start_line + lines_per_thread, height);
            
            if (start_line >= height) break;
            
            threads.emplace_back([this, start_line, end_line, width, &all_luma_fields, &field_valid,
                                 &all_dropouts, num_sources = sources.size(), &video_params,
                                 &output_luma, &thread_dropouts, &thread_total_dropouts,
                                 &thread_total_recoveries, &thread_total_stacked, t]() {
                process_lines_range(start_line, end_line, width, all_luma_fields, field_valid,
                                  all_dropouts, num_sources, *video_params, output_luma,
                                  thread_dropouts[t], thread_total_dropouts[t],
                                  thread_total_recoveries[t], thread_total_stacked[t]);
            });
        }
        
        // Wait for luma threads
        for (auto& thread : threads) {
            thread.join();
        }
        threads.clear();
        
        // Launch worker threads for chroma
        std::vector<std::vector<DropoutRegion>> chroma_thread_dropouts(num_threads);  // Will be discarded
        std::vector<size_t> chroma_thread_total_dropouts(num_threads, 0);
        std::vector<size_t> chroma_thread_total_recoveries(num_threads, 0);
        std::vector<size_t> chroma_thread_total_stacked(num_threads, 0);
        
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start_line = t * lines_per_thread;
            size_t end_line = std::min(start_line + lines_per_thread, height);
            
            if (start_line >= height) break;
            
            threads.emplace_back([this, start_line, end_line, width, &all_chroma_fields, &field_valid,
                                 &all_dropouts, num_sources = sources.size(), &video_params,
                                 &output_chroma, &chroma_thread_dropouts, &chroma_thread_total_dropouts,
                                 &chroma_thread_total_recoveries, &chroma_thread_total_stacked, t]() {
                process_lines_range(start_line, end_line, width, all_chroma_fields, field_valid,
                                  all_dropouts, num_sources, *video_params, output_chroma,
                                  chroma_thread_dropouts[t], chroma_thread_total_dropouts[t],
                                  chroma_thread_total_recoveries[t], chroma_thread_total_stacked[t]);
            });
        }
        
        // Wait for chroma threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Merge luma thread results
        for (size_t t = 0; t < num_threads; ++t) {
            output_dropouts.insert(output_dropouts.end(),
                                 thread_dropouts[t].begin(),
                                 thread_dropouts[t].end());
            total_dropouts += thread_total_dropouts[t];
            total_diff_dod_recoveries += thread_total_recoveries[t];
            total_stacked_pixels += thread_total_stacked[t];
        }
    }
    
    ORC_LOG_DEBUG("StackerStage::stack_field_yc - YC field {}: {} dropout regions, {} pixels affected, {} diff_dod recoveries",
                 field_id.value(), output_dropouts.size(), total_dropouts, total_diff_dod_recoveries);
}

void StackerStage::process_lines_range(
    size_t start_line,
    size_t end_line,
    size_t width,
    const std::vector<std::vector<uint16_t>>& all_fields,
    const std::vector<bool>& field_valid,
    const std::vector<std::vector<DropoutRegion>>& all_dropouts,
    size_t num_sources,
    const VideoParameters& video_params,
    std::vector<uint16_t>& output_samples,
    std::vector<DropoutRegion>& output_dropouts,
    size_t& total_dropouts,
    size_t& total_diff_dod_recoveries,
    size_t& total_stacked_pixels) const
{
    // Process each line in the assigned range
    for (size_t y = start_line; y < end_line; ++y) {
        size_t line_dropouts = 0;
        size_t line_recoveries = 0;
        size_t line_stacked = 0;
        
        DropoutRegion current_dropout{};
        current_dropout.line = static_cast<uint32_t>(y);
        current_dropout.start_sample = 0;
        current_dropout.end_sample = 0;
        bool in_dropout = false;
        
        for (size_t x = 0; x < width; ++x) {
            std::vector<uint16_t> values;
            std::vector<uint16_t> dropout_values;  // Separate collection for dropout pixels
            std::vector<bool> is_dropout(num_sources);
            
            // Collect values from all sources for this field
            for (size_t src_idx = 0; src_idx < num_sources; ++src_idx) {
                // Skip if this source doesn't have this field
                if (!field_valid[src_idx]) {
                    is_dropout[src_idx] = true;
                    continue;
                }
                
                // Access pre-loaded field data directly
                size_t pixel_offset = y * width + x;
                if (pixel_offset >= all_fields[src_idx].size()) {
                    is_dropout[src_idx] = true;
                    continue;
                }
                
                uint16_t pixel_value = all_fields[src_idx][pixel_offset];
                
                // Check dropout status
                bool pixel_is_dropout = false;
                for (const auto& region : all_dropouts[src_idx]) {
                    if (y == region.line && x >= region.start_sample && x < region.end_sample) {
                        pixel_is_dropout = true;
                        break;
                    }
                }
                
                is_dropout[src_idx] = pixel_is_dropout;
                
                // Collect non-dropout pixels and dropout pixels separately
                if (!pixel_is_dropout) {
                    values.push_back(pixel_value);
                } else if (!m_no_diff_dod && pixel_value > 0) {
                    // Keep dropout values for potential diff_dod recovery
                    dropout_values.push_back(pixel_value);
                }
            }
            
            // Apply differential dropout detection only when ALL sources have dropouts
            bool all_dropouts_flag = std::all_of(is_dropout.begin(), is_dropout.end(), 
                                           [](bool b) { return b; });
            if (all_dropouts_flag && num_sources >= 3 && !m_no_diff_dod && !dropout_values.empty()) {
                // All sources marked this as dropout - try to recover using diff_dod
                size_t before_count = dropout_values.size();
                values = diff_dod(dropout_values, video_params);
                if (values.size() > 0 && values.size() < before_count) {
                    line_recoveries++;
                    total_diff_dod_recoveries++;
                }
            }
            
            // Calculate stacked value
            uint16_t stacked_value;
            if (values.empty()) {
                // No valid values - use black level
                stacked_value = video_params.black_16b_ire;
                line_dropouts++;
                total_dropouts++;
                
                // Track dropout region
                if (!in_dropout) {
                    current_dropout.start_sample = static_cast<uint32_t>(x);
                    in_dropout = true;
                }
                current_dropout.end_sample = static_cast<uint32_t>(x + 1);
            } else {
                // For simple modes (no neighbor checking)
                std::vector<uint16_t> dummy;
                std::vector<bool> dropout_flags(5, false);
                dropout_flags[0] = all_dropouts_flag;
                stacked_value = stack_mode(values, dummy, dummy, dummy, dummy, dropout_flags);
                line_stacked++;
                total_stacked_pixels++;
                
                // End current dropout region if we were in one
                if (in_dropout && current_dropout.start_sample < current_dropout.end_sample) {
                    output_dropouts.push_back(current_dropout);
                    in_dropout = false;
                }
            }
            
            output_samples[y * width + x] = stacked_value;
        }
        
        // Finalize dropout region at end of line
        if (in_dropout && current_dropout.start_sample < current_dropout.end_sample) {
            output_dropouts.push_back(current_dropout);
        }
        
        // Trace logging per line
        if (line_dropouts > 0 || line_recoveries > 0) {
            ORC_LOG_TRACE("StackerStage: Line {}: stacked={}, dropouts={}, diff_dod_recoveries={}",
                         y, line_stacked, line_dropouts, line_recoveries);
        }
    }
}

uint16_t StackerStage::stack_mode(
    const std::vector<uint16_t>& values,
    const std::vector<uint16_t>& /*values_n*/,  // TODO: Implement neighbor modes (3, 4)
    const std::vector<uint16_t>& /*values_s*/,
    const std::vector<uint16_t>& /*values_e*/,
    const std::vector<uint16_t>& /*values_w*/,
    const std::vector<bool>& /*all_dropout*/) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t num_elements = values.size();
    int32_t mode = m_mode;
    
    // Auto mode: select based on number of sources
    if (mode == -1) {
        if (num_elements >= 3) {
            mode = 2; // Smart mean for 3+ sources
        } else {
            mode = 0; // Mean for 2 sources
        }
        static bool auto_mode_logged = false;
        if (!auto_mode_logged) {
            ORC_LOG_DEBUG("StackerStage: Auto mode selected mode {} for {} elements", mode, num_elements);
            auto_mode_logged = true;
        }
    }
    
    switch (mode) {
        case 0: // Mean
            return static_cast<uint16_t>(mean(values));
            
        case 1: // Median
            return median(values);
            
        case 2: { // Smart Mean
            const int32_t med = median(values);
            int32_t sum = 0;
            size_t count = 0;
            
            // Sum values within threshold of median
            for (const auto& val : values) {
                if (val < static_cast<uint32_t>(med + m_smart_threshold) && 
                    val > static_cast<uint32_t>(med - m_smart_threshold)) {
                    sum += val;
                    count++;
                }
            }
            
            static size_t smart_mean_calls = 0;
            if ((smart_mean_calls++ % 10000) == 0) {
                ORC_LOG_TRACE("StackerStage: Smart Mean - median={}, selected {}/{} values within threshold {}",
                             med, count, num_elements, m_smart_threshold);
            }
            
            if (count == 0) {
                return med;
            }
            return static_cast<uint16_t>(sum / count);
        }
        
        case 3: // Smart Neighbor
        case 4: { // Neighbor
            // For now, fall back to median when neighbor data not available
            // Full implementation would use values_n, values_s, values_e, values_w
            return median(values);
        }
        
        default:
            return median(values);
    }
}

uint16_t StackerStage::median(std::vector<uint16_t> values) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t n = values.size();
    
    if (n % 2 == 0) {
        // Even number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        std::nth_element(values.begin(), values.begin() + (n - 1) / 2, values.end());
        return static_cast<uint16_t>((values[(n - 1) / 2] + values[n / 2]) / 2.0);
    } else {
        // Odd number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        return values[n / 2];
    }
}

int32_t StackerStage::mean(const std::vector<uint16_t>& values) const
{
    if (values.empty()) {
        return 0;
    }
    
    uint32_t sum = 0;
    for (const auto& val : values) {
        sum += val;
    }
    
    return static_cast<int32_t>(sum / values.size());
}

uint16_t StackerStage::closest(const std::vector<uint16_t>& values, int32_t target) const
{
    if (values.empty()) {
        return 0;
    }
    
    uint16_t result = values[0];
    int32_t min_diff = std::abs(target - static_cast<int32_t>(values[0]));
    
    for (size_t i = 1; i < values.size(); ++i) {
        int32_t diff = std::abs(target - static_cast<int32_t>(values[i]));
        if (diff < min_diff) {
            min_diff = diff;
            result = values[i];
        }
    }
    
    return result;
}

std::vector<uint16_t> StackerStage::diff_dod(
    const std::vector<uint16_t>& input_values,
    const VideoParameters& /*video_params*/) const
{
    std::vector<uint16_t> result;
    
    if (input_values.size() < 3) {
        return result;
    }
    
    // Differential dropout detection logic
    // Check if values are similar enough to be considered valid
    const int32_t med = median(const_cast<std::vector<uint16_t>&>(
        const_cast<std::vector<uint16_t>&>(input_values)));
    
    const int32_t threshold = 500; // Threshold for diff_dod
    
    for (const auto& val : input_values) {
        if (std::abs(static_cast<int32_t>(val) - med) < threshold) {
            result.push_back(val);
        }
    }
    
    static size_t diff_dod_calls = 0;
    static size_t diff_dod_recoveries = 0;
    if (result.size() > 0) {
        diff_dod_recoveries++;
    }
    if ((++diff_dod_calls % 1000) == 0) {
        ORC_LOG_DEBUG("StackerStage: Differential DOD stats - calls={}, recoveries={} ({:.1f}%)",
                     diff_dod_calls, diff_dod_recoveries, 
                     (100.0 * diff_dod_recoveries) / diff_dod_calls);
    }
    
    return result;
}

std::vector<ParameterDescriptor> StackerStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;  // Unused - stacker works with all formats
    (void)source_type;     // Unused - stacker works with all source types
    std::vector<ParameterDescriptor> descriptors;
    
    // Stacking mode
    ParameterDescriptor mode_desc;
    mode_desc.name = "mode";
    mode_desc.display_name = "Stacking Mode";
    mode_desc.description = "Algorithm for combining multiple sources";
    mode_desc.type = ParameterType::STRING;
    mode_desc.constraints.allowed_strings = {
        "Auto",
        "Mean",
        "Median",
        "Smart Mean",
        "Smart Neighbor",
        "Neighbor"
    };
    mode_desc.constraints.default_value = std::string("Auto");
    mode_desc.constraints.required = false;
    descriptors.push_back(mode_desc);
    
    // Smart threshold
    ParameterDescriptor threshold_desc;
    threshold_desc.name = "smart_threshold";
    threshold_desc.display_name = "Smart Threshold";
    threshold_desc.description = "Range threshold for smart modes (0-128, default 15)\n"
                                "Lower values are more selective (fewer sources included in averaging)\n"
                                "Higher values are more inclusive (more sources included)\n"
                                "Only used when mode is 2 (Smart Mean) or 3 (Smart Neighbor)";
    threshold_desc.type = ParameterType::INT32;
    threshold_desc.constraints.min_value = static_cast<int32_t>(0);
    threshold_desc.constraints.max_value = static_cast<int32_t>(128);
    threshold_desc.constraints.default_value = static_cast<int32_t>(15);
    threshold_desc.constraints.required = false;
    descriptors.push_back(threshold_desc);
    
    // No differential dropout detection
    ParameterDescriptor no_diff_dod_desc;
    no_diff_dod_desc.name = "no_diff_dod";
    no_diff_dod_desc.display_name = "Disable Differential Dropout Detection";
    no_diff_dod_desc.description = "When disabled (false), allows recovery of pixels incorrectly marked as dropouts\n"
                                   "by comparing values across sources (requires 3+ sources)\n"
                                   "Enable (true) if you want to strictly trust dropout markings";
    no_diff_dod_desc.type = ParameterType::BOOL;
    no_diff_dod_desc.constraints.default_value = false;
    no_diff_dod_desc.constraints.required = false;
    descriptors.push_back(no_diff_dod_desc);
    
    // Passthrough
    ParameterDescriptor passthrough_desc;
    passthrough_desc.name = "passthrough";
    passthrough_desc.display_name = "Passthrough Universal Dropouts";
    passthrough_desc.description = "When enabled (true), preserves dropout regions that appear in ALL sources\n"
                                   "Useful when every capture has the same physical damage\n"
                                   "When disabled (false), attempts to stack even universal dropouts";
    passthrough_desc.type = ParameterType::BOOL;
    passthrough_desc.constraints.default_value = false;
    passthrough_desc.constraints.required = false;
    descriptors.push_back(passthrough_desc);
    
    // Audio stacking mode
    ParameterDescriptor audio_stacking_desc;
    audio_stacking_desc.name = "audio_stacking";
    audio_stacking_desc.display_name = "Audio Stacking Mode";
    audio_stacking_desc.description = "How to combine audio from multiple sources:\n"
                                     "Disabled = Use audio from best field (determined by video quality)\n"
                                     "Mean = Average audio samples across all sources\n"
                                     "Median = Use median audio sample value across all sources\n"
                                     "Note: Only fields with matching sample counts are stacked together";
    audio_stacking_desc.type = ParameterType::STRING;
    audio_stacking_desc.constraints.allowed_strings = {
        "Disabled",
        "Mean",
        "Median"
    };
    audio_stacking_desc.constraints.default_value = std::string("Mean");
    audio_stacking_desc.constraints.required = false;
    descriptors.push_back(audio_stacking_desc);
    
    // EFM stacking mode
    ParameterDescriptor efm_stacking_desc;
    efm_stacking_desc.name = "efm_stacking";
    efm_stacking_desc.display_name = "EFM Stacking Mode";
    efm_stacking_desc.description = "How to combine EFM t-values from multiple sources:\n"
                                   "Disabled = Use EFM from best field (determined by video quality)\n"
                                   "Mean = Average EFM t-values across all sources\n"
                                   "Median = Use median EFM t-value across all sources\n"
                                   "Note: Only fields with matching t-value counts are stacked together";
    efm_stacking_desc.type = ParameterType::STRING;
    efm_stacking_desc.constraints.allowed_strings = {
        "Disabled",
        "Mean",
        "Median"
    };
    efm_stacking_desc.constraints.default_value = std::string("Mean");
    efm_stacking_desc.constraints.required = false;
    descriptors.push_back(efm_stacking_desc);
    
    return descriptors;
}

std::map<std::string, ParameterValue> StackerStage::get_parameters() const
{
    return parameters_;
}

bool StackerStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Store parameters
    parameters_ = params;
    
    bool any_changed = false;
    int32_t old_mode = m_mode;
    int32_t old_threshold = m_smart_threshold;
    
    for (const auto& [key, value] : params) {
        if (key == "mode") {
            // Accept both string and int32 for compatibility
            if (auto* val = std::get_if<std::string>(&value)) {
                if (*val == "Auto") {
                    m_mode = -1;
                } else if (*val == "Mean") {
                    m_mode = 0;
                } else if (*val == "Median") {
                    m_mode = 1;
                } else if (*val == "Smart Mean") {
                    m_mode = 2;
                } else if (*val == "Smart Neighbor") {
                    m_mode = 3;
                } else if (*val == "Neighbor") {
                    m_mode = 4;
                } else {
                    ORC_LOG_WARN("StackerStage: Invalid mode value '{}'", *val);
                    return false;
                }
                if (m_mode != old_mode) {
                    any_changed = true;
                }
            } else if (auto* val = std::get_if<int32_t>(&value)) {
                // Support legacy integer values
                if (*val < -1 || *val > 4) {
                    ORC_LOG_WARN("StackerStage: Invalid mode value {} (must be -1 to 4)", *val);
                    return false;
                }
                if (m_mode != *val) {
                    any_changed = true;
                }
                m_mode = *val;
            } else {
                return false;
            }
        } else if (key == "smart_threshold") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                if (*val < 0 || *val > 128) {
                    return false;
                }
                m_smart_threshold = *val;
            } else {
                return false;
            }
        } else if (key == "no_diff_dod") {
            if (auto* val = std::get_if<bool>(&value)) {
                m_no_diff_dod = *val;
            } else {
                return false;
            }
        } else if (key == "passthrough") {
            if (auto* val = std::get_if<bool>(&value)) {
                m_passthrough = *val;
            } else {
                return false;
            }
        } else if (key == "audio_stacking") {
            if (auto* val = std::get_if<std::string>(&value)) {
                if (*val == "Disabled") {
                    m_audio_stacking_mode = AudioStackingMode::DISABLED;
                } else if (*val == "Mean") {
                    m_audio_stacking_mode = AudioStackingMode::MEAN;
                } else if (*val == "Median") {
                    m_audio_stacking_mode = AudioStackingMode::MEDIAN;
                } else {
                    ORC_LOG_WARN("StackerStage: Invalid audio_stacking value '{}'", *val);
                    return false;
                }
            } else {
                return false;
            }
        } else if (key == "efm_stacking") {
            if (auto* val = std::get_if<std::string>(&value)) {
                if (*val == "Disabled") {
                    m_efm_stacking_mode = EFMStackingMode::DISABLED;
                } else if (*val == "Mean") {
                    m_efm_stacking_mode = EFMStackingMode::MEAN;
                } else if (*val == "Median") {
                    m_efm_stacking_mode = EFMStackingMode::MEDIAN;
                } else {
                    ORC_LOG_WARN("StackerStage: Invalid efm_stacking value '{}'", *val);
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    
    if (any_changed) {
        ORC_LOG_DEBUG("StackerStage: Parameters changed - mode: {} -> {}, threshold: {} -> {}",
                     old_mode, m_mode, old_threshold, m_smart_threshold);
    }
    
    return true;
}

std::optional<StageReport> StackerStage::generate_report() const {
    StageReport report;
    
    // Summary
    const char* mode_names[] = {"Auto", "Mean", "Median", "Smart Mean", "Smart Neighbor", "Neighbor"};
    int mode_index = m_mode + 1;  // -1 (Auto) becomes 0
    if (mode_index < 0 || mode_index >= 6) mode_index = 0;
    
    const char* stacking_mode_names[] = {"Disabled", "Mean", "Median"};
    int audio_mode_index = static_cast<int>(m_audio_stacking_mode);
    if (audio_mode_index < 0 || audio_mode_index >= 3) audio_mode_index = 0;
    
    int efm_mode_index = static_cast<int>(m_efm_stacking_mode);
    if (efm_mode_index < 0 || efm_mode_index >= 3) efm_mode_index = 0;
    
    report.summary = "Stacker Configuration";
    
    // Configuration items
    report.items.push_back({"Stacking Mode", mode_names[mode_index]});
    report.items.push_back({"Smart Threshold", std::to_string(m_smart_threshold)});
    report.items.push_back({"Differential Dropout Detection", m_no_diff_dod ? "Disabled" : "Enabled"});
    report.items.push_back({"Dropout Passthrough", m_passthrough ? "Enabled" : "Disabled"});
    report.items.push_back({"Audio Stacking", stacking_mode_names[audio_mode_index]});
    report.items.push_back({"EFM Stacking", stacking_mode_names[efm_mode_index]});
    
    // Metrics
    report.metrics["mode"] = static_cast<int64_t>(m_mode);
    report.metrics["smart_threshold"] = static_cast<int64_t>(m_smart_threshold);
    report.metrics["audio_stacking_mode"] = static_cast<int64_t>(m_audio_stacking_mode);
    report.metrics["efm_stacking_mode"] = static_cast<int64_t>(m_efm_stacking_mode);
    
    return report;
}

std::vector<PreviewOption> StackerStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage StackerStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_DEBUG("Stacker PREVIEW: option '{}' index {} rendered in {} ms (hint={})",
                 option_id, index, duration_ms, hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random");
    return result;
}

std::vector<int16_t> StackerStage::stack_audio(
    FieldID field_id,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    size_t best_source_index) const
{
    // If audio stacking is disabled, use the best source's audio
    if (m_audio_stacking_mode == AudioStackingMode::DISABLED) {
        if (best_source_index < sources.size() && 
            sources[best_source_index] && 
            sources[best_source_index]->has_audio()) {
            return sources[best_source_index]->get_audio_samples(field_id);
        }
        // Fallback to first source with audio
        for (const auto& source : sources) {
            if (source && source->has_audio() && source->has_field(field_id)) {
                return source->get_audio_samples(field_id);
            }
        }
        return {};
    }
    
    // Collect audio samples from all sources
    std::vector<std::vector<int16_t>> all_audio_samples;
    std::vector<uint32_t> sample_counts;
    std::vector<size_t> source_indices;
    size_t total_sources_with_audio = 0;
    size_t sources_without_audio = 0;
    size_t sources_without_field = 0;
    
    for (size_t src_idx = 0; src_idx < sources.size(); ++src_idx) {
        const auto& source = sources[src_idx];
        if (!source) {
            continue;
        }
        
        if (!source->has_audio()) {
            sources_without_audio++;
            continue;
        }
        
        if (!source->has_field(field_id)) {
            sources_without_field++;
            continue;
        }
        
        total_sources_with_audio++;
        uint32_t sample_count = source->get_audio_sample_count(field_id);
        auto samples = source->get_audio_samples(field_id);
        
        if (!samples.empty()) {
            all_audio_samples.push_back(std::move(samples));
            sample_counts.push_back(sample_count);
            source_indices.push_back(src_idx);
        }
    }
    
    // Log source availability summary
    if (sources_without_audio > 0 || sources_without_field > 0) {
        ORC_LOG_DEBUG("StackerStage: Field {} - total {} sources, {} without audio, {} without this field, {} with audio available",
                     field_id.value(), sources.size(), sources_without_audio, sources_without_field, total_sources_with_audio);
    }
    
    // If no audio sources available, return empty
    if (all_audio_samples.empty()) {
        ORC_LOG_DEBUG("StackerStage: No audio sources available for field {}", field_id.value());
        return {};
    }
    
    // If only one source has audio, return it directly
    if (all_audio_samples.size() == 1) {
        ORC_LOG_DEBUG("StackerStage: Only 1 audio source available for field {} (source {}, {} samples), using it directly", 
                     field_id.value(), source_indices[0], sample_counts[0]);
        return all_audio_samples[0];
    }
    
    // Sanity check: ensure all sources have the same number of samples
    uint32_t expected_sample_count = sample_counts[0];
    std::vector<size_t> matching_indices;
    std::vector<size_t> mismatched_indices;
    std::vector<uint32_t> mismatched_counts;
    
    matching_indices.push_back(source_indices[0]);
    
    for (size_t i = 1; i < sample_counts.size(); ++i) {
        if (sample_counts[i] != expected_sample_count) {
            mismatched_indices.push_back(source_indices[i]);
            mismatched_counts.push_back(sample_counts[i]);
        } else {
            matching_indices.push_back(source_indices[i]);
        }
    }
    
    if (!mismatched_indices.empty()) {
        // Build summary strings for rejected sources
        std::string rejected_indices_str = "[";
        std::string rejected_counts_str = "[";
        for (size_t i = 0; i < mismatched_indices.size(); ++i) {
            if (i > 0) {
                rejected_indices_str += ", ";
                rejected_counts_str += ", ";
            }
            rejected_indices_str += std::to_string(mismatched_indices[i]);
            rejected_counts_str += std::to_string(mismatched_counts[i]);
        }
        rejected_indices_str += "]";
        rejected_counts_str += "]";
        
        ORC_LOG_WARN("StackerStage: Field {} - {} audio sources rejected {} with {} samples (expected {})",
                    field_id.value(), mismatched_indices.size(), rejected_indices_str, rejected_counts_str, expected_sample_count);
        
        // Use the best source's audio when sample counts don't match
        if (best_source_index < sources.size() && 
            sources[best_source_index] && 
            sources[best_source_index]->has_audio()) {
            return sources[best_source_index]->get_audio_samples(field_id);
        }
        return all_audio_samples[0];
    }
    
    // Stack audio samples
    // Audio is interleaved stereo (L, R, L, R, ...)
    size_t num_samples_total = all_audio_samples[0].size();  // Total interleaved samples
    std::vector<int16_t> stacked_audio(num_samples_total);
    
    // Build list of sources being used
    std::string sources_str = "[";
    for (size_t i = 0; i < source_indices.size(); ++i) {
        if (i > 0) sources_str += ", ";
        sources_str += std::to_string(source_indices[i]);
    }
    sources_str += "]";
    
    // Stack each sample position across all sources
    for (size_t sample_idx = 0; sample_idx < num_samples_total; ++sample_idx) {
        std::vector<int16_t> values;
        values.reserve(all_audio_samples.size());
        
        // Collect value from each source at this sample position
        for (const auto& source_samples : all_audio_samples) {
            if (sample_idx < source_samples.size()) {
                values.push_back(source_samples[sample_idx]);
            }
        }
        
        // Calculate stacked value based on mode
        if (m_audio_stacking_mode == AudioStackingMode::MEAN) {
            stacked_audio[sample_idx] = audio_mean(values);
        } else if (m_audio_stacking_mode == AudioStackingMode::MEDIAN) {
            stacked_audio[sample_idx] = audio_median(values);
        } else {
            // Shouldn't reach here, but fallback to mean
            stacked_audio[sample_idx] = audio_mean(values);
        }
    }
    
    ORC_LOG_DEBUG("StackerStage: Field {} - {} audio sources used for stacking {} ({} samples each) - complete",
                 field_id.value(), all_audio_samples.size(), sources_str, expected_sample_count);
    
    return stacked_audio;
}

int16_t StackerStage::audio_mean(const std::vector<int16_t>& values) const
{
    if (values.empty()) {
        return 0;
    }
    
    int64_t sum = 0;
    for (const auto& val : values) {
        sum += val;
    }
    
    return static_cast<int16_t>(sum / static_cast<int64_t>(values.size()));
}

int16_t StackerStage::audio_median(std::vector<int16_t> values) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t n = values.size();
    
    if (n % 2 == 0) {
        // Even number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        std::nth_element(values.begin(), values.begin() + (n - 1) / 2, values.end());
        return static_cast<int16_t>((static_cast<int32_t>(values[(n - 1) / 2]) + static_cast<int32_t>(values[n / 2])) / 2);
    } else {
        // Odd number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        return values[n / 2];
    }
}

std::vector<uint8_t> StackerStage::stack_efm(
    FieldID field_id,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    size_t best_source_index) const
{
    // If EFM stacking is disabled, use the best source's EFM
    if (m_efm_stacking_mode == EFMStackingMode::DISABLED) {
        if (best_source_index < sources.size() && 
            sources[best_source_index] && 
            sources[best_source_index]->has_efm()) {
            return sources[best_source_index]->get_efm_samples(field_id);
        }
        // Fallback to first source with EFM
        for (const auto& source : sources) {
            if (source && source->has_efm() && source->has_field(field_id)) {
                return source->get_efm_samples(field_id);
            }
        }
        return {};
    }
    
    // Collect EFM t-values from all sources
    std::vector<std::vector<uint8_t>> all_efm_samples;
    std::vector<uint32_t> sample_counts;
    std::vector<size_t> source_indices;
    size_t sources_without_efm = 0;
    size_t sources_without_field = 0;
    
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        if (!source) {
            continue;
        }
        
        if (!source->has_efm()) {
            sources_without_efm++;
            continue;
        }
        
        if (!source->has_field(field_id)) {
            sources_without_field++;
            continue;
        }
        
        auto efm_samples = source->get_efm_samples(field_id);
        uint32_t sample_count = static_cast<uint32_t>(efm_samples.size());
        
        if (sample_count == 0) {
            continue;
        }
        
        all_efm_samples.push_back(std::move(efm_samples));
        sample_counts.push_back(sample_count);
        source_indices.push_back(i);
    }
    
    // If no sources have EFM for this field, return empty
    if (all_efm_samples.empty()) {
        ORC_LOG_DEBUG("StackerStage: Field {} - no sources have EFM data ({} sources without EFM, {} without field)",
                     field_id.value(), sources_without_efm, sources_without_field);
        return {};
    }
    
    // If only one source has EFM, return it directly
    if (all_efm_samples.size() == 1) {
        ORC_LOG_DEBUG("StackerStage: Field {} - only 1 source has EFM, using directly", field_id.value());
        return all_efm_samples[0];
    }
    
    // Verify all sources have the same number of t-values
    uint32_t expected_sample_count = sample_counts[0];
    bool all_match = true;
    for (size_t i = 1; i < sample_counts.size(); ++i) {
        if (sample_counts[i] != expected_sample_count) {
            ORC_LOG_WARN("StackerStage: Field {} - EFM t-value count mismatch: source {} has {}, expected {}",
                        field_id.value(), source_indices[i], sample_counts[i], expected_sample_count);
            all_match = false;
        }
    }
    
    if (!all_match) {
        ORC_LOG_WARN("StackerStage: Field {} - EFM t-value counts don't match, using first source with EFM", field_id.value());
        return all_efm_samples[0];
    }
    
    // Calculate total number of t-values to stack
    size_t num_samples_total = expected_sample_count;
    
    // Allocate output buffer
    std::vector<uint8_t> stacked_efm(num_samples_total);
    
    // Build sources string for logging
    std::string sources_str = "[";
    for (size_t i = 0; i < source_indices.size(); ++i) {
        if (i > 0) sources_str += ", ";
        sources_str += std::to_string(source_indices[i]);
    }
    sources_str += "]";
    
    // Stack each t-value position across all sources
    for (size_t sample_idx = 0; sample_idx < num_samples_total; ++sample_idx) {
        std::vector<uint8_t> values;
        values.reserve(all_efm_samples.size());
        
        // Collect value from each source at this sample position
        for (const auto& source_samples : all_efm_samples) {
            if (sample_idx < source_samples.size()) {
                values.push_back(source_samples[sample_idx]);
            }
        }
        
        // Calculate stacked value based on mode
        if (m_efm_stacking_mode == EFMStackingMode::MEAN) {
            stacked_efm[sample_idx] = efm_mean(values);
        } else if (m_efm_stacking_mode == EFMStackingMode::MEDIAN) {
            stacked_efm[sample_idx] = efm_median(values);
        } else {
            // Shouldn't reach here, but fallback to mean
            stacked_efm[sample_idx] = efm_mean(values);
        }
    }
    
    ORC_LOG_DEBUG("StackerStage: Field {} - {} EFM sources used for stacking {} ({} t-values each) - complete",
                 field_id.value(), all_efm_samples.size(), sources_str, expected_sample_count);
    
    return stacked_efm;
}

uint8_t StackerStage::efm_mean(const std::vector<uint8_t>& values) const
{
    if (values.empty()) {
        return 0;
    }
    
    uint32_t sum = 0;
    for (const auto& val : values) {
        sum += val;
    }
    
    return static_cast<uint8_t>(sum / static_cast<uint32_t>(values.size()));
}

uint8_t StackerStage::efm_median(std::vector<uint8_t> values) const
{
    if (values.empty()) {
        return 0;
    }
    
    const size_t n = values.size();
    
    if (n % 2 == 0) {
        // Even number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        std::nth_element(values.begin(), values.begin() + (n - 1) / 2, values.end());
        return static_cast<uint8_t>((static_cast<uint16_t>(values[(n - 1) / 2]) + static_cast<uint16_t>(values[n / 2])) / 2);
    } else {
        // Odd number of elements
        std::nth_element(values.begin(), values.begin() + n / 2, values.end());
        return values[n / 2];
    }
}

} // namespace orc
