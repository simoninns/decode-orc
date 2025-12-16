/******************************************************************************
 * artifact.h
 *
 * Artifact identity, provenance, and base class for processing results
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <map>

namespace orc {

/**
 * @brief Unique identifier for artifacts
 * 
 * Artifacts are immutable processing results. The ID is computed from:
 * - Input artifact IDs
 * - Stage type and parameters
 * - Algorithm version
 */
class ArtifactID {
public:
    using value_type = std::string;  // Content-addressed hash (e.g., SHA256)
    
    ArtifactID() = default;
    explicit ArtifactID(value_type id) : id_(std::move(id)) {}
    
    const value_type& value() const { return id_; }
    bool is_valid() const { return !id_.empty(); }
    
    bool operator==(const ArtifactID& other) const { return id_ == other.id_; }
    bool operator!=(const ArtifactID& other) const { return id_ != other.id_; }
    bool operator<(const ArtifactID& other) const { return id_ < other.id_; }
    
    std::string to_string() const { return id_; }
    
private:
    value_type id_;
};

/**
 * @brief Provenance information for an artifact
 * 
 * Records how an artifact was created, enabling reproducibility
 * and dependency tracking.
 */
struct Provenance {
    // Creation metadata
    std::string stage_name;              // e.g., "dropout_correction"
    std::string stage_version;           // Algorithm version
    std::map<std::string, std::string> parameters;  // Stage parameters
    
    // Input artifacts
    std::vector<ArtifactID> input_artifacts;
    
    // Temporal metadata
    std::chrono::system_clock::time_point created_at;
    
    // Optional: execution environment
    std::string hostname;
    std::string user;
    
    // Optional: execution statistics
    std::map<std::string, double> statistics;  // e.g., processing time, memory
};

/**
 * @brief Base class for all artifacts
 * 
 * Artifacts are immutable results of processing stages.
 * They carry identity and provenance information.
 */
class Artifact {
public:
    virtual ~Artifact() = default;
    
    // Identity
    const ArtifactID& id() const { return id_; }
    
    // Provenance
    const Provenance& provenance() const { return provenance_; }
    
    // Type information (RTTI alternative for serialization)
    virtual std::string type_name() const = 0;
    
protected:
    Artifact(ArtifactID id, Provenance prov)
        : id_(std::move(id)), provenance_(std::move(prov)) {}
    
private:
    ArtifactID id_;
    Provenance provenance_;
};

using ArtifactPtr = std::shared_ptr<Artifact>;

} // namespace orc

// Hash support for std::unordered_map
namespace std {
    template<>
    struct hash<orc::ArtifactID> {
        size_t operator()(const orc::ArtifactID& id) const {
            return hash<string>{}(id.value());
        }
    };
}
