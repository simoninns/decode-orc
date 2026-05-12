/*
 * File:        stage_registry.h
 * Module:      orc-core
 * Purpose:     Stage type registration
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

// =============================================================================
// MVP Architecture Enforcement
// =============================================================================
// This header is part of the CORE internal implementation.
// GUI/CLI code must NOT include this header directly.
// Stage registry access is provided through ProjectPresenter methods.
// =============================================================================
#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/include/stage_registry.h. Use ProjectPresenter for stage registry access."
#endif
#if defined(ORC_CLI_BUILD)
#error "CLI code cannot include core/include/stage_registry.h. Use ProjectPresenter for stage registry access."
#endif

#include "dag_executor.h"
#include "stage_plugin_loader.h"
#include "stage_plugin_registry.h"
#include <memory>
#include <string>
#include <functional>
#include <map>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace orc {

/**
 * @brief Exception thrown when stage cannot be created
 */
class StageRegistryError : public std::runtime_error {
public:
    explicit StageRegistryError(const std::string& msg) : std::runtime_error(msg) {}
};

enum class StagePluginDiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct StagePluginDiagnostic {
    StagePluginDiagnosticSeverity severity = StagePluginDiagnosticSeverity::Info;
    std::string path;
    std::string message;
};

/**
 * @brief Factory for creating DAG stages by name
 * 
 * The registry maps stage names (strings) to factory functions that create
 * stage instances. This enables converting serialized Projects into executable
 * DAGs.
 * 
 * Usage:
 * ```cpp
 * auto& registry = StageRegistry::instance();
 * auto stage = registry.create_stage("dropout_correct");
 * ```
 * 
 * Thread safety: Not thread-safe. Register stages during initialization only.
 */
class StageRegistry {
public:
    using StageFactory = std::function<DAGStagePtr()>;
    
    /**
     * @brief Get singleton instance
     */
    static StageRegistry& instance();

    /**
        * @brief Ensure runtime plugin stages are initialized
     */
    void initialize();
    
    /**
     * @brief Register a stage factory
     * 
     * @param stage_name Unique name for this stage (e.g., "dropout_correct")
     * @param factory Function that creates a new stage instance
     * @throws StageRegistryError if stage_name already registered
     */
    void register_stage(const std::string& stage_name, StageFactory factory);
    
    /**
     * @brief Create a stage instance by name
     * 
     * @param stage_name Name of the stage to create
     * @return Newly created stage instance
     * @throws StageRegistryError if stage_name not found
     */
    DAGStagePtr create_stage(const std::string& stage_name) const;
    
    /**
     * @brief Check if a stage is registered
     * 
     * @param stage_name Name to check
     * @return True if stage can be created
     */
    bool has_stage(const std::string& stage_name) const;
    
    /**
     * @brief Get list of all registered stage names
     * 
     * @return Vector of stage names
     */
    std::vector<std::string> get_registered_stages() const;

    /**
     * @brief Get successfully loaded stage plugins
     */
    const std::vector<LoadedStagePlugin>& get_loaded_plugins() const;

    /**
     * @brief Get plugin discovery and load diagnostics
     */
    const std::vector<StagePluginDiagnostic>& get_plugin_diagnostics() const;

    /**
     * @brief Get configured plugin search paths used during initialization
     */
    const std::vector<std::string>& get_plugin_search_paths() const;

    /**
     * @brief Get plugin registry path used during initialization
     */
    const std::string& get_plugin_registry_path() const;

    /**
     * @brief Get parsed plugin registry entries used during initialization
     */
    const std::vector<StagePluginRegistryEntry>& get_plugin_registry_entries() const;
    
    /**
     * @brief Get default transform stage name
     * 
     * Returns a simple, neutral stage suitable as a default when
     * adding new nodes. This stage can be changed by the user afterward.
     * 
     * @return Stage name for default transform (currently "passthrough")
     */
    static std::string get_default_transform_stage();
    
    /**
     * @brief Clear all registered stages (primarily for testing)
     */
    void clear();
    
private:
    StageRegistry() = default;
    StageRegistry(const StageRegistry&) = delete;
    StageRegistry& operator=(const StageRegistry&) = delete;

    void initialize_runtime_plugins();
    void add_plugin_diagnostic(StagePluginDiagnosticSeverity severity, const std::string& path, const std::string& message);
    
    std::map<std::string, StageFactory> factories_;
    StagePluginLoader plugin_loader_;
    std::vector<StagePluginDiagnostic> plugin_diagnostics_;
    std::vector<std::string> plugin_search_paths_;
    std::string plugin_registry_path_;
    std::vector<StagePluginRegistryEntry> plugin_registry_entries_;
    bool initialized_ = false;
};

} // namespace orc
