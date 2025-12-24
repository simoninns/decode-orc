/*
 * File:        preview_renderer.cpp
 * Module:      orc-core
 * Purpose:     Preview rendering implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
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
        static const uint8_t space[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        
        switch (ch) {
            case 'N': return N;
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

std::vector<PreviewOutputInfo> PreviewRenderer::get_available_outputs(const std::string& node_id) {
    std::vector<PreviewOutputInfo> outputs;
    
    // Special handling for placeholder node (no real content)
    if (node_id == "_no_preview") {
        // Provide all output types so user can switch between them
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Field,
            "Field",
            1,  // Single placeholder item
            true,
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            true,
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            true,
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            true,
            0.7,
            ""
        });
        return outputs;
    }
    
    if (!field_renderer_ || node_id.empty()) {
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
            if (previewable_stage && previewable_stage->supports_preview()) {
                // Stage supports preview - get outputs from stage options
                return get_stage_preview_outputs(node_id, *node_it, *previewable_stage);
            }
            
            auto node_type = node_it->stage->get_node_type_info().type;
            if (node_type == NodeType::SINK) {
                // Sink doesn't support preview - return empty (no preview available)
                ORC_LOG_DEBUG("Sink node '{}' does not support preview", node_id);
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
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
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
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,
            "Frame",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame_Reversed,
            "Frame (Reversed)",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
        });
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Split,
            "Split",
            1,  // Single placeholder item
            false,  // Not available - placeholder only
            0.7,
            ""
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
        ""
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
                ""
            });
            
            outputs.push_back(PreviewOutputInfo{
                PreviewOutputType::Frame_Reversed,
                "Frame (Reversed)",
                frame_count,
                true,
                0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
                ""
            });
            
            outputs.push_back(PreviewOutputInfo{
                PreviewOutputType::Split,
                "Split",
                frame_count,
                true,
                0.7,  // PAL/NTSC standard (accounts for horizontal blanking)
                ""
            });
        }
    }
    
    // TODO: Future output types
    // - Luma (luma component only)
    // - Chroma (requires chroma decoder)
    // - Composite (requires full signal reconstruction)
    
    return outputs;
}

uint64_t PreviewRenderer::get_output_count(const std::string& node_id, PreviewOutputType type) {
    auto outputs = get_available_outputs(node_id);
    
    for (const auto& output : outputs) {
        if (output.type == type) {
            return output.count;
        }
    }
    
    return 0;
}

PreviewRenderResult PreviewRenderer::render_output(
    const std::string& node_id,
    PreviewOutputType type,
    uint64_t index,
    const std::string& option_id)
{
    ORC_LOG_DEBUG("render_output: node='{}', type={}, option_id='{}', index={}",
                  node_id, static_cast<int>(type), option_id, index);
    
    PreviewRenderResult result;
    result.node_id = node_id;
    result.output_type = type;
    result.output_index = index;
    result.success = false;
    
    // Special handling for placeholder node - render "No source available" image
    if (node_id == "_no_preview") {
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
                return render_stage_preview(node_id, *node_it, *previewable_stage, type, index, option_id);
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
                bool first_field_first = (type == PreviewOutputType::Frame);
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
            uint8_t value = tbc_sample_to_8bit(sample);
            
            // Grayscale (R=G=B)
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
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
    // desc_b has same dimensions as desc_a in interlaced video
    
    // Get field data
    auto field_a_data = repr->get_field(field_a);
    auto field_b_data = repr->get_field(field_b);
    
    if (field_a_data.empty() || field_b_data.empty()) {
        return image;
    }
    
    // Frame is double height
    image.width = desc_a.width;
    image.height = desc_a.height * 2;
    image.rgb_data.resize(image.width * image.height * 3);
    
    // Weave fields together
    // If first_field_first: field_a on even lines, field_b on odd lines
    // If !first_field_first: field_b on even lines, field_a on odd lines
    
    for (size_t frame_y = 0; frame_y < image.height; ++frame_y) {
        bool use_field_a = first_field_first ? (frame_y % 2 == 0) : (frame_y % 2 != 0);
        const auto& field_data = use_field_a ? field_a_data : field_b_data;
        
        size_t field_y = frame_y / 2;
        size_t field_offset = field_y * image.width;
        size_t rgb_offset = frame_y * image.width * 3;
        
        for (size_t x = 0; x < image.width; ++x) {
            if (field_offset + x >= field_data.size()) {
                break;
            }
            
            uint16_t sample = field_data[field_offset + x];
            uint8_t value = tbc_sample_to_8bit(sample);
            
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
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
    
    // Split frame: stack fields vertically
    // Top half is field_a, bottom half is field_b
    image.width = desc_a.width;
    image.height = desc_a.height * 2;  // Double height for both fields
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
            uint8_t value = tbc_sample_to_8bit(sample);
            
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
            uint8_t value = tbc_sample_to_8bit(sample);
            
            image.rgb_data[rgb_offset + x * 3 + 0] = value; // R
            image.rgb_data[rgb_offset + x * 3 + 1] = value; // G
            image.rgb_data[rgb_offset + x * 3 + 2] = value; // B
        }
    }
    
    return image;
}

uint8_t PreviewRenderer::tbc_sample_to_8bit(uint16_t sample) {
    // Simple linear scaling from 16-bit to 8-bit
    // TODO: Could be improved with proper IRE level scaling from metadata
    // (black_16b_ire and white_16b_ire from capture table)
    
    return static_cast<uint8_t>((sample >> 8) & 0xFF);
}

PreviewImage PreviewRenderer::apply_aspect_ratio_scaling(const PreviewImage& input) const {
    // If SAR 1:1 mode or invalid image, return as-is
    if (aspect_ratio_mode_ == AspectRatioMode::SAR_1_1 || !input.is_valid()) {
        return input;
    }
    
    // DAR 4:3 mode - apply aspect correction (0.7 for PAL/NTSC)
    const double aspect_correction = 0.7;
    
    PreviewImage output;
    output.height = input.height;
    output.width = static_cast<size_t>(input.width * aspect_correction + 0.5);
    output.rgb_data.resize(output.width * output.height * 3);
    
    // Simple nearest-neighbor scaling
    for (size_t y = 0; y < output.height; ++y) {
        for (size_t x = 0; x < output.width; ++x) {
            // Map output pixel to input pixel
            size_t src_x = static_cast<size_t>(x / aspect_correction);
            if (src_x >= input.width) src_x = input.width - 1;
            
            size_t src_offset = (y * input.width + src_x) * 3;
            size_t dst_offset = (y * output.width + x) * 3;
            
            output.rgb_data[dst_offset + 0] = input.rgb_data[src_offset + 0];
            output.rgb_data[dst_offset + 1] = input.rgb_data[src_offset + 1];
            output.rgb_data[dst_offset + 2] = input.rgb_data[src_offset + 2];
        }
    }
    
    return output;
}

bool PreviewRenderer::save_png(
    const std::string& node_id,
    PreviewOutputType type,
    uint64_t index,
    const std::string& filename)
{
    // Render the output
    auto result = render_output(node_id, type, index);
    
    if (!result.success || !result.image.is_valid()) {
        ORC_LOG_ERROR("Failed to render output for PNG export: {}", result.error_message);
        return false;
    }
    
    // Apply aspect ratio scaling if needed
    PreviewImage scaled_image = apply_aspect_ratio_scaling(result.image);
    
    // Save to PNG
    return save_png(scaled_image, filename);
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

void PreviewRenderer::set_aspect_ratio_mode(AspectRatioMode mode) {
    aspect_ratio_mode_ = mode;
}

AspectRatioMode PreviewRenderer::get_aspect_ratio_mode() const {
    return aspect_ratio_mode_;
}

std::vector<AspectRatioModeInfo> PreviewRenderer::get_available_aspect_ratio_modes() const {
    return {
        { AspectRatioMode::SAR_1_1, "SAR 1:1", 1.0 },
        { AspectRatioMode::DAR_4_3, "DAR 4:3", 0.7 }
    };
}

AspectRatioModeInfo PreviewRenderer::get_current_aspect_ratio_mode_info() const {
    auto modes = get_available_aspect_ratio_modes();
    for (const auto& mode_info : modes) {
        if (mode_info.mode == aspect_ratio_mode_) {
            return mode_info;
        }
    }
    // Fallback (should never happen)
    return modes[0];
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
    
    // Convert 0-based index to 1-based for display
    info.current_number = index + 1;
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
        
        // Convert to 1-based for display
        info.first_field_number = first_field + 1;
        info.second_field_number = second_field + 1;
    }
    
    return info;
}

SuggestedViewNode PreviewRenderer::get_suggested_view_node() const {
    // Special placeholder node ID for when no real content is available
    const std::string PLACEHOLDER_NODE = "_no_preview";
    
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
                    "Viewing source: " + node.node_id
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
                    "Viewing node: " + node.node_id
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
                        "Viewing sink preview: " + node.node_id
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

void PreviewRenderer::ensure_node_executed(const std::string& node_id) const
{
    if (!dag_) {
        return;
    }
    
    // Execute the DAG up to this node to ensure it has cached output
    // The DAGExecutor caches results, so repeated calls are cheap
    try {
        const_cast<DAGExecutor&>(dag_executor_).execute_to_node(*dag_, node_id);
        ORC_LOG_DEBUG("Executed DAG up to node '{}' for preview", node_id);
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to execute node '{}' for preview: {}", node_id, e.what());
    }
}

std::vector<PreviewOutputInfo> PreviewRenderer::get_stage_preview_outputs(
    const std::string& stage_node_id,
    const DAGNode& stage_node,
    const PreviewableStage& previewable)
{
    std::vector<PreviewOutputInfo> outputs;
    
    ORC_LOG_DEBUG("get_stage_preview_outputs called for node '{}'", stage_node_id);
    
    // Ensure the node has been executed so it has cached output
    ensure_node_executed(stage_node_id);
    
    // Get options from the stage
    auto options = previewable.get_preview_options();
    
    if (options.empty()) {
        ORC_LOG_DEBUG("Stage node '{}' has no preview options", stage_node_id);
        return outputs;
    }
    
    // Convert each option to a PreviewOutputInfo
    for (const auto& option : options) {
        outputs.push_back(PreviewOutputInfo{
            PreviewOutputType::Frame,  // Stages output frames
            option.display_name,
            option.count,
            true,  // If stage advertises it, it's available
            0.7,   // Standard DAR correction
            option.id  // Store original option ID
        });
    }
    
    ORC_LOG_DEBUG("Stage node '{}' has {} preview options", stage_node_id, outputs.size());
    
    return outputs;
}

PreviewRenderResult PreviewRenderer::render_stage_preview(
    const std::string& stage_node_id,
    const DAGNode& stage_node,
    const PreviewableStage& previewable,
    PreviewOutputType type,
    uint64_t index,
    const std::string& requested_option_id)
{
    ORC_LOG_DEBUG("render_stage_preview called for node '{}', type={}, index={}, option_id='{}'", 
                  stage_node_id, static_cast<int>(type), index, requested_option_id);
    
    PreviewRenderResult result;
    result.node_id = stage_node_id;
    result.output_type = type;
    result.output_index = index;
    result.success = false;
    
    // Get preview image from the stage
    auto stage_result = previewable.render_preview(requested_option_id, index);
    
    if (!stage_result.is_valid()) {
        result.image = create_placeholder_image(type, "Rendering failed");
        result.success = true;
        result.error_message = "Failed to render stage preview";
        return result;
    }
    
    // Stage returned a valid image
    result.image = std::move(stage_result);
    result.success = true;
    
    return result;
}

} // namespace orc
