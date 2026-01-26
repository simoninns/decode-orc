/*
 * File:        analysis_presenter.h
 * Module:      orc-presenters
 * Purpose:     Analysis data presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <node_id.h>
#include <field_id.h>
#include <orc_analysis.h>  // Public API analysis types

// Forward declare core Project type
namespace orc {
    class Project;
    class AnalysisTool;
}

namespace orc::presenters {

// Forward declarations
class Project;

/**
 * @brief Analysis type enumeration
 */
enum class AnalysisType {
    SNR,            ///< Signal-to-noise ratio
    Dropout,        ///< Dropout detection
    Burst,          ///< Burst analysis
    Quality,        ///< General quality metrics
    ChromaNR        ///< Chroma noise reduction
};

/**
 * @brief SNR analysis data for a field
 */
struct SNRFieldData {
    FieldID field_id;
    double snr_db;              ///< SNR in decibels
    double signal_power;        ///< Signal power
    double noise_power;         ///< Noise power
    bool is_valid;              ///< Whether measurement is valid
};

/**
 * @brief SNR analysis results
 */
struct SNRAnalysisData {
    std::vector<SNRFieldData> fields;
    double average_snr;         ///< Average SNR across all fields
    double min_snr;             ///< Minimum SNR
    double max_snr;             ///< Maximum SNR
    int total_fields;           ///< Total fields analyzed
};

/**
 * @brief Dropout detection data for a field
 */
struct DropoutFieldData {
    FieldID field_id;
    int dropout_count;          ///< Number of dropouts detected
    int total_pixels;           ///< Total pixels in field
    double dropout_percentage;  ///< Percentage of pixels affected
    std::vector<int> dropout_lines; ///< Line numbers with dropouts
};

/**
 * @brief Dropout analysis results
 */
struct DropoutAnalysisData {
    std::vector<DropoutFieldData> fields;
    int total_dropouts;         ///< Total dropouts across all fields
    double average_percentage;  ///< Average dropout percentage
    int worst_field;            ///< Field with most dropouts
};

/**
 * @brief Burst analysis data for a field
 */
struct BurstFieldData {
    FieldID field_id;
    double burst_amplitude;     ///< Burst amplitude
    double burst_phase;         ///< Burst phase
    bool burst_present;         ///< Whether burst is present
};

/**
 * @brief Burst analysis results
 */
struct BurstAnalysisData {
    std::vector<BurstFieldData> fields;
    double average_amplitude;   ///< Average burst amplitude
    int fields_with_burst;      ///< Count of fields with valid burst
};

/**
 * @brief Quality metrics for a field
 */
struct QualityFieldData {
    FieldID field_id;
    double overall_score;       ///< Overall quality score (0-100)
    double sharpness;           ///< Sharpness metric
    double contrast;            ///< Contrast metric
    double stability;           ///< Temporal stability
};

/**
 * @brief Quality analysis results
 */
struct QualityAnalysisData {
    std::vector<QualityFieldData> fields;
    double average_score;       ///< Average quality score
};

/**
 * @brief Progress callback for analysis operations
 */
using AnalysisProgressCallback = std::function<void(size_t current, size_t total, const std::string& status)>;

/**
 * @brief AnalysisPresenter - Manages analysis data access and operations
 * 
 * This presenter extracts analysis logic from the GUI layer.
 * It provides a clean interface for:
 * - Running various analysis types on nodes
 * - Retrieving analysis results
 * - Managing analysis parameters
 * - Progress tracking for long-running analysis
 * 
 * The presenter coordinates between the core analysis system
 * and the GUI's data visualization needs.
 */
class AnalysisPresenter {
public:
    /**
     * @brief Construct presenter for a project
     * @param project_handle Opaque handle to project
     */
    explicit AnalysisPresenter(void* project_handle);
    
    /**
     * @brief Destructor
     */
    ~AnalysisPresenter();
    
    // Disable copy, enable move
    AnalysisPresenter(const AnalysisPresenter&) = delete;
    AnalysisPresenter& operator=(const AnalysisPresenter&) = delete;
    AnalysisPresenter(AnalysisPresenter&&) noexcept;
    AnalysisPresenter& operator=(AnalysisPresenter&&) noexcept;
    
    // === Analysis Execution ===
    
    /**
     * @brief Run SNR analysis on a node
     * @param node_id Node to analyze
     * @param progress_callback Optional progress callback
     * @return true on success
     */
    bool runSNRAnalysis(NodeID node_id, AnalysisProgressCallback progress_callback = nullptr);
    
