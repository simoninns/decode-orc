/*
 * File:        source_alignment_analysis.cpp
 * Module:      orc-core
 * Purpose:     Source alignment analysis tool implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "source_alignment_analysis.h"
#include "../analysis_registry.h"
#include "../../stages/source_align/source_align_stage.h"
#include "../../observers/biphase_observer.h"
#include "../../include/dag_executor.h"
#include "../../include/project.h"
#include "../../include/logging.h"
#include <algorithm>
#include <set>
#include <sstream>

namespace orc {

// Register the tool
REGISTER_ANALYSIS_TOOL(SourceAlignmentAnalysisTool)

void force_link_SourceAlignmentAnalysisTool() {}

std::string SourceAlignmentAnalysisTool::id() const {
    return "source_alignment";
}

std::string SourceAlignmentAnalysisTool::name() const {
    return "Source Alignment Analysis";
}

std::string SourceAlignmentAnalysisTool::description() const {
    return "Analyzes multiple sources to determine optimal alignment based on VBI frame numbers or CLV timecodes";
}

std::string SourceAlignmentAnalysisTool::category() const {
    return "Source Processing";
}

std::vector<ParameterDescriptor> SourceAlignmentAnalysisTool::parameters() const {
    return {};  // No additional parameters needed
}

bool SourceAlignmentAnalysisTool::canAnalyze(AnalysisSourceType source_type) const {
    // Can analyze laserdisc sources
    return source_type == AnalysisSourceType::LaserDisc;
}

bool SourceAlignmentAnalysisTool::isApplicableToStage(const std::string& stage_name) const {
    // Source alignment analysis is only applicable to source_align stages
    return stage_name == "source_align";
}

/**
 * @brief Get VBI frame number or CLV timecode frame equivalent for a field
 * 
 * ARCHITECTURAL NOTE: This function gets observations from the source stage.
 * It does NOT run observers itself - that's the source stage's responsibility.
 */
static int32_t get_frame_number_from_vbi(
    const VideoFieldRepresentation& source,
    FieldID field_id)
{
    // Get VBI observations from the source (provided by source stage's observers)
    auto observations = source.get_observations(field_id);
    
    for (const auto& obs : observations) {
        auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(obs);
        if (!biphase_obs) {
            continue;
        }
        
        // Check for CAV picture number (preferred)
        if (biphase_obs->picture_number.has_value()) {
            return biphase_obs->picture_number.value();
        }
        
        // Check for CLV timecode
        if (biphase_obs->clv_timecode.has_value()) {
            // Convert CLV timecode to frame number
            const auto& tc = biphase_obs->clv_timecode.value();
            
            // Determine frame rate from video format
            auto params = source.get_video_parameters();
            bool is_pal = params && params->system == VideoSystem::PAL;
            int32_t fps = is_pal ? 25 : 30;
            
            // Convert to total frame number
            int32_t frame_num = tc.hours * 3600 * fps +
                               tc.minutes * 60 * fps +
                               tc.seconds * fps +
                               tc.picture_number;
            return frame_num;
        }
    }
    
    return -1;  // No VBI frame number found
}

