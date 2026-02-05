/*
 * File:        preview_renderer.cpp
 * Module:      orc-core
 * Purpose:     Preview rendering implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "preview_renderer.h"
#include "previewable_stage.h"
#include "dag_executor.h"
#include "logging.h"
#include <algorithm>
#include <cstdio>
#include <png.h>

namespace orc {

// Helper function to create a placeholder image with text
static PreviewImage create_placeholder_image(PreviewOutputType type, const char* message) {
    PreviewImage placeholder;
    placeholder.width = 1135;
    
    // Height depends on output type
    if (type == PreviewOutputType::Frame || type == PreviewOutputType::Frame_Reversed) {
        // Frame = two fields woven together
        placeholder.height = 313 * 2;  // 626 for PAL frame
    } else {
        // Single field
        placeholder.height = 313;
    }
    
    placeholder.rgb_data.resize(placeholder.width * placeholder.height * 3);
    
    // Fill with black background
    for (size_t i = 0; i < placeholder.rgb_data.size(); ++i) {
        placeholder.rgb_data[i] = 0;  // Black
    }
    
    // Draw message text in white
    // Simple 8x8 bitmap font for the message
    const size_t base_char_width = 8;
    const size_t base_char_height = 8;
    
    // Scale text larger for frame rendering (2x scale)
    const size_t scale = (type == PreviewOutputType::Frame || 
                         type == PreviewOutputType::Frame_Reversed) ? 2 : 1;
    const size_t char_width = base_char_width * scale;
    const size_t char_height = base_char_height * scale;
    const size_t message_len = std::strlen(message);
    const size_t text_width = message_len * char_width;
    
    // Center the text
    size_t text_start_x = (placeholder.width - text_width) / 2;
    size_t text_start_y = (placeholder.height - char_height) / 2;
    
    // Helper function to get character bitmap pattern
    auto get_char_pattern = [](char ch) -> const uint8_t* {
        static const uint8_t N[] = {0x00, 0x82, 0xC2, 0xA2, 0x92, 0x8A, 0x86, 0x00};
        static const uint8_t R[] = {0x00, 0x7C, 0x42, 0x42, 0x7C, 0x48, 0x44, 0x00};
        static const uint8_t o[] = {0x00, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x3C, 0x00};
        static const uint8_t s[] = {0x00, 0x00, 0x3C, 0x40, 0x3C, 0x02, 0x7C, 0x00};
        static const uint8_t u[] = {0x00, 0x00, 0x42, 0x42, 0x42, 0x46, 0x3A, 0x00};
        static const uint8_t r[] = {0x00, 0x00, 0x5C, 0x62, 0x40, 0x40, 0x40, 0x00};
        static const uint8_t c[] = {0x00, 0x00, 0x3C, 0x40, 0x40, 0x40, 0x3C, 0x00};
        static const uint8_t e[] = {0x00, 0x00, 0x3C, 0x42, 0x7E, 0x40, 0x3C, 0x00};
        static const uint8_t a[] = {0x00, 0x00, 0x3C, 0x02, 0x3E, 0x42, 0x3E, 0x00};
        static const uint8_t v[] = {0x00, 0x00, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00};
        static const uint8_t i[] = {0x00, 0x08, 0x00, 0x18, 0x08, 0x08, 0x1C, 0x00};
        static const uint8_t l[] = {0x00, 0x18, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00};
        static const uint8_t b[] = {0x00, 0x40, 0x40, 0x5C, 0x62, 0x42, 0x3C, 0x00};
        static const uint8_t t[] = {0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x0E, 0x00};
        static const uint8_t h[] = {0x00, 0x40, 0x40, 0x5C, 0x62, 0x42, 0x42, 0x00};
        static const uint8_t g[] = {0x00, 0x00, 0x3E, 0x42, 0x3E, 0x02, 0x3C, 0x00};
        static const uint8_t p[] = {0x00, 0x00, 0x5C, 0x62, 0x62, 0x5C, 0x40, 0x00};
        static const uint8_t n[] = {0x00, 0x00, 0x5C, 0x62, 0x42, 0x42, 0x42, 0x00};
        static const uint8_t d[] = {0x00, 0x02, 0x02, 0x3E, 0x42, 0x42, 0x3E, 0x00};
        static const uint8_t f[] = {0x00, 0x0E, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x00};
        static const uint8_t space[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        
        switch (ch) {
            case 'N': return N;
            case 'R': return R;
            case 'o': return o;
            case 's': return s;
            case 'u': return u;
            case 'r': return r;
            case 'c': return c;
            case 'e': return e;
            case 'a': return a;
            case 'v': return v;
            case 'i': return i;
            case 'l': return l;
            case 'b': return b;
            case 't': return t;
            case 'h': return h;
            case 'g': return g;
            case 'p': return p;
            case 'n': return n;
            case 'd': return d;
            case 'f': return f;
            case ' ': return space;
            default: return space;
        }
    };
    
    // Draw each character with scaling support
    auto draw_char = [&](char ch, size_t pos_x, size_t pos_y) {
        const uint8_t* pattern = get_char_pattern(ch);
        for (size_t y = 0; y < 8; ++y) {
            uint8_t row = pattern[y];
            for (size_t x = 0; x < 8; ++x) {
                if (row & (1 << (7 - x))) {
                    // Draw scaled pixel (scale x scale block)
                    for (size_t sy = 0; sy < scale; ++sy) {
                        for (size_t sx = 0; sx < scale; ++sx) {
                            size_t px = pos_x + x * scale + sx;
                            size_t py = pos_y + y * scale + sy;
                            if (px < placeholder.width && py < placeholder.height) {
                                size_t offset = (py * placeholder.width + px) * 3;
                                placeholder.rgb_data[offset + 0] = 255;  // White
                                placeholder.rgb_data[offset + 1] = 255;
                                placeholder.rgb_data[offset + 2] = 255;
                            }
                        }
                    }
                }
            }
        }
    };
    
    for (size_t i = 0; i < message_len; ++i) {
        draw_char(message[i], text_start_x + i * char_width, text_start_y);
    }
    
    return placeholder;
}

PreviewRenderer::PreviewRenderer(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (dag_) {
        field_renderer_ = std::make_unique<DAGFieldRenderer>(dag_);
    }
}

std::vector<PreviewOutputInfo> PreviewRenderer::get_available_outputs(const NodeID& node_id) {
    std::vector<PreviewOutputInfo> outputs;
    
    // Special handling for placeholder node (no real content)
    if (node_id.to_string() == "_no_preview") {
        // Provide all output types so user can switch between them
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Field,
            "Field",
            1,  // Single placeholder item
            true,
            0.7,
            "",
            false,  // No dropouts for placeholder
            false,  // No separate channels for placeholder
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            true,
            0.7,
            "",
            false,  // No dropouts for placeholder
            false,  // No separate channels for placeholder
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            true,
            0.7,
            "",
            false,  // No dropouts for placeholder
            false,  // No separate channels for placeholder
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            true,
            0.7,
            "",
            false,  // No dropouts for placeholder
            false,  // No separate channels for placeholder
            0       // first_field_offset
        });
        return outputs;
    }
    
    if (!field_renderer_ || !node_id.is_valid()) {
        return outputs;
    }
    
    // Check if this is a previewable stage or sink node
    if (dag_) {
        const auto& dag_nodes = dag_->nodes();
        auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
            [&node_id](const auto& n) { return n.node_id == node_id; });
        
        if (node_it != dag_nodes.end() && node_it->stage) {
            // Check if this stage implements PreviewableStage (sources/transforms/sinks)
            auto* previewable_stage = dynamic_cast<const PreviewableStage*>(node_it->stage.get());
            if (previewable_stage) {
                if (previewable_stage->supports_preview()) {
                    // Stage supports preview - get outputs from stage options
                    return get_stage_preview_outputs(node_id, *node_it, *previewable_stage);
                }

                // Try executing with cache disabled to populate cached output, then re-check
                ensure_node_executed(node_id, true);
                if (previewable_stage->supports_preview()) {
                    return get_stage_preview_outputs(node_id, *node_it, *previewable_stage);
                }
            }
            
            auto node_type = node_it->stage->get_node_type_info().type;
            if (node_type == NodeType::SINK) {
                // Sink doesn't support preview - return empty (no preview available)
                ORC_LOG_DEBUG("Sink node '{}' does not support preview", node_id.to_string());
                return outputs;
            }
        }
    }
    
    // Try to render field 0 to see if node has outputs
    auto result = field_renderer_->render_field_at_node(node_id, FieldID(0));
    
    if (!result.is_valid || !result.representation) {
        // Node exists but can't render - provide placeholder outputs marked as unavailable
        // so GUI knows not to auto-open preview
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Field,
            "Field",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for unavailable content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for unavailable content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for unavailable content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for unavailable content
            false,  // No separate channels
            0       // first_field_offset
        });
        return outputs;
    }
    
    // Get total field count from representation
    auto field_count = result.representation->field_count();
    
    if (field_count == 0) {
        // Node rendered but has no fields - provide placeholder outputs marked as unavailable
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Field,
            "Field",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for empty content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for empty content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for empty content
            false,  // No separate channels
            0       // first_field_offset
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            "",
            false,  // No dropouts for empty content
            false,  // No separate channels
            0       // first_field_offset
        });
        return outputs;
    }
    
    // Field output - always available
    outputs.push_back(PreviewOutputInfo{
        PreviewOutputType::Field,
        "Field",
        field_count,
        true,
        0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
        "",
        true,  // Dropouts available for field outputs
        false, // No separate channels
        0      // first_field_offset (not applicable for field view)
    });
    
    // Frame outputs - available if we have at least 2 fields
    if (field_count >= 2) {
        // Determine first viewable frame based on is_first_field hint
        // We need to find the first field that is marked as "first"
        uint64_t first_frame_start = 0;
        
        // Check if field 0 is the first field by looking at hints
        auto parity_hint = result.representation->get_field_parity_hint(FieldID(0));
        if (parity_hint.has_value() && !parity_hint->is_first_field) {
            // Field 0 is second field, so first complete frame starts at field 1
            first_frame_start = 1;
        }
        
        // Calculate number of complete frames
        uint64_t complete_fields = field_count - first_frame_start;
        uint64_t frame_count = complete_fields / 2;
        
        if (frame_count > 0) {
            outputs.push_back(PreviewOutputInfo{
                PreviewOutputType::Frame,
                "Frame",
                frame_count,
                true,
                0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
                "",
                true,  // Dropouts available for frame outputs
                false, // No separate channels
                first_frame_start  // Field offset for first frame
            });
            
            outputs.push_back(PreviewOutputInfo{
                PreviewOutputType::Frame_Reversed,
                "Frame (Reversed)",
                frame_count,
                true,
                0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
                "",
                true,  // Dropouts available for reversed frame outputs
                false, // No separate channels
                first_frame_start  // Field offset for first frame
            });
            
            outputs.push_back(PreviewOutputInfo{
                PreviewOutputType::Split,
                "Split",
                frame_count,
                true,
                0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
                "",
                true,  // Dropouts available for split outputs
                false, // No separate channels
                first_frame_start  // Field offset for first frame
            });
        }
    }
    
    // TODO: Future output types
    // - Luma (luma component only)
    // - Chroma (requires chroma decoder)
    // - Composite (requires full signal reconstruction)
    
    return outputs;
}

uint64_t PreviewRenderer::get_output_count(const NodeID& node_id, PreviewOutputType type) {
    auto outputs = get_available_outputs(node_id);
    
    for (const auto& output : outputs) {
        if (output.type == type) {
            return output.count;
        }
    }
    
    return 0;
}

PreviewRenderResult PreviewRenderer::render_output(
    const NodeID& node_id,
    PreviewOutputType type,
    uint64_t index,
    const std::string& option_id,
    PreviewNavigationHint hint)
{
    ORC_LOG_DEBUG("render_output: node='{}', type={}, option_id='{}', index={}, hint={}",
                  node_id.to_string(), static_cast<int>(type), option_id, index,
                  (hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random"));
    
    PreviewRenderResult result;
    result.node_id = node_id;
    result.output_type = type;
    result.output_index = index;
    result.success = false;
    
    // Special handling for placeholder node - render "No source available" image
    if (node_id.to_string() == "_no_preview") {
        result.image = create_placeholder_image(type, "No source available");
        result.success = true;
        return result;
    }
    
    // Check if this is a previewable stage or sink node
    if (dag_) {
        const auto& dag_nodes = dag_->nodes();
        auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
            [&node_id](const auto& n) { return n.node_id == node_id; });
        
        if (node_it != dag_nodes.end() && node_it->stage) {
            // Check for PreviewableStage interface (any stage including sinks)
            auto* previewable_stage = dynamic_cast<const PreviewableStage*>(node_it->stage.get());
            
            if (previewable_stage && previewable_stage->supports_preview()) {
                // Render using stage's preview interface
                return render_stage_preview(node_id, *node_it, *previewable_stage, type, index, option_id, hint);
            }
        }
    }
    
    if (!field_renderer_) {
        result.error_message = "No DAG field renderer available";
        return result;
    }
    
    switch (type) {
        case PreviewOutputType::Field:
        case PreviewOutputType::Luma:
        {
            // Render single field
            FieldID field_id(index);
            auto field_result = field_renderer_->render_field_at_node(node_id, field_id);
            
            if (!field_result.is_valid || !field_result.representation) {
                // Return placeholder instead of error
                result.image = create_placeholder_image(type, "Nothing to output");
                result.success = true;
                result.error_message = field_result.error_message;
                return result;
            }
            
            result.image = render_field(field_result.representation, field_id);
            result.success = result.image.is_valid();
            
            if (!result.success) {
                // Return placeholder instead of error
                result.image = create_placeholder_image(type, "Nothing to output");
                result.success = true;
                result.error_message = "Failed to render field " + std::to_string(index);
            }
            break;
        }
        
        case PreviewOutputType::Frame:
        case PreviewOutputType::Frame_Reversed:
        case PreviewOutputType::Split:
        {
            // Determine first field offset by checking is_first_field observation
            uint64_t first_field_offset = 0;
            
            // Check field 0 to see if it's the first field
            auto probe_result = field_renderer_->render_field_at_node(node_id, FieldID(0));
            if (probe_result.is_valid && probe_result.representation) {
                auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
                if (parity_hint.has_value() && !parity_hint->is_first_field) {
                    // Field 0 is second field, so frames start at field 1
                    first_field_offset = 1;
                }
            }
            
            // Calculate field IDs for this frame
            uint64_t field_a_index = first_field_offset + (index * 2);     // First field of frame
            uint64_t field_b_index = field_a_index + 1;                     // Second field of frame
            
            FieldID field_a(field_a_index);
            FieldID field_b(field_b_index);
            
            // Render both fields
            auto result_a = field_renderer_->render_field_at_node(node_id, field_a);
            auto result_b = field_renderer_->render_field_at_node(node_id, field_b);
            
            if (!result_a.is_valid || !result_a.representation ||
                !result_b.is_valid || !result_b.representation) {
                // Return placeholder instead of error
                result.image = create_placeholder_image(type, "Nothing to output");
                result.success = true;
                result.error_message = "Failed to render one or both fields for frame " + std::to_string(index);
                return result;
            }
            
            // Choose rendering method based on type
            if (type == PreviewOutputType::Split) {
                // Split: stack fields vertically
                result.image = render_split_frame(
                    result_a.representation,
                    field_a,
                    field_b
                );
            } else {
                // Frame or Frame_Reversed: weave fields
                // Determine field order: if first_field_offset is 0, field 0 is the first field
                // If first_field_offset is 1, field 1 is the first field
                bool first_field_first = (first_field_offset == 0) ? (type == PreviewOutputType::Frame) : (type != PreviewOutputType::Frame);
                result.image = render_frame(
                    result_a.representation,
                    field_a,
                    field_b,
                    first_field_first
                );
            }
            
            result.success = result.image.is_valid();
            
            if (!result.success) {
                // Return placeholder instead of error
                result.image = create_placeholder_image(type, "Nothing to output");
                result.success = true;
                result.error_message = "Failed to render frame " + std::to_string(index);
            }
            break;
        }
        
        case PreviewOutputType::Chroma:
        case PreviewOutputType::Composite:
        default:
            result.error_message = "Output type not yet implemented";
            break;
    }
    
    // Render dropout highlighting onto the image if enabled
    if (result.success && result.image.is_valid()) {
        render_dropouts(result.image);
    }
    
    // Aspect ratio scaling removed from core; GUI handles display scaling
    
    return result;
}

void PreviewRenderer::update_dag(std::shared_ptr<const DAG> dag) {
    dag_ = dag;
    
    if (dag_) {
        field_renderer_ = std::make_unique<DAGFieldRenderer>(dag_);
    } else {
        field_renderer_.reset();
    }
}

PreviewImage PreviewRenderer::render_field(
    std::shared_ptr<const VideoFieldRepresentation> repr,
    FieldID field_id)
{
    PreviewImage image;
    
    if (!repr || !repr->has_field(field_id)) {
        return image;
    }
    
    // Check if this is an RGB field representation from chroma decoder
    if (repr->type_name() == "RGBFieldRepresentation") {
        ORC_LOG_DEBUG("render_field: Detected RGBFieldRepresentation for field {}", field_id.value());
        
        // Special handling for RGB data
        auto desc_opt = repr->get_descriptor(field_id);
        if (!desc_opt) {
            return image;
        }
        
        const auto& desc = *desc_opt;
        
        // Try to get RGB888 data directly
        // We need to dynamic_cast to access the get_rgb888_data() method
        // This is a bit hacky, but works for the local class
        // Get the field data which contains RGB in 16-bit format
        auto field_data = repr->get_field(field_id);
        if (field_data.empty() || field_data.size() < desc.width * desc.height * 3) {
            return image;
        }
        
        // Initialize image
        image.width = desc.width;
        image.height = desc.height;
        image.rgb_data.resize(image.width * image.height * 3);
        
        // Convert 16-bit RGB to 8-bit RGB
        for (size_t i = 0; i < image.rgb_data.size(); ++i) {
            if (i < field_data.size()) {
                // Scale from 16-bit to 8-bit
                image.rgb_data[i] = static_cast<uint8_t>(field_data[i] >> 8);
            }
        }
        
        return image;
    }
    
    // Get field descriptor for dimensions
    auto desc_opt = repr->get_descriptor(field_id);
    if (!desc_opt) {
        return image;
    }
    
    const auto& desc = *desc_opt;
    
    // Get field data
    auto field_data = repr->get_field(field_id);
    if (field_data.empty()) {
        return image;
    }
    
    // Get video parameters for IRE scaling
    auto video_params = repr->get_video_parameters();
    double blackIRE = video_params ? video_params->black_16b_ire : 0.0;
    double whiteIRE = video_params ? video_params->white_16b_ire : 65535.0;
    
    // Initialize image
    image.width = desc.width;
    image.height = desc.height;
    image.rgb_data.resize(image.width * image.height * 3);
    
    // Convert 16-bit samples to 8-bit RGB grayscale
    for (size_t y = 0; y < desc.height; ++y) {
        size_t field_offset = y * desc.width;
        size_t rgb_offset = y * desc.width * 3;
        
        for (size_t x = 0; x < desc.width; ++x) {
            if (field_offset + x >= field_data.size()) {
                break;
            }
            
            uint16_t sample = field_data[field_offset + x];
            uint8_t value = tbc_sample_to_8bit(sample, blackIRE, whiteIRE);
            
            // Grayscale (R=G=B)
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
    // Extract dropout regions for this field
    image.dropout_regions = repr->get_dropout_hints(field_id);
    ORC_LOG_DEBUG("render_field: Extracted {} dropout regions for field {}", 
                  image.dropout_regions.size(), field_id.value());
    
    return image;
}

PreviewImage PreviewRenderer::render_frame(
    std::shared_ptr<const VideoFieldRepresentation> repr,
    FieldID field_a,
    FieldID field_b,
    bool first_field_first)
{
    PreviewImage image;
    
    if (!repr || !repr->has_field(field_a) || !repr->has_field(field_b)) {
        return image;
    }
    
    // Check if this is RGB data from chroma decoder
    if (repr->type_name() == "RGBFieldRepresentation") {
        ORC_LOG_DEBUG("render_frame: Detected RGBFieldRepresentation, using RGB rendering");
        
        // For RGB preview, the representation contains a full decoded frame
        // Both fields should return the same RGB data
        auto desc_opt = repr->get_descriptor(field_a);
        if (!desc_opt) {
            return image;
        }
        
        const auto& desc = *desc_opt;
        auto field_data = repr->get_field(field_a);
        
        if (field_data.empty() || field_data.size() < desc.width * desc.height * 3) {
            ORC_LOG_WARN("render_frame: RGB field data size mismatch: got {}, expected {}", 
                         field_data.size(), desc.width * desc.height * 3);
            return image;
        }
        
        // Initialize image
        image.width = desc.width;
        image.height = desc.height;
        image.rgb_data.resize(image.width * image.height * 3);
        
        ORC_LOG_DEBUG("render_frame: Converting RGB frame {}x{}, {} bytes", 
                      image.width, image.height, field_data.size());
        
        // Convert 16-bit RGB to 8-bit RGB
        for (size_t i = 0; i < image.rgb_data.size() && i < field_data.size(); ++i) {
            image.rgb_data[i] = static_cast<uint8_t>(field_data[i] >> 8);
        }
        
        return image;
    }
    
    // Get field descriptors
    auto desc_a_opt = repr->get_descriptor(field_a);
    auto desc_b_opt = repr->get_descriptor(field_b);
    
    if (!desc_a_opt || !desc_b_opt) {
        return image;
    }
    
    const auto& desc_a = *desc_a_opt;
    const auto& desc_b = *desc_b_opt;
    
    // Get field data
    auto field_a_data = repr->get_field(field_a);
    auto field_b_data = repr->get_field(field_b);
    
    if (field_a_data.empty() || field_b_data.empty()) {
        return image;
    }
    
    // Get video parameters for IRE scaling
    auto video_params = repr->get_video_parameters();
    double blackIRE = video_params ? video_params->black_16b_ire : 0.0;
    double whiteIRE = video_params ? video_params->white_16b_ire : 65535.0;
    
    // Frame height is sum of both field heights (they can differ, e.g., NTSC: 262 + 263 = 525)
    image.width = desc_a.width;
    image.height = desc_a.height + desc_b.height;
    image.rgb_data.resize(image.width * image.height * 3);
    
    // Weave fields together
    // If first_field_first: field_a on even lines, field_b on odd lines
    // If !first_field_first: field_b on even lines, field_a on odd lines
    
    for (size_t frame_y = 0; frame_y < image.height; ++frame_y) {
        bool use_field_a = first_field_first ? (frame_y % 2 == 0) : (frame_y % 2 != 0);
        const auto& field_data = use_field_a ? field_a_data : field_b_data;
        
        // Calculate which line within the field this corresponds to
        // Each field line appears twice in the frame (at positions frame_y where frame_y % 2 matches the field)
        size_t field_y = frame_y / 2;
        size_t field_offset = field_y * image.width;
        size_t rgb_offset = frame_y * image.width * 3;
        
        for (size_t x = 0; x < image.width; ++x) {
            if (field_offset + x >= field_data.size()) {
                break;
            }
            
            uint16_t sample = field_data[field_offset + x];
            uint8_t value = tbc_sample_to_8bit(sample, blackIRE, whiteIRE);
            
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
    // Combine dropout regions from both fields
    // Field A dropouts go on even lines, Field B on odd lines (adjusted by first_field_first)
    auto dropouts_a = repr->get_dropout_hints(field_a);
    auto dropouts_b = repr->get_dropout_hints(field_b);
    
    ORC_LOG_DEBUG("render_frame: Field {} has {} dropouts, Field {} has {} dropouts",
                  field_a.value(), dropouts_a.size(), field_b.value(), dropouts_b.size());
    
    // Adjust line numbers for interlaced frame display
    for (auto& region : dropouts_a) {
        // Field A lines map to even/odd frame lines depending on first_field_first
        region.line = first_field_first ? (region.line * 2) : (region.line * 2 + 1);
        image.dropout_regions.push_back(region);
    }
    
    for (auto& region : dropouts_b) {
        // Field B lines map to odd/even frame lines depending on first_field_first
        region.line = first_field_first ? (region.line * 2 + 1) : (region.line * 2);
        image.dropout_regions.push_back(region);
    }
    
    return image;
}

PreviewImage PreviewRenderer::render_split_frame(
    std::shared_ptr<const VideoFieldRepresentation> repr,
    FieldID field_a,
    FieldID field_b)
{
    PreviewImage image;
    
    if (!repr || !repr->has_field(field_a) || !repr->has_field(field_b)) {
        return image;
    }
    
    // Get field descriptors
    auto desc_a_opt = repr->get_descriptor(field_a);
    auto desc_b_opt = repr->get_descriptor(field_b);
    
    if (!desc_a_opt || !desc_b_opt) {
        return image;
    }
    
    const auto& desc_a = *desc_a_opt;
    const auto& desc_b = *desc_b_opt;
    
    // Get field data
    auto field_a_data = repr->get_field(field_a);
    auto field_b_data = repr->get_field(field_b);
    
    if (field_a_data.empty() || field_b_data.empty()) {
        return image;
    }
    
    // Get video parameters for IRE scaling
    auto video_params = repr->get_video_parameters();
    double blackIRE = video_params ? video_params->black_16b_ire : 0.0;
    double whiteIRE = video_params ? video_params->white_16b_ire : 65535.0;
    
    // Split frame: stack fields vertically
    // Top half is field_a, bottom half is field_b
    image.width = desc_a.width;
    image.height = desc_a.height + desc_b.height;  // Sum of field heights (can differ)
    image.rgb_data.resize(image.width * image.height * 3);
    
    // Copy field_a to top half
    for (size_t field_y = 0; field_y < desc_a.height; ++field_y) {
        size_t field_offset = field_y * image.width;
        size_t rgb_offset = field_y * image.width * 3;
        
        for (size_t x = 0; x < image.width; ++x) {
            if (field_offset + x >= field_a_data.size()) {
                break;
            }
            
            uint16_t sample = field_a_data[field_offset + x];
            uint8_t value = tbc_sample_to_8bit(sample, blackIRE, whiteIRE);
            
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
    // Copy field_b to bottom half
    for (size_t field_y = 0; field_y < desc_b.height; ++field_y) {
        size_t frame_y = desc_a.height + field_y;  // Offset to bottom half
        size_t field_offset = field_y * image.width;
        size_t rgb_offset = frame_y * image.width * 3;
        
        for (size_t x = 0; x < image.width; ++x) {
            if (field_offset + x >= field_b_data.size()) {
                break;
            }
            
            uint16_t sample = field_b_data[field_offset + x];
            uint8_t value = tbc_sample_to_8bit(sample, blackIRE, whiteIRE);
            
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
    // Add dropout regions for split frame (top half is field_a, bottom half is field_b)
    auto dropouts_a = repr->get_dropout_hints(field_a);
    auto dropouts_b = repr->get_dropout_hints(field_b);
    
    // Field A dropouts go in top half (no adjustment needed)
    image.dropout_regions = dropouts_a;
    
    // Field B dropouts go in bottom half (offset line numbers by field_a height)
    for (auto& region : dropouts_b) {
        region.line += desc_a.height;
        image.dropout_regions.push_back(region);
    }
    
    return image;
}

uint8_t PreviewRenderer::tbc_sample_to_8bit(uint16_t sample, double blackIRE, double whiteIRE) {
    // IRE level scaling from metadata (black_16b_ire and white_16b_ire from capture table)
    // This matches the implementation in PreviewHelpers::scale_16bit_to_8bit()
    double ireRange = whiteIRE - blackIRE;
    int32_t adjusted = static_cast<int32_t>(sample) - static_cast<int32_t>(blackIRE);
    int32_t scaled = static_cast<int32_t>((adjusted * 255.0) / ireRange);
    return static_cast<uint8_t>(std::max(0, std::min(255, scaled)));
}

namespace {
} // anonymous namespace



bool PreviewRenderer::save_png(
    const NodeID& node_id,
    PreviewOutputType type,
    uint64_t index,
    const std::string& filename,
    const std::string& option_id)
{
    // Render the output (includes aspect ratio scaling from render_output)
    auto result = render_output(node_id, type, index, option_id);
    
    if (!result.success || !result.image.is_valid()) {
        ORC_LOG_ERROR("Failed to render output for PNG export: {}", result.error_message);
        return false;
    }
    
    // Save to PNG (image is already scaled by render_output)
    return save_png(result.image, filename);
}

bool PreviewRenderer::save_png(const PreviewImage& image, const std::string& filename) {
    if (!image.is_valid()) {
        ORC_LOG_ERROR("Invalid image for PNG export");
        return false;
    }
    
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        ORC_LOG_ERROR("Failed to open file for writing: {}", filename);
        return false;
    }
    
    // Create PNG structures
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        ORC_LOG_ERROR("Failed to create PNG write structure");
        return false;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        ORC_LOG_ERROR("Failed to create PNG info structure");
        return false;
    }
    
    // Error handling
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        ORC_LOG_ERROR("PNG write error");
        return false;
    }
    
    png_init_io(png, fp);
    
    // Set image attributes
    png_set_IHDR(
        png,
        info,
        image.width,
        image.height,
        8,                      // 8 bits per channel
        PNG_COLOR_TYPE_RGB,     // RGB color type
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    
    png_write_info(png, info);
    
    // Write image data row by row
    for (uint32_t y = 0; y < image.height; ++y) {
        png_bytep row = const_cast<png_bytep>(&image.rgb_data[y * image.width * 3]);
        png_write_row(png, row);
    }
    
    png_write_end(png, nullptr);
    
    // Cleanup
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    
    ORC_LOG_DEBUG("Saved PNG: {} ({}x{})", filename, image.width, image.height);
    return true;
}



void PreviewRenderer::set_show_dropouts(bool show) {
    show_dropouts_ = show;
}

bool PreviewRenderer::get_show_dropouts() const {
    return show_dropouts_;
}

std::shared_ptr<const VideoFieldRepresentation> PreviewRenderer::get_representation_at_node(const NodeID& node_id) {
    // Ensure node is executed
    ensure_node_executed(node_id);
    
    // Use field renderer to get the representation
    // We need to render a field to get the representation - use field 0 as a representative
    if (!field_renderer_) {
        return nullptr;
    }
    
    // Render field 0 to get the representation at this node
    auto result = field_renderer_->render_field_at_node(node_id, FieldID(0));
    
    if (!result.is_valid || !result.representation) {
        return nullptr;
    }
    
    return result.representation;
}

void PreviewRenderer::render_dropouts(PreviewImage& image) const {
    if (!show_dropouts_ || image.dropout_regions.empty() || !image.is_valid()) {
        return;
    }
    
    // Render dropout regions as red highlights directly onto the RGB data
    for (const auto& region : image.dropout_regions) {
        // Validate line number
        if (region.line >= image.height) {
            continue;
        }
        
        // Clamp sample range to image width
        uint32_t start_x = std::min(region.start_sample, static_cast<uint32_t>(image.width));
        uint32_t end_x = std::min(region.end_sample, static_cast<uint32_t>(image.width));
        
        if (start_x >= end_x) {
            continue;
        }
        
        // Draw horizontal line at this scanline
        size_t row_offset = region.line * image.width * 3;
        for (uint32_t x = start_x; x < end_x; ++x) {
            size_t pixel_offset = row_offset + x * 3;
            if (pixel_offset + 2 < image.rgb_data.size()) {
                // Blend with red (75% red, 25% original)
                image.rgb_data[pixel_offset + 0] = static_cast<uint8_t>(
                    image.rgb_data[pixel_offset + 0] * 0.25 + 255 * 0.75);  // R
                image.rgb_data[pixel_offset + 1] = static_cast<uint8_t>(
                    image.rgb_data[pixel_offset + 1] * 0.25);                // G
                image.rgb_data[pixel_offset + 2] = static_cast<uint8_t>(
                    image.rgb_data[pixel_offset + 2] * 0.25);                // B
            }
        }
    }
}



uint64_t PreviewRenderer::get_equivalent_index(
    PreviewOutputType from_type,
    uint64_t from_index,
    PreviewOutputType to_type
) const {
    // Helper to determine if a type is frame-based
    auto is_frame_type = [](PreviewOutputType type) {
        return type == PreviewOutputType::Frame || 
               type == PreviewOutputType::Frame_Reversed ||
               type == PreviewOutputType::Split;
    };
    
    bool from_is_frame = is_frame_type(from_type);
    bool to_is_frame = is_frame_type(to_type);
    
    if (from_is_frame && !to_is_frame) {
        // Frame to field: Frame N -> Field (N*2)
        // Show the first field of the frame
        return from_index * 2;
    } else if (!from_is_frame && to_is_frame) {
        // Field to frame: Field N -> Frame (N/2)
        // Show the frame containing the field
        return from_index / 2;
    } else {
        // Same category (both frame or both field) - keep same index
        return from_index;
    }
}

std::string PreviewRenderer::get_preview_item_label(
    PreviewOutputType type,
    uint64_t index,
    uint64_t total_count
) const {
    // Get display name for this output type
    std::string type_name;
    switch (type) {
        case PreviewOutputType::Field:
            type_name = "Field";
            break;
        case PreviewOutputType::Frame:
            type_name = "Frame";
            break;
        case PreviewOutputType::Frame_Reversed:
            type_name = "Frame (Reversed)";
            break;
        case PreviewOutputType::Split:
            type_name = "Split";
            break;
        case PreviewOutputType::Luma:
            type_name = "Luma";
            break;
        case PreviewOutputType::Chroma:
            type_name = "Chroma";
            break;
        case PreviewOutputType::Composite:
            type_name = "Composite";
            break;
        default:
            type_name = "Item";
            break;
    }
    
    // Convert 0-based index to 1-based for display
    uint64_t display_index = index + 1;
    
    if (type == PreviewOutputType::Field) {
        // Field view: just show field number
        return type_name + " " + std::to_string(display_index) + " / " + std::to_string(total_count);
    } else {
        // Frame-based views: show frame number with constituent field numbers
        // Frame N is made of fields (N*2) and (N*2+1) in 0-based indexing
        // So frame at index I is made of fields (I*2) and (I*2+1)
        uint64_t first_field = index * 2;
        uint64_t second_field = first_field + 1;
        
        // Convert to 1-based for display
        uint64_t first_field_display = first_field + 1;
        uint64_t second_field_display = second_field + 1;
        
        std::string label = type_name + " " + std::to_string(display_index);
        
        // Add field composition
        if (type == PreviewOutputType::Frame_Reversed) {
            // Reversed: show second field first
            label += " (" + std::to_string(second_field_display) + "-" + std::to_string(first_field_display) + ")";
        } else {
            // Normal: show first field first
            label += " (" + std::to_string(first_field_display) + "-" + std::to_string(second_field_display) + ")";
        }
        
        label += " / " + std::to_string(total_count);
        
        return label;
    }
}

PreviewItemDisplayInfo PreviewRenderer::get_preview_item_display_info(
    PreviewOutputType type,
    uint64_t index,
    uint64_t total_count
) const {
    PreviewItemDisplayInfo info;
    
    // Get display name for this output type
    switch (type) {
        case PreviewOutputType::Field:
            info.type_name = "Field";
            break;
        case PreviewOutputType::Frame:
            info.type_name = "Frame";
            break;
        case PreviewOutputType::Frame_Reversed:
            info.type_name = "Frame (Reversed)";
            break;
        case PreviewOutputType::Split:
            info.type_name = "Split";
            break;
        case PreviewOutputType::Luma:
            info.type_name = "Luma";
            break;
        case PreviewOutputType::Chroma:
            info.type_name = "Chroma";
            break;
        case PreviewOutputType::Composite:
            info.type_name = "Composite";
            break;
        default:
            info.type_name = "Item";
            break;
    }
    
    // Use 0-based indexing throughout
    info.current_number = index;
    info.total_count = total_count;
    
    if (type == PreviewOutputType::Field) {
        // Field view: no constituent field info
        info.has_field_info = false;
        info.first_field_number = 0;
        info.second_field_number = 0;
    } else {
        // Frame-based views: calculate constituent field numbers
        info.has_field_info = true;
        
        uint64_t first_field = index * 2;
        uint64_t second_field = first_field + 1;
        
        // Use 0-based indexing
        info.first_field_number = first_field;
        info.second_field_number = second_field;
    }
    
    return info;
}

FrameLineNavigationResult PreviewRenderer::navigate_frame_line(
    const NodeID& node_id,
    PreviewOutputType output_type,
    uint64_t current_field,
    int current_line,
    int direction,
    int field_height  // Note: Not used - we check actual field heights instead
) const {
    (void)field_height;  // Suppress unused parameter warning
    FrameLineNavigationResult result;
    result.is_valid = false;
    result.new_field_index = current_field;
    result.new_line_number = current_line;
    
    // Only valid for frame modes
    if (output_type != PreviewOutputType::Frame && output_type != PreviewOutputType::Frame_Reversed) {
        ORC_LOG_DEBUG("navigate_frame_line: Invalid output type (must be Frame or Frame_Reversed)");
        return result;
    }
    
    // Use the SAME logic as render_output() to determine field order
    uint64_t first_field_offset = 0;
    auto probe_result = field_renderer_->render_field_at_node(node_id, FieldID(0));
    if (probe_result.is_valid && probe_result.representation) {
        auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
        if (parity_hint.has_value() && !parity_hint->is_first_field) {
            // Field 0 is second field, so frames start at field 1
            first_field_offset = 1;
        }
    }
    
    ORC_LOG_DEBUG("navigate_frame_line: first_field_offset={}, current_field={}", first_field_offset, current_field);
    
    // For a frame, field_a is the first field shown, field_b is the second
    // In the interlaced display:
    //   - Even image lines (0, 2, 4...) show field_a
    //   - Odd image lines (1, 3, 5...) show field_b
    // OR if Frame_Reversed, swap them
    
    bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);
    
    // Determine which field corresponds to the current position
    // The current_field we receive is already the actual field index
    // Determine if it's the first or second field of its frame
    // Adjust for first_field_offset: if offset=1, then field 1 is first, field 2 is second, etc.
    bool current_is_first_field = ((current_field - first_field_offset) % 2 == 0);
    
    ORC_LOG_DEBUG("navigate_frame_line: current_is_first_field={} (before reverse check)", current_is_first_field);
    
    if (is_reversed) {
        current_is_first_field = !current_is_first_field;
    }
    
    ORC_LOG_DEBUG("navigate_frame_line: current_is_first_field={} (after reverse check, is_reversed={})", 
                  current_is_first_field, is_reversed);
    
    // Navigate within the interlaced frame display
    // NOTE: Fields may have different heights (NTSC: 262/263, PAL: 312/313)
    uint64_t new_field = current_field;
    int new_line = current_line;
    
    // Get current field descriptor to check its height
    auto current_field_result = field_renderer_->render_field_at_node(node_id, FieldID(current_field));
    if (!current_field_result.is_valid || !current_field_result.representation) {
        ORC_LOG_DEBUG("navigate_frame_line: Current field {} not available", current_field);
        return result;
    }
    auto current_field_descriptor = current_field_result.representation->get_descriptor(FieldID(current_field));
    if (!current_field_descriptor) {
        ORC_LOG_DEBUG("navigate_frame_line: Current field {} has no descriptor", current_field);
        return result;
    }
    
    if (direction > 0) {
        // Moving down through interlaced lines
        if (current_is_first_field) {
            // Currently showing first field line -> next shows second field, same line number
            new_field = current_field + 1;
            new_line = current_line;  // Same line within field
        } else {
            // Currently showing second field line
            // The second field has one extra line (line 312) that doesn't exist in first field
            // When at line 311 (last line in both fields), next line is 312 (extra line in same field)
            // When at line 312 (the extra line), can't navigate further
            if (current_line >= static_cast<int>(current_field_descriptor->height) - 1) {
                // At line 312 (the extra line) -> can't go further
                ORC_LOG_DEBUG("navigate_frame_line: At extra line of second field, can't navigate further down");
                return result;
            }
            // Check if next line is the extra line (line 312, height-1)
            if (current_line + 1 >= static_cast<int>(current_field_descriptor->height) - 1) {
                // Next line would be the extra line -> stay in same field
                new_field = current_field;
                new_line = current_line + 1;
            } else {
                // Normal alternation: next shows first field at next line
                new_field = current_field - 1;
                new_line = current_line + 1;
            }
        }
    } else if (direction < 0) {
        // Moving up through interlaced lines  
        if (current_is_first_field) {
            // Currently showing first field line -> prev shows second field, prev line number
            new_field = current_field + 1;
            new_line = current_line - 1;  // Previous line within second field
        } else {
            // Currently showing second field line
            // Special case: if we're on line 312 (the extra line), prev is line 311 (same field)
            if (current_line >= static_cast<int>(current_field_descriptor->height) - 1) {
                // At the extra line (line 312) -> prev is line 311 in same field
                new_field = current_field;
                new_line = current_line - 1;
            } else {
                // Normal alternation: prev shows first field at same line
                new_field = current_field - 1;
                new_line = current_line;
            }
        }
    }
    
    // Bounds check - validate that the new field index actually exists first
    auto new_field_result = field_renderer_->render_field_at_node(node_id, FieldID(new_field));
    if (!new_field_result.is_valid || !new_field_result.representation) {
        ORC_LOG_DEBUG("navigate_frame_line: Field {} not available", new_field);
        return result;
    }
    
    // Get the actual field descriptor to check real field height (handles 262/263 correctly)
    auto new_field_descriptor = new_field_result.representation->get_descriptor(FieldID(new_field));
    if (!new_field_descriptor) {
        ORC_LOG_DEBUG("navigate_frame_line: Field {} has no descriptor", new_field);
        return result;
    }
    
    // Bounds check - validate line number against actual field height
    // Don't use generic field_height parameter as it assumes equal field sizes
    if (new_line < 0 || new_line >= static_cast<int>(new_field_descriptor->height)) {
        ORC_LOG_DEBUG("navigate_frame_line: Out of bounds - line {} (actual field height={})", new_line, new_field_descriptor->height);
        return result;
    }
    
    // Also check that we're not beyond the total field count
    size_t total_fields = new_field_result.representation->field_count();
    if (new_field >= total_fields) {
        ORC_LOG_DEBUG("navigate_frame_line: Field {} exceeds total field count {}", new_field, total_fields);
        return result;
    }
    
    result.is_valid = true;
    result.new_field_index = new_field;
    result.new_line_number = new_line;
    
    ORC_LOG_DEBUG("navigate_frame_line: field {}->{}  line {}->{}  direction={}", 
        current_field, new_field, current_line, new_line, direction);
    
    return result;
}

ImageToFieldMappingResult PreviewRenderer::map_image_to_field(
    const NodeID& node_id,
    PreviewOutputType output_type,
    uint64_t output_index,
    int image_y,
    int image_height
) const {
    ImageToFieldMappingResult result;
    result.is_valid = false;
    result.field_index = 0;
    result.field_line = 0;
    
    if (output_type == PreviewOutputType::Field) {
        // Simple case: field mode, image_y is the line number
        // Validate against actual field height
        auto field_result = field_renderer_->render_field_at_node(node_id, FieldID(output_index));
        if (!field_result.is_valid || !field_result.representation) {
            return result;
        }
        
        auto descriptor = field_result.representation->get_descriptor(FieldID(output_index));
        if (!descriptor || image_y < 0 || image_y >= static_cast<int>(descriptor->height)) {
            return result;  // Line out of bounds
        }
        
        result.is_valid = true;
        result.field_index = output_index;
        result.field_line = image_y;
        return result;
    }
    
    if (output_type == PreviewOutputType::Frame || output_type == PreviewOutputType::Frame_Reversed) {
        // Frame mode: determine field order and top/bottom placement using parity hints
        uint64_t first_field_offset = 0;
        auto probe_result = field_renderer_->render_field_at_node(node_id, FieldID(0));
        if (probe_result.is_valid && probe_result.representation) {
            auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
            if (parity_hint.has_value() && !parity_hint->is_first_field) {
                first_field_offset = 1;
            }
        }
        bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);

        // Calculate fields composing this frame
        uint64_t frame_first_field = first_field_offset + (output_index * 2);
        uint64_t frame_second_field = frame_first_field + 1;

        // Determine whether the first field is on even (top) or odd (bottom) image lines
        bool first_is_top = true; // default assumption
        auto first_result = field_renderer_->render_field_at_node(node_id, FieldID(frame_first_field));
        std::shared_ptr<const VideoFieldRepresentation> first_repr;
        if (first_result.is_valid && first_result.representation) {
            first_repr = first_result.representation;
            auto first_parity = first_repr->get_field_parity_hint(FieldID(frame_first_field));
            if (first_parity.has_value()) {
                first_is_top = first_parity->is_first_field;
            }
        }
        
        // Early validation: ensure we have field representation before proceeding
        if (!first_repr) {
            return result;  // Node doesn't support field rendering (e.g., sink nodes)
        }
        
        // Account for reversed weaving
        if (is_reversed) first_is_top = !first_is_top;

        // Get the actual field heights to handle odd total line counts correctly
        auto first_descriptor = first_repr->get_descriptor(FieldID(frame_first_field));
        auto second_result = field_renderer_->render_field_at_node(node_id, FieldID(frame_second_field));
        if (!first_descriptor || !second_result.is_valid || !second_result.representation) {
            return result;  // Missing field data
        }
        auto second_descriptor = second_result.representation->get_descriptor(FieldID(frame_second_field));
        if (!second_descriptor) {
            return result;
        }
        
        size_t first_field_height = first_descriptor->height;
        size_t second_field_height = second_descriptor->height;
        
        // For NTSC: first field = 262, second field = 263, total = 525 lines
        // Lines are interleaved: 0,2,4...522 from first (262 lines), 1,3,5...523 from second (262 lines)
        // But we have line 524 left! It must come from the second field (which has 263 lines)
        // So the last line switches to whichever field has more lines
        
        bool is_even_line = (image_y % 2) == 0;
        bool use_first = (is_even_line == first_is_top);
        
        // Check if this would be out of bounds for the selected field
        int tentative_field_line = image_y / 2;
        if (use_first && tentative_field_line >= static_cast<int>(first_field_height)) {
            // Would be out of bounds for first field, must be from second field
            use_first = false;
            tentative_field_line = image_y / 2;  // Recalculate for second field
        } else if (!use_first && tentative_field_line >= static_cast<int>(second_field_height)) {
            // Would be out of bounds for second field, must be from first field
            use_first = true;
            tentative_field_line = image_y / 2;  // Recalculate for first field
        }
        
        result.field_index = use_first ? frame_first_field : frame_second_field;
        result.field_line = tentative_field_line;
        
        // Validate that the calculated field_line is within the actual field height
        if (!first_repr) {
            return result;  // No representation available
        }
        
        auto target_field_result = field_renderer_->render_field_at_node(node_id, FieldID(result.field_index));
        if (!target_field_result.is_valid || !target_field_result.representation) {
            return result;
        }
        
        auto target_descriptor = target_field_result.representation->get_descriptor(FieldID(result.field_index));
        if (!target_descriptor || result.field_line < 0 || result.field_line >= static_cast<int>(target_descriptor->height)) {
            return result;  // Line out of bounds for this field
        }
        
        result.is_valid = true;
        return result;
    }
    
    if (output_type == PreviewOutputType::Split) {
        // Split mode: top half is first field, bottom half is second field
        int split_point = image_height / 2;
        
        if (image_y < split_point) {
            // Top half - first field
            result.field_index = output_index * 2;
            result.field_line = image_y;
        } else {
            // Bottom half - second field
            result.field_index = output_index * 2 + 1;
            result.field_line = image_y - split_point;
        }
        
        // Validate that the calculated field_line is within the actual field height
        auto target_field_result = field_renderer_->render_field_at_node(node_id, FieldID(result.field_index));
        if (!target_field_result.is_valid || !target_field_result.representation) {
            return result;
        }
        
        auto target_descriptor = target_field_result.representation->get_descriptor(FieldID(result.field_index));
        if (!target_descriptor || result.field_line < 0 || result.field_line >= static_cast<int>(target_descriptor->height)) {
            return result;  // Line out of bounds for this field
        }
        
        result.is_valid = true;
        return result;
    }
    
    // Unsupported output type
    return result;
}

FieldToImageMappingResult PreviewRenderer::map_field_to_image(
    const NodeID& node_id,
    PreviewOutputType output_type,
    uint64_t output_index,
    uint64_t field_index,
    int field_line,
    int image_height
) const {
    FieldToImageMappingResult result;
    result.is_valid = false;
    result.image_y = 0;
    
    if (output_type == PreviewOutputType::Field) {
        // Simple case: field mode, line number is the image_y
        result.is_valid = true;
        result.image_y = field_line;
        return result;
    }
    
    if (output_type == PreviewOutputType::Frame || output_type == PreviewOutputType::Frame_Reversed) {
        // Frame mode: determine field order and placement using parity hints
        uint64_t first_field_offset = 0;
        auto probe_result = field_renderer_->render_field_at_node(node_id, FieldID(0));
        if (probe_result.is_valid && probe_result.representation) {
            auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
            if (parity_hint.has_value() && !parity_hint->is_first_field) {
                first_field_offset = 1;
            }
        }
        bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);

        // Calculate fields composing this frame
        uint64_t frame_first_field = first_field_offset + (output_index * 2);
        uint64_t frame_second_field = frame_first_field + 1;

        // Determine whether the first field is on even (top) or odd (bottom) lines
        bool first_is_top = true; // default assumption
        {
            auto first_result = field_renderer_->render_field_at_node(node_id, FieldID(frame_first_field));
            if (first_result.is_valid && first_result.representation) {
                auto first_parity = first_result.representation->get_field_parity_hint(FieldID(frame_first_field));
                if (first_parity.has_value()) {
                    first_is_top = first_parity->is_first_field;
                }
            }
        }
        if (is_reversed) first_is_top = !first_is_top;

        if (field_index == frame_first_field) {
            result.image_y = first_is_top ? (field_line * 2) : (field_line * 2 + 1);
        } else if (field_index == frame_second_field) {
            result.image_y = first_is_top ? (field_line * 2 + 1) : (field_line * 2);
        } else {
            // Field doesn't belong to this frame
            return result;
        }
        result.is_valid = true;
        return result;
    }
    
    if (output_type == PreviewOutputType::Split) {
        // Split mode: top half is first field, bottom half is second field
        int split_point = image_height / 2;
        
        if (field_index == output_index * 2) {
            // First field - top half
            result.image_y = field_line;
        } else if (field_index == output_index * 2 + 1) {
            // Second field - bottom half
            result.image_y = field_line + split_point;
        } else {
            // Field doesn't belong to this output
            return result;
        }
        result.is_valid = true;
        return result;
    }
    
    // Unsupported output type
    return result;
}

FrameFieldsResult PreviewRenderer::get_frame_fields(
    const NodeID& node_id,
    uint64_t frame_index
) const {
    FrameFieldsResult result;
    result.is_valid = false;
    result.first_field = 0;
    result.second_field = 0;
    
    // Determine first_field_offset using the SAME logic as render_output()
    uint64_t first_field_offset = 0;
    auto probe_result = field_renderer_->render_field_at_node(node_id, FieldID(0));
    if (probe_result.is_valid && probe_result.representation) {
        auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
        if (parity_hint.has_value() && !parity_hint->is_first_field) {
            first_field_offset = 1;
        }
    }
    
    // Calculate field indices for this frame
    result.first_field = first_field_offset + (frame_index * 2);
    result.second_field = result.first_field + 1;
    result.is_valid = true;
    
    return result;
}

SuggestedViewNode PreviewRenderer::get_suggested_view_node() const {
    // Special placeholder node ID for when no real content is available
    const NodeID PLACEHOLDER_NODE = NodeID(-999);  // Use special negative ID for placeholder
    
    if (!dag_) {
        return SuggestedViewNode{
            PLACEHOLDER_NODE,
            false,
            "No DAG available"
        };
    }
    
    const auto& dag_nodes = dag_->nodes();
    if (dag_nodes.empty()) {
        return SuggestedViewNode{
            PLACEHOLDER_NODE,
            false,
            "Project has no processing nodes - add nodes in the DAG Editor"
        };
    }
    
    // Priority 1: First SOURCE node
    for (const auto& node : dag_nodes) {
        if (node.stage) {
            auto node_type_info = node.stage->get_node_type_info();
            if (node_type_info.type == NodeType::SOURCE) {
                return SuggestedViewNode{
                    node.node_id,
                    true,
                    fmt::format("Viewing source: {}", node.node_id)
                };
            }
        }
    }
    
    // Priority 2: First node with outputs (not a SINK)
    for (const auto& node : dag_nodes) {
        if (node.stage) {
            auto node_type_info = node.stage->get_node_type_info();
            if (node_type_info.type != NodeType::SINK) {
                return SuggestedViewNode{
                    node.node_id,
                    true,
                    fmt::format("Viewing node: {}", node.node_id)
                };
            }
        }
    }
    
    // Priority 3: First previewable SINK node
    for (const auto& node : dag_nodes) {
        if (node.stage) {
            auto node_type_info = node.stage->get_node_type_info();
            if (node_type_info.type == NodeType::SINK) {
                auto* previewable_stage = dynamic_cast<const PreviewableStage*>(node.stage.get());
                if (previewable_stage && previewable_stage->supports_preview()) {
                    return SuggestedViewNode{
                        node.node_id,
                        true,
                        fmt::format("Viewing sink preview: {}", node.node_id)
                    };
                }
            }
        }
    }
    
    // Only non-previewable SINK nodes available - return placeholder
    return SuggestedViewNode{
        PLACEHOLDER_NODE,
        true,
        "Project only contains sink nodes - no preview available"
    };
}

// ============================================================================
// Stage preview support
// ============================================================================

void PreviewRenderer::ensure_node_executed(const NodeID& node_id, bool disable_cache) const
{
    if (!dag_) {
        return;
    }
    
    // For sink nodes, we need to execute their inputs to populate cached_input_
    // For other nodes, execute up to the node itself
    const auto& dag_nodes = dag_->nodes();
    auto node_it = std::find_if(dag_nodes.begin(), dag_nodes.end(),
        [&node_id](const auto& n) { return n.node_id == node_id; });
    
    if (node_it == dag_nodes.end()) {
        ORC_LOG_ERROR("Node '{}' not found in DAG", node_id.to_string());
        return;
    }
    
    bool is_sink = (node_it->stage && node_it->stage->get_node_type_info().type == NodeType::SINK);
    
    // CRITICAL: Only disable artifact caching when explicitly requested (e.g., for actual rendering)
    // We need execute() to be called on the stage instance so it can populate
    // its cached_output_ member for preview rendering. If artifact caching is enabled,
    // the executor returns cached artifacts without calling execute(), leaving the
    // stage's cached_output_ null.
    // However, when just querying available outputs, we can use cached results to avoid
    // re-executing the entire DAG and triggering observers on all fields.
    bool prev_cache_state = dag_executor_.is_cache_enabled();
    if (disable_cache) {
        const_cast<DAGExecutor&>(dag_executor_).set_cache_enabled(false);
    }
    
    const_cast<DAGExecutor&>(dag_executor_).execute_to_node(*dag_, node_id);
    
    // Restore previous cache state if it was changed
    if (disable_cache) {
        const_cast<DAGExecutor&>(dag_executor_).set_cache_enabled(prev_cache_state);
    }
    
    if (is_sink) {
        ORC_LOG_DEBUG("Executed inputs for sink node '{}' (sink's cached_input_ should now be populated)", node_id.to_string());
    } else {
        ORC_LOG_DEBUG("Executed DAG up to node '{}' - stage instance should have cached_output_ set", node_id.to_string());
    }
}

std::vector<PreviewOutputInfo> PreviewRenderer::get_stage_preview_outputs(
    const NodeID& stage_node_id,
    const DAGNode& stage_node,
    const PreviewableStage& previewable)
{
    (void)stage_node;  // Unused for now
    std::vector<PreviewOutputInfo> outputs;
    
    ORC_LOG_DEBUG("get_stage_preview_outputs called for node '{}'", stage_node_id.to_string());
    
    // Ensure the node has been executed so it has cached output
    // Use cached execution to avoid re-processing all fields through observers
    ensure_node_executed(stage_node_id, false);
    
    // Get options from the stage
    auto options = previewable.get_preview_options();
    
    if (options.empty()) {
        ORC_LOG_WARN("Stage node '{}' has no preview options after execution - cached output may be null", stage_node_id.to_string());
        auto node_type_info = stage_node.stage->get_node_type_info();
        ORC_LOG_WARN("Node '{}' is type '{}' ({})", stage_node_id.to_string(), node_type_info.stage_name, node_type_info.display_name);
        return outputs;
    }
    
    // Convert each option to a PreviewOutputInfo
    for (const auto& option : options) {
        // Infer the output type from the option ID
        PreviewOutputType type = PreviewOutputType::Frame;  // Default
        if (option.id == "field" || option.id == "field_raw") {
            type = PreviewOutputType::Field;
        } else if (option.id == "split" || option.id == "split_raw") {
            type = PreviewOutputType::Split;
        } else if (option.id == "frame" || option.id == "frame_raw") {
            type = PreviewOutputType::Frame;
        }
        
        // Check if this is a chroma decoder stage (chroma_sink)
        // Chroma decoder outputs RGB frames, not YUV fields, so dropouts are not available
        std::string stage_name = stage_node.stage->get_node_type_info().stage_name;
        bool is_chroma_decoder = (stage_name == "chroma_sink");
        
        // Check if the stage has separate Y/C channels (for YC sources)
        // We do this by rendering field 0 and checking has_separate_channels()
        bool has_separate_channels = false;
        if (auto* previewable_ptr = const_cast<PreviewableStage*>(&previewable)) {
            try {
                auto preview_img = previewable_ptr->render_preview(options[0].id, 0, PreviewNavigationHint::Random);
                // Now render field 0 to get the representation
                auto result = field_renderer_->render_field_at_node(stage_node_id, FieldID(0));
                if (result.representation) {
                    has_separate_channels = result.representation->has_separate_channels();
                }
            } catch (...) {
                // Failed to check - assume false
                has_separate_channels = false;
            }
        }
        
        // Determine first field offset for frame-based outputs by probing field 0 parity
        uint64_t first_field_offset = 0;
        if (type == PreviewOutputType::Frame || type == PreviewOutputType::Frame_Reversed || type == PreviewOutputType::Split) {
            auto probe_result = field_renderer_->render_field_at_node(stage_node_id, FieldID(0));
            if (probe_result.is_valid && probe_result.representation) {
                auto parity_hint = probe_result.representation->get_field_parity_hint(FieldID(0));
                if (parity_hint.has_value() && !parity_hint->is_first_field) {
                    first_field_offset = 1;
                }
            }
        }

        outputs.push_back(PreviewOutputInfo{
            type,
            option.display_name,
            option.count,
            true,  // If stage advertises it, it's available
            option.dar_aspect_correction,  // Use stage-provided DAR correction
            option.id,  // Store original option ID
            !is_chroma_decoder,  // Dropouts not available for chroma decoder (RGB output)
            has_separate_channels,  // YC sources have separate channels
            first_field_offset      // field offset for frame-based outputs
        });
    }
    
    ORC_LOG_DEBUG("Stage node '{}' has {} preview options", stage_node_id.to_string(), outputs.size());
    
    return outputs;
}

PreviewRenderResult PreviewRenderer::render_stage_preview(
    const NodeID& stage_node_id,
    const DAGNode& stage_node,
    const PreviewableStage& previewable,
    PreviewOutputType type,
    uint64_t index,
    const std::string& requested_option_id,
    PreviewNavigationHint hint)
{
    (void)stage_node;  // Unused for now
    ORC_LOG_DEBUG("render_stage_preview called for node '{}', type={}, index={}, option_id='{}', hint={}", 
                  stage_node_id.to_string(), static_cast<int>(type), index, requested_option_id,
                  (hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random"));
    
    PreviewRenderResult result;
    result.node_id = stage_node_id;
    result.output_type = type;
    result.output_index = index;
    result.success = false;
    
    // Ensure the node and its inputs have been executed so the stage has cached input data
    // Disable cache to force fresh execution with cached_output_ populated
    ensure_node_executed(stage_node_id, true);
    
    // Determine effective option ID (fallback if empty)
    std::string effective_option_id = requested_option_id;
    if (effective_option_id.empty()) {
        auto options = previewable.get_preview_options();
        if (!options.empty()) {
            // Prefer an option that matches the requested output type
            for (const auto& option : options) {
                if ((type == PreviewOutputType::Field && (option.id == "field" || option.id == "field_raw")) ||
                    (type == PreviewOutputType::Split && (option.id == "split" || option.id == "split_raw")) ||
                    (type == PreviewOutputType::Frame && (option.id == "frame" || option.id == "frame_raw")) ||
                    (type == PreviewOutputType::Frame_Reversed && (option.id == "frame" || option.id == "frame_raw"))) {
                    effective_option_id = option.id;
                    break;
                }
            }
            if (effective_option_id.empty()) {
                effective_option_id = options.front().id;
            }
        }
    }

    // Get preview image from the stage
    auto stage_result = previewable.render_preview(effective_option_id, index, hint);
    
    if (!stage_result.is_valid()) {
        result.image = create_placeholder_image(type, "Rendering failed");
        result.success = true;
        result.error_message = "Failed to render stage preview";

        // Log the failure to render
        ORC_LOG_DEBUG("Rendering failed for node '{}', type={}, index={}, option_id='{}'", 
                      stage_node_id.to_string(), static_cast<int>(type), index, effective_option_id);
        return result;
    }
    
    // Stage returned a valid image
    result.image = std::move(stage_result);
    result.success = true;
    
    // Render dropout highlighting onto the image if enabled
    if (result.success && result.image.is_valid()) {
        render_dropouts(result.image);
    }
    
    // Aspect ratio scaling removed from core; GUI handles display scaling
    
    return result;
}

} // namespace orc
