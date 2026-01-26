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
#include <memory>
#include <string>
#include <functional>
#include <map>
#include <stdexcept>
#include <iostream>

namespace orc {

/**
 * @brief Exception thrown when stage cannot be created
 */
class StageRegistryError : public std::runtime_error {
public:
    explicit StageRegistryError(const std::string& msg) : std::runtime_error(msg) {}
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
    
    std::map<std::string, StageFactory> factories_;
};

/**
 * @brief Helper for auto-registering stages
 * 
 * Automatically queries the stage for its name via get_node_type_info(),
 * eliminating duplication and preventing mismatches.
 * 
 * This class is typically used via the ORC_REGISTER_STAGE macro.
 */
class StageRegistration {
public:
    StageRegistration(StageRegistry::StageFactory factory) {
        // Create a temporary instance to get the stage name
        auto temp_stage = factory();
        std::string stage_name = temp_stage->get_node_type_info().stage_name;
        StageRegistry::instance().register_stage(stage_name, factory);
    }
};

} // namespace orc

/**
 * @brief Macro for explicit stage registration
 * 
 * This macro creates a static registration object that automatically registers
 * the stage with the StageRegistry during static initialization. The registration
 * is self-documenting and ensures the stage is available for use.
 * 
 * Usage in stage implementation files:
 * ```cpp
 * ORC_REGISTER_STAGE(DropoutCorrectStage);
 * ```
 * 
 * The macro expands to:
 * - A uniquely-named static StageRegistration object
 * - A lambda factory that creates instances of the stage
 * 
 * This approach:
 * - Makes registration explicit and easy to verify
 * - Prevents forgetting to register new stages
 * - Self-documents which stages are available
 * - Eliminates the need for force-linking workarounds
 */
#define ORC_REGISTER_STAGE(StageClass) \
    namespace { \
        static ::orc::StageRegistration _orc_stage_registration_##StageClass([]() { \
            return std::make_shared<StageClass>(); \
        }); \
    }
