/*
 * File:        hints_presenter.h
 * Module:      orc-presenters
 * Purpose:     Hints management presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <unordered_map>
#include <node_id.h>
#include <field_id.h>
#include "hints_view_models.h"

// Forward declare core Project type
namespace orc {
    class Project;
    class DAG;
}

namespace orc::presenters {

// Forward declarations
class Project;

/**
 * @brief Hint type enumeration
 */
enum class HintType {
    ActiveLine,     ///< Active line region hint
    VBI,            ///< VBI data hint
    Dropout,        ///< Dropout correction hint
    Burst,          ///< Color burst hint
    Custom          ///< Custom hint type
};

/**
 * @brief Active line hint data
 */
struct ActiveLineHint {
    int first_line;     ///< First active line
    int last_line;      ///< Last active line
    bool enabled;       ///< Whether hint is enabled
};

/**
 * @brief VBI hint data
 */
struct VBIHint {
    std::vector<int> lines;     ///< VBI line numbers
    std::string data_type;      ///< Type of VBI data (teletext, closed caption, etc)
    bool enabled;               ///< Whether hint is enabled
};

/**
 * @brief Dropout hint data
 */
struct DropoutHint {
    FieldID field_id;           ///< Field this hint applies to
    int line_start;             ///< Start line of dropout
    int line_end;               ///< End line of dropout
    int pixel_start;            ///< Start pixel
    int pixel_end;              ///< End pixel
    std::string correction_method;  ///< Correction method to use
    bool enabled;               ///< Whether hint is enabled
};

/**
 * @brief Burst hint data
 */
struct BurstHint {
    int burst_start;            ///< Burst start position
    int burst_length;           ///< Burst length
    bool enabled;               ///< Whether hint is enabled
};

/**
 * @brief Generic hint container
 */
struct Hint {
    int id;                     ///< Unique hint ID
    HintType type;              ///< Hint type
    std::string description;    ///< User description
    bool enabled{true};         ///< Enabled state
    
    // Type-specific data (only one will be valid based on type)
    ActiveLineHint active_line;
    VBIHint vbi;
    DropoutHint dropout;
    BurstHint burst;
};

/**
 * @brief HintsPresenter - Manages hint data for processing stages
 * 
 * This presenter extracts hint management logic from the GUI layer.
 * It provides a clean interface for:
 * - Adding, editing, and removing hints
 * - Querying hints for specific nodes/fields
 * - Enabling/disabling hints
 * - Validating hint data
 * 
 * Hints provide user guidance to processing stages for better results.
 */
class HintsPresenter {
public:
    /**
     * @brief Construct presenter for a project
     * @param dag_provider Callback that returns opaque DAG handle
     */
    explicit HintsPresenter(std::function<std::shared_ptr<void>()> dag_provider);
    
    /**
     * @brief Destructor
     */
    ~HintsPresenter();
    
    // Disable copy, enable move
    HintsPresenter(const HintsPresenter&) = delete;
    HintsPresenter& operator=(const HintsPresenter&) = delete;
    HintsPresenter(HintsPresenter&&) noexcept;
    HintsPresenter& operator=(HintsPresenter&&) noexcept;

    struct FieldHintsView {
        std::optional<FieldParityHintView> parity;
        std::optional<FieldPhaseHintView> phase;
        std::optional<ActiveLineHintView> active_line;
        std::optional<VideoParametersView> video_params;
    };

    /**
     * @brief Fetch all available hints for a node/field
     */
    FieldHintsView getHintsForField(NodeID node_id, FieldID field_id) const;
    
    // === Hint Management ===
    
    /**
     * @brief Add an active line hint
     * @param node_id Node to add hint to
     * @param hint Hint data
     * @return Hint ID
     */
    int addActiveLineHint(NodeID node_id, const ActiveLineHint& hint);
    
    /**
     * @brief Add a VBI hint
     * @param node_id Node to add hint to
     * @param hint Hint data
     * @return Hint ID
     */
    int addVBIHint(NodeID node_id, const VBIHint& hint);
    
    /**
     * @brief Add a dropout hint
     * @param node_id Node to add hint to
     * @param hint Hint data
     * @return Hint ID
     */
    int addDropoutHint(NodeID node_id, const DropoutHint& hint);
    
    /**
     * @brief Add a burst hint
     * @param node_id Node to add hint to
     * @param hint Hint data
     * @return Hint ID
     */
    int addBurstHint(NodeID node_id, const BurstHint& hint);
    
    /**
     * @brief Update an existing hint
     * @param hint_id Hint to update
     * @param hint New hint data
     * @return true on success
     */
    bool updateHint(int hint_id, const Hint& hint);
    
    /**
     * @brief Remove a hint
     * @param hint_id Hint to remove
     * @return true on success
     */
    bool removeHint(int hint_id);
    
    /**
     * @brief Enable or disable a hint
     * @param hint_id Hint to modify
     * @param enabled Enable state
     */
    void setHintEnabled(int hint_id, bool enabled);
    
    // === Hint Queries ===
    
    /**
     * @brief Get all hints for a node
     * @param node_id Node to query
     * @return List of hints
     */
    std::vector<Hint> getHints(NodeID node_id) const;
    
    /**
     * @brief Get hints of a specific type for a node
     * @param node_id Node to query
     * @param type Hint type to filter by
     * @return List of hints
     */
    std::vector<Hint> getHintsByType(NodeID node_id, HintType type) const;
    
    /**
     * @brief Get a specific hint by ID
     * @param hint_id Hint ID
     * @return Hint data (throws if not found)
     */
    Hint getHint(int hint_id) const;
    
    /**
     * @brief Check if a node has any hints
     * @param node_id Node to check
     * @return true if hints exist
     */
    bool hasHints(NodeID node_id) const;
    
    /**
     * @brief Get dropout hints for a specific field
     * @param node_id Node to query
     * @param field_id Field to filter by
     * @return List of dropout hints
     */
    std::vector<DropoutHint> getDropoutHintsForField(NodeID node_id, FieldID field_id) const;
    
    // === Validation ===
    
    /**
     * @brief Validate a hint
     * @param hint Hint to validate
     * @param error_message Output error message if invalid
     * @return true if valid
     */
    bool validateHint(const Hint& hint, std::string* error_message = nullptr) const;
    
    // === Bulk Operations ===
    
    /**
     * @brief Remove all hints for a node
     * @param node_id Node to clear hints from
     */
    void clearHints(NodeID node_id);
    
    /**
     * @brief Import hints from file
     * @param node_id Node to import hints for
     * @param file_path Path to hints file
     * @return true on success
     */
    bool importHints(NodeID node_id, const std::string& file_path);
    
    /**
     * @brief Export hints to file
     * @param node_id Node to export hints from
     * @param file_path Path to save to
     * @return true on success
     */
    bool exportHints(NodeID node_id, const std::string& file_path) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