AnalysisResult SourceAlignmentAnalysisTool::analyze(const AnalysisContext& ctx,
                                                     AnalysisProgress* progress) {
    AnalysisResult result;
    
    if (progress) {
        progress->setStatus("Initializing source alignment analysis...");
        progress->setProgress(0);
    }
    
    // Get the source_align node's inputs from the DAG
    if (!ctx.dag || !ctx.project) {
        result.status = AnalysisResult::Failed;
        result.summary = "No DAG or project provided for analysis";
        ORC_LOG_ERROR("Source alignment analysis requires DAG and project in context");
        return result;
    }
    
    // Find the source_align node in the DAG
    const auto& dag_nodes = ctx.dag->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&ctx](const DAGNode& node) { return node.node_id == ctx.node_id; });
    
    if (node_it == dag_nodes.end()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Source align node not found in DAG";
        ORC_LOG_ERROR("Node '{}': Not found in DAG", ctx.node_id);
        return result;
    }
    
    // Get all input node IDs
    const auto& input_node_ids = node_it->input_node_ids;
    
    if (input_node_ids.empty()) {
        result.status = AnalysisResult::Failed;
        result.summary = "Source align node has no inputs";
        ORC_LOG_ERROR("Node '{}': No input nodes", ctx.node_id);
        return result;
    }
    
    if (progress) {
        progress->setStatus("Executing DAG to get input sources...");
        progress->setProgress(10);
    }
    
    // Execute the DAG to get all input sources
    DAGExecutor executor;
    std::vector<std::shared_ptr<VideoFieldRepresentation>> input_sources;
    
    try {
        for (size_t i = 0; i < input_node_ids.size(); ++i) {
            const auto& input_node_id = input_node_ids[i];
            auto all_outputs = executor.execute_to_node(*ctx.dag, input_node_id);
            
            // Get the outputs from this input node
            auto output_it = all_outputs.find(input_node_id);
            if (output_it == all_outputs.end() || output_it->second.empty()) {
                result.status = AnalysisResult::Failed;
                result.summary = "Input node " + std::to_string(i + 1) + " produced no outputs";
                ORC_LOG_ERROR("Node '{}': Input node '{}' produced no outputs", ctx.node_id, input_node_id);
                return result;
            }
            
            // Find the VideoFieldRepresentation output
            std::shared_ptr<VideoFieldRepresentation> source;
            for (const auto& artifact : output_it->second) {
                source = std::dynamic_pointer_cast<VideoFieldRepresentation>(artifact);
                if (source) {
                    break;
                }
            }
            
            if (!source) {
                result.status = AnalysisResult::Failed;
                result.summary = "Input node " + std::to_string(i + 1) + " did not produce VideoFieldRepresentation";
                ORC_LOG_ERROR("Node '{}': Input node '{}' did not produce VideoFieldRepresentation", ctx.node_id, input_node_id);
                return result;
            }
            
            // Log artifact ID to verify we're getting different sources
            ORC_LOG_DEBUG("Input {}: node_id='{}', artifact_id='{}', field_count={}, ptr={}", 
                        i + 1, input_node_id, source->id().to_string(), source->field_count(), 
                        static_cast<const void*>(source.get()));
            
            input_sources.push_back(source);
            
            if (progress && progress->isCancelled()) {
                result.status = AnalysisResult::Cancelled;
                return result;
            }
        }
        
        ORC_LOG_DEBUG("Got {} input sources for alignment analysis", input_sources.size());
        
        // Check if all sources are the same object (pointer equality)
        if (input_sources.size() > 1) {
            bool all_same_ptr = true;
            const void* first_ptr = input_sources[0].get();
            for (size_t i = 1; i < input_sources.size(); ++i) {
                if (input_sources[i].get() != first_ptr) {
                    all_same_ptr = false;
                    break;
                }
            }
            
            if (all_same_ptr) {
                result.status = AnalysisResult::Failed;
                result.summary = "ERROR: All " + std::to_string(input_sources.size()) + 
                                " inputs are the SAME source (artifact_id: " + input_sources[0]->id().to_string() + ")";
                
                AnalysisResult::ResultItem error_item;
                error_item.type = "error";
                error_item.message = "All inputs to the source_align node point to the same source object. "
                                    "This indicates a configuration problem:\n\n"
                                    "• Each input should come from a DIFFERENT source (different TBC captures)\n"
                                    "• Check that your upstream nodes (field_map stages) are connected to different sources\n"
                                    "• The source_align stage is meant to align multiple captures of the same disc,\n"
                                    "  not the same capture duplicated multiple times\n\n"
                                    "All inputs have artifact_id: " + input_sources[0]->id().to_string();
                result.items.push_back(error_item);
                
                ORC_LOG_ERROR("All {} inputs are the same object - this is a configuration error!", input_sources.size());
                return result;
            }
        }
        
        if (progress) {
            progress->setStatus("Finding first common VBI frame across sources...");
            progress->setProgress(30);
        }
        
        // Structure to track VBI frames found in each source
        struct SourceVBIInfo {
            FieldIDRange range;
            std::set<int32_t> vbi_frames;  // VBI frames found so far
            std::map<int32_t, FieldID> frame_to_field;  // Map VBI frame to field_id
            int32_t first_vbi = -1;
            int32_t last_vbi = -1;
            size_t total_vbi_count = 0;  // Total VBI frames found during full scan
            bool fully_scanned = false;
        };
        
        std::vector<SourceVBIInfo> source_info(input_sources.size());
        
        // Phase 1: Quick scan - find the first few VBI frames from each source
        // We'll scan up to MAX_SCAN_FIELDS per source to find initial VBI frames
        constexpr size_t MAX_SCAN_FIELDS = 1000;  // Usually enough to find VBI data
        
        ORC_LOG_DEBUG("Phase 1: Quick scan for initial VBI frames (up to {} fields per source)", MAX_SCAN_FIELDS);
        
        for (size_t src_idx = 0; src_idx < input_sources.size(); ++src_idx) {
            const auto& source = input_sources[src_idx];
            if (!source) {
                continue;
            }
            
            auto& info = source_info[src_idx];
            info.range = source->field_range();
            
            ORC_LOG_DEBUG("  Source {}: quick scan (range {}-{})",
                         src_idx + 1, info.range.start.value(), info.range.end.value() - 1);
            
            size_t scanned = 0;
            for (FieldID field_id = info.range.start; field_id < info.range.end && scanned < MAX_SCAN_FIELDS; ++field_id) {
                if (!source->has_field(field_id)) {
                    continue;
                }
                
                scanned++;
                int32_t frame_num = get_frame_number_from_vbi(*source, field_id);
                if (frame_num >= 0) {
                    info.vbi_frames.insert(frame_num);
                    info.frame_to_field[frame_num] = field_id;
                    
                    if (info.first_vbi < 0) {
                        info.first_vbi = frame_num;
                        ORC_LOG_DEBUG("    Source {}: first VBI frame {} found at field_id {}", 
                                     src_idx + 1, frame_num, field_id.value());
                    }
                    info.last_vbi = frame_num;
                }
            }
            
            ORC_LOG_DEBUG("    Source {}: found {} unique VBI frames in first {} fields", 
                         src_idx + 1, info.vbi_frames.size(), scanned);
            
            if (progress && progress->isCancelled()) {
                result.status = AnalysisResult::Cancelled;
                return result;
            }
        }
        
        if (progress) {
            progress->setProgress(50);
        }
        
        // Phase 2: Find first common VBI frame from the quick scan
        // Build intersection of VBI frames found so far
        std::set<int32_t> common_frames;
        bool first_source = true;
        
        for (size_t src_idx = 0; src_idx < source_info.size(); ++src_idx) {
            const auto& info = source_info[src_idx];
            
            if (info.vbi_frames.empty()) {
                ORC_LOG_WARN("Source {} has no VBI frames in quick scan", src_idx + 1);
                common_frames.clear();
                break;
            }
            
            if (first_source) {
                common_frames = info.vbi_frames;
                first_source = false;
            } else {
                std::set<int32_t> intersection;
                std::set_intersection(
                    common_frames.begin(), common_frames.end(),
                    info.vbi_frames.begin(), info.vbi_frames.end(),
                    std::inserter(intersection, intersection.begin())
                );
                common_frames = std::move(intersection);
            }
            
            if (common_frames.empty()) {
                ORC_LOG_WARN("No VBI frame overlap found between sources up to source {}", src_idx + 1);
                break;
            }
        }
        
        int32_t first_common_frame = -1;
        if (!common_frames.empty()) {
            // Use the earliest common frame found
            first_common_frame = *common_frames.begin();
            ORC_LOG_DEBUG("Found first common VBI frame {} in quick scan (appears in all {} sources)", 
                        first_common_frame, input_sources.size());
        } else {
            // Need to do a full scan - sources might have non-overlapping VBI frames in the quick scan
            ORC_LOG_WARN("No common VBI frame in quick scan - will need full scan to find best alignment");
        }
        
        if (progress) {
            progress->setProgress(60);
        }
        
        // Phase 3: If we didn't find a common frame, do a full scan to gather all VBI data
        // This is the fallback - only happens if sources have very sparse or non-overlapping VBI in first 1000 fields
        if (first_common_frame < 0) {
            ORC_LOG_DEBUG("Phase 3: Full scan to find best alignment");
            
            for (size_t src_idx = 0; src_idx < input_sources.size(); ++src_idx) {
                const auto& source = input_sources[src_idx];
                if (!source) {
                    continue;
                }
                
                auto& info = source_info[src_idx];
                
                ORC_LOG_DEBUG("  Source {}: full scan of {} fields", src_idx + 1, source->field_count());
                
                for (FieldID field_id = info.range.start; field_id < info.range.end; ++field_id) {
                    if (!source->has_field(field_id)) {
                        continue;
                    }
                    
                    int32_t frame_num = get_frame_number_from_vbi(*source, field_id);
                    if (frame_num >= 0) {
                        if (info.vbi_frames.find(frame_num) == info.vbi_frames.end()) {
                            info.vbi_frames.insert(frame_num);
                            info.frame_to_field[frame_num] = field_id;
                        }
                        info.total_vbi_count++;
                        info.last_vbi = frame_num;
                    }
                }
                
                info.fully_scanned = true;
                ORC_LOG_DEBUG("    Source {}: found {} unique VBI frames, {} total VBI observations", 
                             src_idx + 1, info.vbi_frames.size(), info.total_vbi_count);
                
                if (progress && progress->isCancelled()) {
                    result.status = AnalysisResult::Cancelled;
                    return result;
                }
            }
        }
        
        if (progress) {
            progress->setStatus("Computing optimal alignment...");
            progress->setProgress(70);
        }
        
        // Phase 4: Determine the best alignment based on available data
        std::vector<FieldID> alignment_offsets(input_sources.size());
        std::vector<size_t> participating_sources;
        size_t max_sources_found = 0;
        
        // If we found a common frame in quick scan, use it
        if (first_common_frame >= 0) {
            // All sources have this frame
            max_sources_found = input_sources.size();
            for (size_t src_idx = 0; src_idx < input_sources.size(); ++src_idx) {
                const auto& info = source_info[src_idx];
                auto it = info.frame_to_field.find(first_common_frame);
                if (it != info.frame_to_field.end()) {
                    alignment_offsets[src_idx] = it->second;
                    participating_sources.push_back(src_idx);
                }
            }
        } else {
            // Full scan was done - find frame that appears in most sources
            std::map<int32_t, std::vector<size_t>> frame_to_sources;
            
            for (size_t src_idx = 0; src_idx < source_info.size(); ++src_idx) {
                const auto& info = source_info[src_idx];
                for (int32_t frame_num : info.vbi_frames) {
                    frame_to_sources[frame_num].push_back(src_idx);
                }
            }
            
            // Find the earliest frame that appears in the most sources
            for (const auto& [frame_num, sources] : frame_to_sources) {
                if (sources.size() > max_sources_found) {
                    max_sources_found = sources.size();
                    first_common_frame = frame_num;
                    
                    participating_sources.clear();
                    for (size_t src_idx : sources) {
                        participating_sources.push_back(src_idx);
                        alignment_offsets[src_idx] = source_info[src_idx].frame_to_field[frame_num];
                    }
                    
                    // If all sources have this frame, we're done
                    if (max_sources_found == input_sources.size()) {
                        break;
                    }
                }
            }
        }
        
        // Prepare summary data
        std::vector<int32_t> first_vbi_frames;
        std::vector<int32_t> last_vbi_frames;
        std::vector<size_t> vbi_counts;
        std::vector<FieldIDRange> source_ranges;
        
        for (const auto& info : source_info) {
            first_vbi_frames.push_back(info.first_vbi);
            last_vbi_frames.push_back(info.last_vbi);
            vbi_counts.push_back(info.fully_scanned ? info.total_vbi_count : info.vbi_frames.size());
            source_ranges.push_back(info.range);
        }
        
        if (first_common_frame < 0 || max_sources_found == 0) {
            result.status = AnalysisResult::Failed;
            result.summary = "No VBI frames found in any sources";
            
            AnalysisResult::ResultItem warning_item;
            warning_item.type = "error";
            warning_item.message = "Could not find any VBI frame numbers in the input sources. "
                                  "This may indicate sources have no VBI data or are corrupted.";
            result.items.push_back(warning_item);
            
            // Add detailed info about each source
            for (size_t i = 0; i < input_sources.size(); ++i) {
                AnalysisResult::ResultItem info_item;
                info_item.type = "info";
                std::ostringstream msg;
                msg << "Source " << (i + 1) << ": "
                    << "fields " << source_ranges[i].start.value() << "-" << source_ranges[i].end.value()
                    << " (" << source_ranges[i].size() << " total), "
                    << vbi_counts[i] << " with VBI";
                if (first_vbi_frames[i] >= 0) {
                    msg << ", VBI frames " << first_vbi_frames[i] << "-" << last_vbi_frames[i];
                }
                info_item.message = msg.str();
                result.items.push_back(info_item);
            }
            
            return result;
        }
        
        // Log the results
        ORC_LOG_DEBUG("  Best common VBI frame {} found in {} of {} sources:", 
                    first_common_frame, max_sources_found, input_sources.size());
        for (size_t src_idx : participating_sources) {
            ORC_LOG_DEBUG("    Source {}: at field_id {} (offset = {})", 
                        src_idx + 1, alignment_offsets[src_idx].value(), alignment_offsets[src_idx].value());
        }
        
        // If not all sources participate, add a warning
        if (max_sources_found < input_sources.size()) {
            std::vector<size_t> excluded_sources;
            for (size_t i = 0; i < input_sources.size(); ++i) {
                if (std::find(participating_sources.begin(), participating_sources.end(), i) == participating_sources.end()) {
                    excluded_sources.push_back(i);
                }
            }
            
            ORC_LOG_WARN("Not all sources have overlapping VBI frames - {} sources excluded", excluded_sources.size());
            for (size_t src_idx : excluded_sources) {
                ORC_LOG_WARN("  Excluded source {}: VBI range {}-{}", 
                            src_idx + 1, first_vbi_frames[src_idx], last_vbi_frames[src_idx]);
            }
            
            AnalysisResult::ResultItem warning_item;
            warning_item.type = "warning";
            std::ostringstream msg;
            msg << "Only " << max_sources_found << " of " << input_sources.size() 
                << " sources have overlapping VBI frames.\n\n";
            msg << "Excluded sources (from different disc sections):\n";
            for (size_t src_idx : excluded_sources) {
                msg << "  • Source " << (src_idx + 1) << ": VBI frames ";
                if (first_vbi_frames[src_idx] >= 0) {
                    msg << first_vbi_frames[src_idx] << "-" << last_vbi_frames[src_idx];
                } else {
                    msg << "none";
                }
                msg << "\n";
            }
            msg << "\nThe alignment map will only include the " << max_sources_found << " overlapping sources.";
            warning_item.message = msg.str();
            result.items.push_back(warning_item);
        }
        
        // Check if all participating sources already start at the same field_id with the same VBI frame
        // This indicates they may have already been aligned by upstream field_map stages
        bool all_start_at_zero = true;
        for (size_t src_idx : participating_sources) {
            if (alignment_offsets[src_idx].value() != 0) {
                all_start_at_zero = false;
                break;
            }
        }
        
        if (all_start_at_zero && participating_sources.size() > 1) {
            ORC_LOG_WARN("All participating sources start at field_id 0 with VBI frame {} - they may have been pre-aligned by field_map stages", 
                        first_common_frame);
        }
        
        if (progress) {
            progress->setStatus("Generating alignment map...");
            progress->setProgress(90);
        }
        
        // Build the alignment map string - only include sources that have the common frame
        std::ostringstream alignment_map;
        bool first = true;
        for (size_t src_idx : participating_sources) {
            if (!first) {
                alignment_map << ", ";
            }
            first = false;
            // Format: input_id+offset (1-indexed input IDs)
            alignment_map << (src_idx + 1) << "+" << alignment_offsets[src_idx].value();
        }
        
        // Build comprehensive summary
        std::ostringstream summary;
        if (max_sources_found < input_sources.size()) {
            summary << "⚠ Partial alignment: " << max_sources_found << " of " << input_sources.size() 
                   << " sources have overlapping VBI frames\n";
            summary << "Alignment based on VBI frame " << first_common_frame << "\n\n";
        } else {
            summary << "Alignment based on VBI frame " << first_common_frame << "\n\n";
        }
        
        summary << "Alignment Map: " << alignment_map.str() << "\n\n";
        
        summary << "Source Details:\n";
        for (size_t i = 0; i < input_sources.size(); ++i) {
            bool is_participating = std::find(participating_sources.begin(), participating_sources.end(), i) 
                                  != participating_sources.end();
            
            summary << "  Source " << (i + 1);
            if (!is_participating) {
                summary << " [EXCLUDED - no overlapping VBI frames]";
            }
            summary << ":\n";
            
            summary << "    Field range: " << source_ranges[i].start.value() << "-" << source_ranges[i].end.value()
                    << " (" << source_ranges[i].size() << " fields)\n";
            
            if (first_vbi_frames[i] >= 0) {
                summary << "    VBI range: frame " << first_vbi_frames[i] << "-" << last_vbi_frames[i]
                        << " (" << vbi_counts[i] << " fields with VBI)\n";
                
                if (is_participating) {
                    // Show where the first common frame appears in this source
                    summary << "    First common VBI frame (" << first_common_frame << ") at field: " 
                            << alignment_offsets[i].value() << "\n";
                    
                    summary << "    Alignment offset: " << alignment_offsets[i].value() << " fields";
                    if (alignment_offsets[i].value() > 0) {
                        summary << " (skip first " << alignment_offsets[i].value() << ")";
                    }
                    summary << "\n";
                    
                    size_t output_fields = source_ranges[i].size() - alignment_offsets[i].value();
                    summary << "    Output: " << output_fields << " fields after alignment\n";
                } else {
                    summary << "    Status: VBI range does not overlap with other sources\n";
                }
            } else {
                summary << "    VBI data: none found\n";
                if (!is_participating) {
                    summary << "    Status: Cannot align without VBI data\n";
                }
            }
            
            if (i < input_sources.size() - 1) {
                summary << "\n";
            }
        }
        
        result.status = AnalysisResult::Success;
        result.summary = summary.str();
        
        // Store the alignment map in the result graphData
        result.graphData["alignmentMap"] = alignment_map.str();
        result.graphData["firstCommonFrame"] = std::to_string(first_common_frame);
        
        // Add statistics
        result.statistics["sourceCount"] = static_cast<long long>(input_sources.size());
        result.statistics["participatingSourceCount"] = static_cast<long long>(max_sources_found);
        result.statistics["excludedSourceCount"] = static_cast<long long>(input_sources.size() - max_sources_found);
        result.statistics["firstCommonVBIFrame"] = static_cast<long long>(first_common_frame);
        
        size_t total_output_fields = 0;
        size_t total_dropped_fields = 0;
        for (size_t src_idx : participating_sources) {
            size_t output_fields = source_ranges[src_idx].size() - alignment_offsets[src_idx].value();
            total_output_fields += output_fields;
            total_dropped_fields += alignment_offsets[src_idx].value();
        }
        result.statistics["totalOutputFields"] = static_cast<long long>(total_output_fields);
        result.statistics["totalDroppedFields"] = static_cast<long long>(total_dropped_fields);
        
        // Add result items for individual sources (these show up in the details view)
        for (size_t i = 0; i < input_sources.size(); ++i) {
            bool is_participating = std::find(participating_sources.begin(), participating_sources.end(), i) 
                                  != participating_sources.end();
            
            AnalysisResult::ResultItem source_item;
            if (is_participating) {
                source_item.type = "info";
                source_item.message = "Source " + std::to_string(i + 1) + 
                                     ": offset +" + std::to_string(alignment_offsets[i].value()) +
                                     " fields, VBI frames " + std::to_string(first_vbi_frames[i]) + 
                                     "-" + std::to_string(last_vbi_frames[i]);
            } else {
                source_item.type = "warning";
                source_item.message = "Source " + std::to_string(i + 1) + " [EXCLUDED]: " +
                                     "VBI frames " + std::to_string(first_vbi_frames[i]) + 
                                     "-" + std::to_string(last_vbi_frames[i]) +
                                     " (no overlap with other sources)";
            }
            result.items.push_back(source_item);
        }
        
        if (progress) {
            progress->setStatus("Analysis complete");
            progress->setProgress(100);
        }
        
    } catch (const std::exception& e) {
        result.status = AnalysisResult::Failed;
        result.summary = "Analysis failed: " + std::string(e.what());
        ORC_LOG_ERROR("Source alignment analysis failed: {}", e.what());
    }
    
    return result;
}