    /**
     * @brief Run dropout analysis on a node
     * @param node_id Node to analyze
     * @param progress_callback Optional progress callback
     * @return true on success
     */
    bool runDropoutAnalysis(NodeID node_id, AnalysisProgressCallback progress_callback = nullptr);
    
    /**
     * @brief Run burst analysis on a node
     * @param node_id Node to analyze
     * @param progress_callback Optional progress callback
     * @return true on success
     */
    bool runBurstAnalysis(NodeID node_id, AnalysisProgressCallback progress_callback = nullptr);
    
    /**
     * @brief Run quality metrics analysis on a node
     * @param node_id Node to analyze
     * @param progress_callback Optional progress callback
     * @return true on success
     */
    bool runQualityAnalysis(NodeID node_id, AnalysisProgressCallback progress_callback = nullptr);
    
    /**
     * @brief Cancel ongoing analysis
     */
    void cancelAnalysis();
    
    /**
     * @brief Check if analysis is running
     */
    bool isAnalysisRunning() const;
    
    // === Data Retrieval ===
    
    /**
     * @brief Get SNR analysis results
     * @param node_id Node to get results for
     * @return SNR data (empty if not available)
     */
    SNRAnalysisData getSNRAnalysis(NodeID node_id) const;
    
    /**
     * @brief Get dropout analysis results
     * @param node_id Node to get results for
     * @return Dropout data (empty if not available)
     */
    DropoutAnalysisData getDropoutAnalysis(NodeID node_id) const;
    
    /**
     * @brief Get burst analysis results
     * @param node_id Node to get results for
     * @return Burst data (empty if not available)
     */
    BurstAnalysisData getBurstAnalysis(NodeID node_id) const;
    
    /**
     * @brief Get quality metrics results
     * @param node_id Node to get results for
     * @return Quality data (empty if not available)
     */
    QualityAnalysisData getQualityAnalysis(NodeID node_id) const;
    
    /**
     * @brief Check if analysis data is available for a node
     * @param node_id Node to check
     * @param type Analysis type
     * @return true if data is available
     */
    bool hasAnalysisData(NodeID node_id, AnalysisType type) const;
    
    // === Analysis Parameters ===
    
    /**
     * @brief Set analysis parameters
     * @param node_id Node to configure
     * @param type Analysis type
     * @param parameters Parameter map
     */
    void setAnalysisParameters(NodeID node_id, AnalysisType type, 
                              const std::map<std::string, std::string>& parameters);
    
    /**
     * @brief Get current analysis parameters
     * @param node_id Node to query
     * @param type Analysis type
     * @return Parameter map
     */
    std::map<std::string, std::string> getAnalysisParameters(NodeID node_id, AnalysisType type) const;
    
    // === Data Export ===
    
    /**
     * @brief Export analysis results to CSV
     * @param node_id Node to export from
     * @param type Analysis type
     * @param output_path Output file path
     * @return true on success
     */
    bool exportToCSV(NodeID node_id, AnalysisType type, const std::string& output_path) const;
    
    // === Analysis Tool Registry (Phase 2.4) ===
    
    /**
     * @brief Get all available analysis tools
     * @return Vector of all registered analysis tools
     */
    std::vector<orc::AnalysisToolInfo> getAvailableTools() const;
    
    /**
     * @brief Get analysis tools applicable to a specific stage type
     * @param stage_name Name of the stage type (e.g., "field_map", "PAL_Comp_Source")
     * @return Vector of applicable tools, sorted by priority
     */
    std::vector<orc::AnalysisToolInfo> getToolsForStage(const std::string& stage_name) const;
    
    /**
     * @brief Get information about a specific tool
     * @param tool_id Unique tool identifier
     * @return Tool info (empty name if not found)
     */
    orc::AnalysisToolInfo getToolInfo(const std::string& tool_id) const;
    
    // === Generic Analysis Execution (Phase 2.8) ===
    
    /**
     * @brief Get parameter descriptors for a specific analysis tool
     * @param tool_id Unique tool identifier
     * @param source_type Type of source being analyzed
     * @return Vector of parameter descriptors for this tool
     */
    std::vector<orc::ParameterDescriptor> getToolParameters(
        const std::string& tool_id,
        orc::AnalysisSourceType source_type
    ) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
