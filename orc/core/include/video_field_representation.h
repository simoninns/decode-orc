#pragma once

#include "field_id.h"
#include "artifact.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <memory>

namespace orc {

/**
 * @brief Field parity (interlacing information)
 */
enum class FieldParity {
    Top,     // Top field (odd lines in progressive numbering)
    Bottom   // Bottom field (even lines)
};

/**
 * @brief Video standard/format
 */
enum class VideoFormat {
    NTSC,
    PAL,
    Unknown
};

/**
 * @brief Descriptor for a single video field
 */
struct FieldDescriptor {
    FieldID field_id;
    FieldParity parity;
    VideoFormat format;
    size_t width;          // Samples per line
    size_t height;         // Number of lines
    
    // Optional: timing information from VBI if available
    std::optional<int32_t> frame_number;
    std::optional<uint32_t> timecode;
};

/**
 * @brief Abstract interface for accessing video field samples
 * 
 * A Video Field Representation provides read-only access to field samples.
 * Concrete implementations may be:
 * - Raw TBC fields
 * - Dropout-corrected fields
 * - Stacked or filtered fields
 * 
 * All sample data is immutable from the client perspective.
 */
class VideoFieldRepresentation : public Artifact {
public:
    using sample_type = uint16_t;  // 16-bit samples (standard for TBC data)
    
    virtual ~VideoFieldRepresentation() = default;
    
    // Sequence information
    virtual FieldIDRange field_range() const = 0;
    virtual size_t field_count() const = 0;
    virtual bool has_field(FieldID id) const = 0;
    
    // Field metadata
    virtual std::optional<FieldDescriptor> get_descriptor(FieldID id) const = 0;
    
    // Sample access (read-only)
    // Returns pointer to line data, or nullptr if field/line not available
    // Lifetime: pointer valid until next call to get_line or object destruction
    virtual const sample_type* get_line(FieldID id, size_t line) const = 0;
    
    // Bulk access (returns copy)
    virtual std::vector<sample_type> get_field(FieldID id) const = 0;
    
    // Type information
    std::string type_name() const override { return "VideoFieldRepresentation"; }
    
protected:
    VideoFieldRepresentation(ArtifactID id, Provenance prov)
        : Artifact(std::move(id), std::move(prov)) {}
};

using VideoFieldRepresentationPtr = std::shared_ptr<VideoFieldRepresentation>;

} // namespace orc
