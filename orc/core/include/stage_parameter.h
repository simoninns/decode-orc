// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#pragma once

#include <string>
#include <variant>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>

namespace orc {

/// Parameter value types supported by stages
using ParameterValue = std::variant<
    int32_t,      // Integer values
    uint32_t,     // Unsigned integer values
    double,       // Floating point values
    bool,         // Boolean flags
    std::string   // String values
>;

/// Type of parameter
enum class ParameterType {
    INT32,
    UINT32,
    DOUBLE,
    BOOL,
    STRING
};

/// Parameter constraints
struct ParameterConstraints {
    // For numeric types
    std::optional<ParameterValue> min_value;
    std::optional<ParameterValue> max_value;
    std::optional<ParameterValue> default_value;
    
    // For string types (allowed values)
    std::vector<std::string> allowed_strings;
    
    // Whether parameter is required
    bool required = false;
};

/// Description of a stage parameter
struct ParameterDescriptor {
    std::string name;                 // Parameter internal name (e.g., "overcorrect_extension")
    std::string display_name;         // Human-readable name (e.g., "Overcorrect Extension")
    std::string description;          // Detailed description of what parameter does
    ParameterType type;               // Parameter value type
    ParameterConstraints constraints; // Value constraints and defaults
};

/// Interface for stages that expose configurable parameters
class ParameterizedStage {
public:
    virtual ~ParameterizedStage() = default;
    
    /// Get list of parameters this stage supports
    virtual std::vector<ParameterDescriptor> get_parameter_descriptors() const = 0;
    
    /// Get current parameter values
    virtual std::map<std::string, ParameterValue> get_parameters() const = 0;
    
    /// Set parameter values
    /// Returns true if all parameters were valid and set successfully
    virtual bool set_parameters(const std::map<std::string, ParameterValue>& params) = 0;
};

/// Helper functions to work with parameter values
namespace parameter_util {
    /// Convert ParameterValue to string for display
    std::string value_to_string(const ParameterValue& value);
    
    /// Convert string to ParameterValue based on type
    std::optional<ParameterValue> string_to_value(const std::string& str, ParameterType type);
    
    /// Get type name as string
    const char* type_name(ParameterType type);
}

} // namespace orc