bool SourceAlignmentAnalysisTool::canApplyToGraph() const {
    return true;  // Can apply alignment map to source_align node
}

bool SourceAlignmentAnalysisTool::applyToGraph(const AnalysisResult& result,
                                               Project& project,
                                               NodeID node_id) {
    if (result.status != AnalysisResult::Success) {
        ORC_LOG_ERROR("Cannot apply failed analysis result");
        return false;
    }
    
    // Get the alignment map from the result
    auto it = result.graphData.find("alignmentMap");
    if (it == result.graphData.end()) {
        ORC_LOG_ERROR("Analysis result does not contain alignment map");
        return false;
    }
    
    const std::string& alignment_map = it->second;
    
    // Update the node parameter
    try {
        std::map<std::string, ParameterValue> params;
        params["alignmentMap"] = alignment_map;
        project_io::set_node_parameters(project, node_id, params);
        ORC_LOG_DEBUG("Applied alignment map '{}' to node '{}'", alignment_map, node_id);
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to apply alignment map: {}", e.what());
        return false;
    }
}

int SourceAlignmentAnalysisTool::estimateDurationSeconds(const AnalysisContext& ctx) const {
    (void)ctx;
    // Alignment analysis is relatively fast - just scanning VBI data
    return 10;
}

} // namespace orc
