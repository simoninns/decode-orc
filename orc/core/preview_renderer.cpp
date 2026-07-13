/*
 * File:        preview_renderer.cpp
 * Module:      orc-core
 * Purpose:     Preview rendering implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "preview_renderer.h"

#include <orc/stage/colour_preview_conversion.h>
#include <orc/stage/colour_preview_provider.h>
#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/logging.h>
#include <orc/stage/preview_helpers.h>
#include <orc/stage/stage_custom_preview_renderer.h>
#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "dag_executor.h"
#include "plugin_safe_call.h"

namespace orc {

namespace {

bool is_signal_domain_type(VideoDataType type) {
  return type == VideoDataType::CompositeNTSC ||
         type == VideoDataType::CompositePAL ||
         type == VideoDataType::YC_NTSC || type == VideoDataType::YC_PAL;
}

bool has_colour_domain_type(const StagePreviewCapability& capability) {
  return std::any_of(capability.supported_data_types.begin(),
                     capability.supported_data_types.end(),
                     [](VideoDataType type) {
                       return type == VideoDataType::ColourNTSC ||
                              type == VideoDataType::ColourPAL;
                     });
}

bool has_signal_domain_type(const StagePreviewCapability& capability) {
  return std::any_of(
      capability.supported_data_types.begin(),
      capability.supported_data_types.end(),
      [](VideoDataType type) { return is_signal_domain_type(type); });
}

}  // namespace

// Helper function to create a placeholder image with text
static PreviewImage create_placeholder_image(PreviewOutputType type,
                                             const char* message) {
  PreviewImage placeholder;
  placeholder.width = 1135;

  // Height depends on output type
  const bool is_full_frame_type =
      (type == PreviewOutputType::Frame_Field1_First ||
       type == PreviewOutputType::Frame_Reversed ||
       type == PreviewOutputType::Split);
  if (is_full_frame_type) {
    placeholder.height = 625;  // Full PAL frame height
  } else {
    placeholder.height = 313;  // Single field height
  }

  placeholder.rgb_data.resize(static_cast<size_t>(placeholder.width) *
                              placeholder.height * 3);

  // Fill with black background
  for (size_t i = 0; i < placeholder.rgb_data.size(); ++i) {
    placeholder.rgb_data[i] = 0;  // Black
  }

  // Draw message text in white
  // Simple 8x8 bitmap font for the message
  const size_t base_char_width = 8;
  const size_t base_char_height = 8;

  // Scale text larger for frame rendering (2x scale)
  const size_t scale = is_full_frame_type ? 2 : 1;
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
    static const uint8_t space[] = {0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00};

    switch (ch) {
      case 'N':
        return N;
      case 'R':
        return R;
      case 'o':
        return o;
      case 's':
        return s;
      case 'u':
        return u;
      case 'r':
        return r;
      case 'c':
        return c;
      case 'e':
        return e;
      case 'a':
        return a;
      case 'v':
        return v;
      case 'i':
        return i;
      case 'l':
        return l;
      case 'b':
        return b;
      case 't':
        return t;
      case 'h':
        return h;
      case 'g':
        return g;
      case 'p':
        return p;
      case 'n':
        return n;
      case 'd':
        return d;
      case 'f':
        return f;
      case ' ':
      default:
        return space;
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

static PreviewImage scale_image_horizontal_for_export(const PreviewImage& image,
                                                      double correction) {
  if (!image.is_valid()) {
    return image;
  }

  if (correction <= 0.0 || std::abs(correction - 1.0) < 1e-6) {
    return image;
  }

  const uint32_t src_width = image.width;
  const uint32_t src_height = image.height;
  const uint32_t dst_width = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::lround(src_width * correction)));

  if (dst_width == src_width) {
    return image;
  }

  PreviewImage scaled;
  scaled.width = dst_width;
  scaled.height = src_height;
  scaled.rgb_data.resize(static_cast<size_t>(dst_width) * src_height * 3);
  scaled.vectorscope_data = image.vectorscope_data;

  for (uint32_t y = 0; y < src_height; ++y) {
    for (uint32_t x = 0; x < dst_width; ++x) {
      uint32_t src_x = static_cast<uint32_t>(
          std::floor(static_cast<double>(x) / correction));
      src_x = std::min(src_x, src_width - 1);

      const size_t src_offset =
          (static_cast<size_t>(y) * src_width + src_x) * 3;
      const size_t dst_offset = (static_cast<size_t>(y) * dst_width + x) * 3;

      scaled.rgb_data[dst_offset + 0] = image.rgb_data[src_offset + 0];
      scaled.rgb_data[dst_offset + 1] = image.rgb_data[src_offset + 1];
      scaled.rgb_data[dst_offset + 2] = image.rgb_data[src_offset + 2];
    }
  }

  scaled.dropout_regions = image.dropout_regions;
  for (auto& region : scaled.dropout_regions) {
    region.start_sample = std::min<uint32_t>(
        dst_width,
        static_cast<uint32_t>(std::lround(region.start_sample * correction)));
    region.end_sample = std::min<uint32_t>(
        dst_width,
        static_cast<uint32_t>(std::lround(region.end_sample * correction)));
  }

  return scaled;
}

PreviewRenderer::PreviewRenderer(std::shared_ptr<const DAG> dag) : dag_(dag) {
  if (dag_) {
    frame_renderer_ = std::make_unique<DAGFrameRenderer>(dag_);
  }
}

std::vector<PreviewOutputInfo> PreviewRenderer::get_available_outputs(
    const NodeID& node_id) {
  std::vector<PreviewOutputInfo> outputs;

  // Special handling for placeholder node (no real content)
  if (node_id.to_string() == "_no_preview") {
    // Provide all output types so user can switch between them
    outputs.push_back(PreviewOutputInfo{
        PreviewOutputType::Frame_Field1, "Field 1",
        1,  // Single placeholder item
        true, 0.7, "",
        false,  // No dropouts for placeholder
        false,  // No separate channels for placeholder
        0       // first_field_offset
    });
    outputs.push_back(PreviewOutputInfo{
        PreviewOutputType::Frame_Field1_First, "Frame",
        1,  // Single placeholder item
        true, 0.7, "",
        false,  // No dropouts for placeholder
        false,  // No separate channels for placeholder
        0       // first_field_offset
    });
    outputs.push_back(PreviewOutputInfo{
        PreviewOutputType::Frame_Reversed, "Frame (Reversed)",
        1,  // Single placeholder item
        true, 0.7, "",
        false,  // No dropouts for placeholder
        false,  // No separate channels for placeholder
        0       // first_field_offset
    });
    outputs.push_back(PreviewOutputInfo{
        PreviewOutputType::Split, "Split",
        1,  // Single placeholder item
        true, 0.7, "",
        false,  // No dropouts for placeholder
        false,  // No separate channels for placeholder
        0       // first_field_offset
    });
    return outputs;
  }

  if (!frame_renderer_ || !node_id.is_valid()) {
    return outputs;
  }

  // Check if this is a previewable stage or sink node
  if (dag_) {
    const auto& dag_nodes = dag_->nodes();
    auto node_it = std::find_if(
        dag_nodes.begin(), dag_nodes.end(),
        [&node_id](const auto& n) { return n.node_id == node_id; });

    if (node_it != dag_nodes.end() && node_it->stage) {
      const auto node_type = node_it->stage->get_node_type_info().type;
      auto* capability_stage =
          dynamic_cast<const IStagePreviewCapability*>(node_it->stage.get());

      if (capability_stage) {
        ensure_node_executed(node_id, false);
        StagePreviewCapability capability =
            capability_stage->get_preview_capability();

        if (!capability.is_valid()) {
          ensure_node_executed(node_id, true);
          capability = capability_stage->get_preview_capability();
        }

        if (capability.is_valid()) {
          auto* colour_provider =
              dynamic_cast<const IColourPreviewProvider*>(node_it->stage.get());

          if (has_colour_domain_type(capability) && colour_provider) {
            return get_capability_preview_outputs(capability);
          }

          // Signal-domain: render from the VFR produced by the DAG executor.
          if (has_signal_domain_type(capability) && frame_renderer_) {
            auto vfr_result = frame_renderer_->render_frame_at_node(
                node_id, static_cast<FrameID>(0));
            if (vfr_result.is_valid && vfr_result.representation) {
              auto options = PreviewHelpers::get_standard_preview_options(
                  vfr_result.representation);
              return build_outputs_from_options(node_id, *node_it, options);
            }
          }
        }
      } else if (auto* custom =
                     dynamic_cast<const IStageCustomPreviewRenderer*>(
                         node_it->stage.get())) {
        // Custom rendering path for multi-output stages (e.g. SourceAlignStage)
        // that cannot expose their preview through the standard VFR mechanism.
        ensure_node_executed(node_id, false);
        auto options = custom->get_preview_options();
        return build_outputs_from_options(node_id, *node_it, options);
      }

      // Fallback: stages that do not expose their own preview functionality
      // (or whose capability is not yet valid) default to the SDK preview
      // helper.  Render a pass-through preview from the VFR available at this
      // node — for sink nodes the frame renderer transparently substitutes the
      // upstream node's output, so no per-plugin boilerplate is required.
      if (frame_renderer_) {
        auto vfr_result = frame_renderer_->render_frame_at_node(
            node_id, static_cast<FrameID>(0));
        if (vfr_result.is_valid && vfr_result.representation) {
          auto options = PreviewHelpers::get_standard_preview_options(
              vfr_result.representation);
          return build_outputs_from_options(node_id, *node_it, options);
        }
      }

      if (node_type == NodeType::SINK) {
        ORC_LOG_DEBUG("Sink node '{}' has no VFR available for preview",
                      node_id.to_string());
        return outputs;
      }
    }
  }

  return outputs;
}

uint64_t PreviewRenderer::get_output_count(const NodeID& node_id,
                                           PreviewOutputType type) {
  auto outputs = get_available_outputs(node_id);

  for (const auto& output : outputs) {
    if (output.type == type) {
      return output.count;
    }
  }

  return 0;
}

PreviewRenderResult PreviewRenderer::render_output(const NodeID& node_id,
                                                   PreviewOutputType type,
                                                   uint64_t index,
                                                   const std::string& option_id,
                                                   PreviewNavigationHint hint) {
  ORC_LOG_DEBUG(
      "render_output: node='{}', type={}, option_id='{}', index={}, hint={}",
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
    auto node_it = std::find_if(
        dag_nodes.begin(), dag_nodes.end(),
        [&node_id](const auto& n) { return n.node_id == node_id; });

    if (node_it != dag_nodes.end() && node_it->stage) {
      if (auto* capability_stage = dynamic_cast<const IStagePreviewCapability*>(
              node_it->stage.get())) {
        ensure_node_executed(node_id, true);
        const StagePreviewCapability capability =
            capability_stage->get_preview_capability();

        if (capability.is_valid()) {
          // Colour-domain stages use the carrier-backed rendering path.
          if (has_colour_domain_type(capability)) {
            if (auto* colour_provider =
                    dynamic_cast<const IColourPreviewProvider*>(
                        node_it->stage.get())) {
              return render_colour_carrier_preview(
                  node_id, *colour_provider, capability, type, index, hint);
            }
          }

          // Signal-domain stages: render directly from the DAG VFR output.
          if (has_signal_domain_type(capability) && frame_renderer_) {
            auto vfr_result = frame_renderer_->render_frame_at_node(
                node_id, static_cast<FrameID>(0));
            if (vfr_result.is_valid && vfr_result.representation) {
              result.image = PreviewHelpers::render_standard_preview(
                  vfr_result.representation, option_id, index, hint,
                  capability.geometry.mask_inactive_area);
              result.success = result.image.is_valid();
              if (result.success) render_dropouts(result.image);
              return result;
            }
          }
        }
      } else if (auto* custom =
                     dynamic_cast<const IStageCustomPreviewRenderer*>(
                         node_it->stage.get())) {
        // Custom rendering path for multi-output stages (e.g.
        // SourceAlignStage).
        ensure_node_executed(node_id, true);
        result.image = custom->render_preview(option_id, index, hint);
        result.success = result.image.is_valid();
        if (result.success) render_dropouts(result.image);
        return result;
      }

      // Fallback: stages without their own preview functionality default to the
      // SDK preview helper.  Render a pass-through preview from the VFR
      // available at this node (for sink nodes the frame renderer substitutes
      // the upstream node's output).
      if (frame_renderer_) {
        auto vfr_result = frame_renderer_->render_frame_at_node(
            node_id, static_cast<FrameID>(0));
        if (vfr_result.is_valid && vfr_result.representation) {
          result.image = PreviewHelpers::render_standard_preview(
              vfr_result.representation, option_id, index, hint);
          result.success = result.image.is_valid();
          if (result.success) render_dropouts(result.image);
          return result;
        }
      }
    }
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
  first_field_offset_cache_.clear();

  if (dag_) {
    frame_renderer_ = std::make_unique<DAGFrameRenderer>(dag_);
  } else {
    frame_renderer_.reset();
  }
}

namespace {

// Returns the number of lines in field 1 for the given video system.
inline size_t field1_line_count(VideoSystem sys) {
  return (sys == VideoSystem::PAL) ? static_cast<size_t>(kPalField1Lines)
                                   : static_cast<size_t>(kNtscField1Lines);
}

}  // namespace

bool PreviewRenderer::save_png(const NodeID& node_id, PreviewOutputType type,
                               uint64_t index, const std::string& filename,
                               const std::string& option_id,
                               double aspect_correction) {
  // Render output at sample aspect ratio; optional export correction is applied
  // below.
  auto result = render_output(node_id, type, index, option_id);

  if (!result.success || !result.image.is_valid()) {
    ORC_LOG_ERROR("Failed to render output for PNG export: {}",
                  result.error_message);
    return false;
  }

  const PreviewImage export_image =
      scale_image_horizontal_for_export(result.image, aspect_correction);
  return save_png(export_image, filename);
}

bool PreviewRenderer::save_png(const PreviewImage& image,
                               const std::string& filename) {
  if (!image.is_valid()) {
    ORC_LOG_ERROR("Invalid image for PNG export");
    return false;
  }

  FILE* fp = nullptr;
#ifdef _WIN32
  if (fopen_s(&fp, filename.c_str(), "wb") != 0) {
    fp = nullptr;
  }
#else
  fp = fopen(filename.c_str(), "wb");
#endif
  if (!fp) {
    ORC_LOG_ERROR("Failed to open file for writing: {}", filename);
    return false;
  }

  // Create PNG structures
  png_structp png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
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
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    ORC_LOG_ERROR("PNG write error");
    return false;
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif

  png_init_io(png, fp);

  // Set image attributes
  png_set_IHDR(png, info, image.width, image.height,
               8,                   // 8 bits per channel
               PNG_COLOR_TYPE_RGB,  // RGB color type
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  // Write image data row by row
  for (uint32_t y = 0; y < image.height; ++y) {
    png_bytep row = const_cast<png_bytep>(
        &image.rgb_data[static_cast<size_t>(y) * image.width * 3]);
    png_write_row(png, row);
  }

  png_write_end(png, nullptr);

  // Cleanup
  png_destroy_write_struct(&png, &info);
  fclose(fp);

  ORC_LOG_DEBUG("Saved PNG: {} ({}x{})", filename, image.width, image.height);
  return true;
}

void PreviewRenderer::set_show_dropouts(bool show) { show_dropouts_ = show; }

bool PreviewRenderer::get_show_dropouts() const { return show_dropouts_; }

VideoFrameRepresentationPtr PreviewRenderer::get_representation_at_node(
    const NodeID& node_id) const {
  if (!dag_ || !frame_renderer_) {
    return nullptr;
  }

  std::unordered_map<NodeID, const DAGNode*> node_index;
  node_index.reserve(dag_->nodes().size());
  for (const auto& node : dag_->nodes()) {
    node_index.emplace(node.node_id, &node);
  }

  std::queue<NodeID> pending;
  std::unordered_set<NodeID> visited;
  pending.push(node_id);

  while (!pending.empty()) {
    const NodeID current = pending.front();
    pending.pop();

    if (!visited.insert(current).second) {
      continue;
    }

    auto node_it = node_index.find(current);
    if (node_it == node_index.end()) {
      continue;
    }

    ensure_node_executed(current);

    auto result =
        frame_renderer_->render_frame_at_node(current, static_cast<FrameID>(0));
    if (result.is_valid && result.representation) {
      if (current != node_id) {
        ORC_LOG_DEBUG(
            "get_representation_at_node: falling back from '{}' to upstream "
            "node '{}'",
            node_id.to_string(), current.to_string());
      }
      return result.representation;
    }

    for (const auto& input_node_id : node_it->second->input_node_ids) {
      pending.push(input_node_id);
    }
  }

  return nullptr;
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
    uint32_t start_x =
        std::min(region.start_sample, static_cast<uint32_t>(image.width));
    uint32_t end_x =
        std::min(region.end_sample, static_cast<uint32_t>(image.width));

    if (start_x >= end_x) {
      continue;
    }

    // Draw horizontal line at this scanline
    size_t row_offset = static_cast<size_t>(region.line) * image.width * 3;
    for (uint32_t x = start_x; x < end_x; ++x) {
      size_t pixel_offset = row_offset + static_cast<size_t>(x) * 3;
      if (pixel_offset + 2 < image.rgb_data.size()) {
        // Blend with red (75% red, 25% original)
        image.rgb_data[pixel_offset + 0] = static_cast<uint8_t>(
            image.rgb_data[pixel_offset + 0] * 0.25 + 255 * 0.75);  // R
        image.rgb_data[pixel_offset + 1] =
            static_cast<uint8_t>(image.rgb_data[pixel_offset + 1] * 0.25);  // G
        image.rgb_data[pixel_offset + 2] =
            static_cast<uint8_t>(image.rgb_data[pixel_offset + 2] * 0.25);  // B
      }
    }
  }
}

uint64_t PreviewRenderer::get_equivalent_index(
    PreviewOutputType /*from_type*/, uint64_t from_index,
    PreviewOutputType /*to_type*/) const {
  // Frame and field indices are now both per-frame counts; no conversion
  // needed.
  return from_index;
}

std::string PreviewRenderer::get_preview_item_label(
    PreviewOutputType type, uint64_t index, uint64_t total_count) const {
  // Get display name for this output type
  std::string type_name;
  switch (type) {
    case PreviewOutputType::Frame_Field1:
      type_name = "Field 1";
      break;
    case PreviewOutputType::Frame_Field2:
      type_name = "Field 2";
      break;
    case PreviewOutputType::Frame_Field1_First:
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

  bool is_single_field = (type == PreviewOutputType::Frame_Field1 ||
                          type == PreviewOutputType::Frame_Field2 ||
                          type == PreviewOutputType::Luma);

  if (is_single_field) {
    return type_name + " " + std::to_string(display_index) + " / " +
           std::to_string(total_count);
  } else {
    // Frame-based views: show frame number with constituent field numbers.
    // In the VFR domain each frame contains field1 + field2; sequential
    // field numbers are frame*2 and frame*2+1.
    uint64_t first_field_display = index * 2 + 1;
    uint64_t second_field_display = index * 2 + 2;

    std::string label = type_name + " " + std::to_string(display_index);

    if (type == PreviewOutputType::Frame_Reversed) {
      label += " (" + std::to_string(second_field_display) + "-" +
               std::to_string(first_field_display) + ")";
    } else if (type == PreviewOutputType::Frame_Field1_First ||
               type == PreviewOutputType::Split) {
      label += " (" + std::to_string(first_field_display) + "-" +
               std::to_string(second_field_display) + ")";
    }

    label += " / " + std::to_string(total_count);

    return label;
  }
}

PreviewItemDisplayInfo PreviewRenderer::get_preview_item_display_info(
    PreviewOutputType type, uint64_t index, uint64_t total_count) const {
  PreviewItemDisplayInfo info;

  // Get display name for this output type
  switch (type) {
    case PreviewOutputType::Frame_Field1:
      info.type_name = "Field 1";
      break;
    case PreviewOutputType::Frame_Field2:
      info.type_name = "Field 2";
      break;
    case PreviewOutputType::Frame_Field1_First:
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

  bool is_single_field = (type == PreviewOutputType::Frame_Field1 ||
                          type == PreviewOutputType::Frame_Field2 ||
                          type == PreviewOutputType::Luma);

  if (is_single_field) {
    // Single-field view: no constituent field pair info
    info.has_field_info = false;
    info.first_field_number = 0;
    info.second_field_number = 0;
  } else {
    // Frame-based views: constituent fields are frame*2 and frame*2+1
    info.has_field_info = true;
    info.first_field_number = index * 2;
    info.second_field_number = index * 2 + 1;
  }

  return info;
}

FrameLineNavigationResult PreviewRenderer::navigate_frame_line(
    const NodeID& node_id, PreviewOutputType output_type,
    uint64_t current_field, int current_line, int direction,
    int field_height  // Note: Not used - we check actual field heights instead
) const {
  (void)field_height;  // Suppress unused parameter warning
  FrameLineNavigationResult result;
  result.is_valid = false;
  result.new_field_index = current_field;
  result.new_line_number = current_line;

  // Only valid for interlaced frame modes
  if (output_type != PreviewOutputType::Frame_Field1_First &&
      output_type != PreviewOutputType::Frame_Reversed) {
    ORC_LOG_DEBUG(
        "navigate_frame_line: Invalid output type (must be Frame_Field1_First "
        "or Frame_Reversed)");
    return result;
  }

  // In the VFR domain, current_field encodes a sequential field index:
  //   field 0 = frame 0 / field1;  field 1 = frame 0 / field2
  //   field 2 = frame 1 / field1;  field 3 = frame 1 / field2, etc.
  // Determine which frame and which half we are in.
  FrameID frame_id = current_field / 2;
  bool current_is_field1 = (current_field % 2 == 0);

  auto frame_result = frame_renderer_->render_frame_at_node(node_id, frame_id);
  if (!frame_result.is_valid || !frame_result.representation) {
    ORC_LOG_DEBUG("navigate_frame_line: Frame {} not available", frame_id);
    return result;
  }

  auto desc = frame_result.representation->get_frame_descriptor(frame_id);
  if (!desc) {
    ORC_LOG_DEBUG("navigate_frame_line: No descriptor for frame {}", frame_id);
    return result;
  }

  size_t f1_lines = field1_line_count(desc->system);
  size_t f2_lines = desc->height - f1_lines;
  size_t total_frames = frame_result.representation->frame_count();

  // In Frame_Reversed mode the visual top is field2, so "first" is reversed.
  bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);
  bool current_is_top_field =
      is_reversed ? !current_is_field1 : current_is_field1;

  size_t current_field_height = current_is_field1 ? f1_lines : f2_lines;

  uint64_t new_field = current_field;
  int new_line = current_line;

  ORC_LOG_DEBUG(
      "navigate_frame_line: frame={} current_is_field1={} current_is_top={} "
      "f1_lines={} f2_lines={} current_line={}",
      frame_id, current_is_field1, current_is_top_field, f1_lines, f2_lines,
      current_line);

  if (direction > 0) {
    // Moving down
    if (current_is_top_field) {
      // On top field → move to same line of bottom field
      new_field = current_is_field1 ? current_field + 1 : current_field - 1;
      new_line = current_line;
    } else {
      // On bottom field
      // PAL field2 has fewer lines (f2_lines < f1_lines); the last visible
      // line in field2 is f2_lines-1.  After that we step to the extra line
      // in field1 (line f1_lines-1).
      if (static_cast<size_t>(current_line) >= current_field_height - 1) {
        ORC_LOG_DEBUG(
            "navigate_frame_line: At last line of bottom field, can't go "
            "further down");
        return result;
      }
      // Check if the next bottom-field line would be the last one and field1
      // still has an extra line beyond it.
      if (f1_lines > f2_lines &&
          static_cast<size_t>(current_line) + 1 >= f2_lines) {
        // Next step is the extra line in the top field (f1 line f1_lines-1)
        new_field = current_is_field1 ? current_field : current_field - 1;
        // field1 last line = f1_lines - 1
        new_line = static_cast<int>(f1_lines) - 1;
      } else {
        // Normal alternation: back to top field at next line number
        new_field = current_is_field1 ? current_field : current_field - 1;
        new_line = current_line + 1;
      }
    }
  } else if (direction < 0) {
    // Moving up
    if (current_is_top_field) {
      if (current_line <= 0) {
        ORC_LOG_DEBUG(
            "navigate_frame_line: At first line of top field, can't go up");
        return result;
      }
      // Move to same line of bottom field, one line up
      new_field = current_is_field1 ? current_field + 1 : current_field - 1;
      new_line = current_line - 1;
    } else {
      // On bottom field
      // Special case: if we're at the extra line (field1 line f1_lines-1 in
      // a PAL frame where f1_lines > f2_lines), the previous line is f2_lines-1
      // in this same bottom field.
      if (f1_lines > f2_lines && !current_is_field1 &&
          static_cast<size_t>(current_line) >= f2_lines) {
        new_field = current_field;
        new_line = static_cast<int>(f2_lines) - 1;
      } else {
        // Normal: back to top field at same line
        new_field = current_is_field1 ? current_field : current_field - 1;
        new_line = current_line;
      }
    }
  }

  // Bounds check: validate the new sequential field is within the VFR range
  FrameID new_frame_id = new_field / 2;
  if (new_frame_id >= total_frames) {
    ORC_LOG_DEBUG("navigate_frame_line: Frame {} out of range (total={})",
                  new_frame_id, total_frames);
    return result;
  }

  bool new_is_field1 = (new_field % 2 == 0);
  size_t new_field_height = new_is_field1 ? f1_lines : f2_lines;
  if (new_line < 0 || static_cast<size_t>(new_line) >= new_field_height) {
    ORC_LOG_DEBUG(
        "navigate_frame_line: Out of bounds - line {} (field height={})",
        new_line, new_field_height);
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
    const NodeID& node_id, PreviewOutputType output_type, uint64_t output_index,
    int image_y, int image_height) const {
  ImageToFieldMappingResult result;
  result.is_valid = false;
  result.field_index = 0;
  result.field_line = 0;

  ORC_LOG_DEBUG(
      "map_image_to_field: node='{}' output_type={} output_index={} image_y={} "
      "image_height={}",
      node_id.to_string(), static_cast<int>(output_type), output_index, image_y,
      image_height);

  auto repr = get_representation_at_node(node_id);
  if (!repr) {
    ORC_LOG_DEBUG(
        "map_image_to_field: no representation available for node='{}'",
        node_id.to_string());
    return result;
  }

  // Get frame geometry from VFR
  FrameID frame_id(output_index);
  auto desc = repr->get_frame_descriptor(frame_id);
  // If the frame is not found, fall back to the first frame for geometry
  if (!desc) {
    desc = repr->get_frame_descriptor(static_cast<FrameID>(0));
  }
  size_t f1_lines = desc ? field1_line_count(desc->system) : 313;
  size_t f2_lines = desc ? (desc->height - f1_lines) : 312;

  if (output_type == PreviewOutputType::Frame_Field1) {
    // Flat single-field display: image_y maps directly to line within field1
    if (image_y < 0 || static_cast<size_t>(image_y) >= f1_lines) {
      return result;
    }
    result.is_valid = true;
    result.field_index = output_index * 2;  // Sequential field1 index
    result.field_line = image_y;
    ORC_LOG_DEBUG(
        "map_image_to_field: mapped to field={} line={} (Frame_Field1 mode)",
        result.field_index, result.field_line);
    return result;
  }

  if (output_type == PreviewOutputType::Frame_Field2) {
    // Flat single-field display: image_y maps directly to line within field2
    if (image_y < 0 || static_cast<size_t>(image_y) >= f2_lines) {
      return result;
    }
    result.is_valid = true;
    result.field_index = output_index * 2 + 1;  // Sequential field2 index
    result.field_line = image_y;
    ORC_LOG_DEBUG(
        "map_image_to_field: mapped to field={} line={} (Frame_Field2 mode)",
        result.field_index, result.field_line);
    return result;
  }

  if (output_type == PreviewOutputType::Frame_Field1_First ||
      output_type == PreviewOutputType::Frame_Reversed) {
    // Interlaced frame: even lines come from field1 (Frame_Field1_First) or
    // field2 (Frame_Reversed); odd lines from the other field.
    bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);
    bool field1_on_even = !is_reversed;

    bool is_even_line = (image_y % 2) == 0;
    bool use_field1 = (is_even_line == field1_on_even);

    size_t target_field_height = use_field1 ? f1_lines : f2_lines;
    int tentative_field_line = image_y / 2;

    // Clamp to available field height
    if (tentative_field_line < 0 ||
        static_cast<size_t>(tentative_field_line) >= target_field_height) {
      // Out of bounds for chosen field — try the other
      use_field1 = !use_field1;
      target_field_height = use_field1 ? f1_lines : f2_lines;
      if (static_cast<size_t>(tentative_field_line) >= target_field_height) {
        return result;
      }
    }

    result.field_index =
        use_field1 ? (output_index * 2) : (output_index * 2 + 1);
    result.field_line = tentative_field_line;

    // Validate the final mapping against VFR-derived field heights
    size_t target_height = (result.field_index % 2 == 0) ? f1_lines : f2_lines;
    if (result.field_line < 0 ||
        static_cast<size_t>(result.field_line) >= target_height) {
      return result;  // Line out of bounds for this field
    }

    result.is_valid = true;
    ORC_LOG_DEBUG("map_image_to_field: mapped to field={} line={} (frame mode)",
                  result.field_index, result.field_line);
    return result;
  }

  if (output_type == PreviewOutputType::Split) {
    // Split mode: top half is field1 (flat, VFR line 0..f1_lines-1),
    // bottom half is field2 (VFR line f1_lines..height-1).
    if (image_y < static_cast<int>(f1_lines)) {
      result.field_index = output_index * 2;
      result.field_line = image_y;
      if (result.field_line < 0) return result;
    } else {
      result.field_index = output_index * 2 + 1;
      result.field_line = image_y - static_cast<int>(f1_lines);
      if (result.field_line < 0 ||
          static_cast<size_t>(result.field_line) >= f2_lines) {
        return result;
      }
    }

    result.is_valid = true;
    ORC_LOG_DEBUG("map_image_to_field: mapped to field={} line={} (split mode)",
                  result.field_index, result.field_line);
    return result;
  }

  // Unsupported output type
  return result;
}

FieldToImageMappingResult PreviewRenderer::map_field_to_image(
    const NodeID& node_id, PreviewOutputType output_type, uint64_t output_index,
    uint64_t field_index, int field_line, int image_height) const {
  FieldToImageMappingResult result;
  result.is_valid = false;
  result.image_y = 0;

  ORC_LOG_DEBUG(
      "map_field_to_image: node='{}' output_type={} output_index={} "
      "field_index={} field_line={} image_height={}",
      node_id.to_string(), static_cast<int>(output_type), output_index,
      field_index, field_line, image_height);

  auto repr = get_representation_at_node(node_id);
  if (!repr) {
    ORC_LOG_DEBUG(
        "map_field_to_image: no representation available for node='{}'",
        node_id.to_string());
    return result;
  }

  // Get VFR geometry for field height calculations
  FrameID frame_id = output_index;
  auto desc = repr->get_frame_descriptor(frame_id);
  if (!desc) desc = repr->get_frame_descriptor(static_cast<FrameID>(0));
  size_t f1_lines = desc ? field1_line_count(desc->system) : 313;

  if (output_type == PreviewOutputType::Frame_Field1) {
    // Flat field1 view: field_line is the image_y directly
    result.is_valid = true;
    result.image_y = field_line;
    ORC_LOG_DEBUG(
        "map_field_to_image: mapped to image_y={} (Frame_Field1 mode)",
        result.image_y);
    return result;
  }

  if (output_type == PreviewOutputType::Frame_Field2) {
    // Flat field2 view: field_line is the image_y directly
    result.is_valid = true;
    result.image_y = field_line;
    ORC_LOG_DEBUG(
        "map_field_to_image: mapped to image_y={} (Frame_Field2 mode)",
        result.image_y);
    return result;
  }

  if (output_type == PreviewOutputType::Frame_Field1_First ||
      output_type == PreviewOutputType::Frame_Reversed) {
    // Interlaced frame: field1 on even lines (Frame_Field1_First) or odd
    // lines (Frame_Reversed).
    bool is_reversed = (output_type == PreviewOutputType::Frame_Reversed);
    bool field1_on_even = !is_reversed;

    // Determine which sequential half this field_index belongs to
    uint64_t frame_field1_index = output_index * 2;
    uint64_t frame_field2_index = output_index * 2 + 1;

    if (field_index == frame_field1_index) {
      result.image_y = field1_on_even ? (field_line * 2) : (field_line * 2 + 1);
    } else if (field_index == frame_field2_index) {
      result.image_y = field1_on_even ? (field_line * 2 + 1) : (field_line * 2);
    } else {
      return result;
    }
    result.is_valid = true;
    ORC_LOG_DEBUG("map_field_to_image: mapped to image_y={} (frame mode)",
                  result.image_y);
    return result;
  }

  if (output_type == PreviewOutputType::Split) {
    // Split mode: top half is field1 (f1_lines rows), bottom half is field2.
    if (field_index == output_index * 2) {
      result.image_y = field_line;
    } else if (field_index == output_index * 2 + 1) {
      result.image_y = field_line + static_cast<int>(f1_lines);
    } else {
      return result;
    }
    result.is_valid = true;
    ORC_LOG_DEBUG("map_field_to_image: mapped to image_y={} (split mode)",
                  result.image_y);
    return result;
  }

  // Unsupported output type
  return result;
}

FrameFieldsResult PreviewRenderer::get_frame_fields(
    const NodeID& node_id, uint64_t frame_index) const {
  FrameFieldsResult result;
  result.is_valid = false;
  result.first_field = 0;
  result.second_field = 0;

  // In VFR domain, field1 of frame N is at sequential index N*2, field2 at
  // N*2+1.
  result.first_field = frame_index * 2;
  result.second_field = frame_index * 2 + 1;
  result.is_valid = true;

  ORC_LOG_DEBUG("get_frame_fields: node='{}' frame={} -> first={} second={}",
                node_id.to_string(), frame_index, result.first_field,
                result.second_field);
  return result;
}

SuggestedViewNode PreviewRenderer::get_suggested_view_node() const {
  // Special placeholder node ID for when no real content is available
  const NodeID PLACEHOLDER_NODE =
      NodeID(-999);  // Use special negative ID for placeholder

  if (!dag_) {
    return SuggestedViewNode{PLACEHOLDER_NODE, false, "No DAG available"};
  }

  const auto& dag_nodes = dag_->nodes();
  if (dag_nodes.empty()) {
    return SuggestedViewNode{
        PLACEHOLDER_NODE, false,
        "Project has no processing nodes - add nodes in the DAG Editor"};
  }

  // Priority 1: First SOURCE node
  for (const auto& node : dag_nodes) {
    if (node.stage) {
      auto node_type_info = node.stage->get_node_type_info();
      if (node_type_info.type == NodeType::SOURCE) {
        return SuggestedViewNode{
            node.node_id, true,
            fmt::format("Viewing source: {}", node.node_id)};
      }
    }
  }

  // Priority 2: First node with outputs (not a SINK)
  for (const auto& node : dag_nodes) {
    if (node.stage) {
      auto node_type_info = node.stage->get_node_type_info();
      if (node_type_info.type != NodeType::SINK) {
        return SuggestedViewNode{node.node_id, true,
                                 fmt::format("Viewing node: {}", node.node_id)};
      }
    }
  }

  // Priority 3: First SINK node that declares preview capability
  for (const auto& node : dag_nodes) {
    if (node.stage) {
      auto node_type_info = node.stage->get_node_type_info();
      if (node_type_info.type == NodeType::SINK) {
        auto* capability_stage =
            dynamic_cast<const IStagePreviewCapability*>(node.stage.get());
        if (capability_stage &&
            capability_stage->get_preview_capability().is_valid()) {
          return SuggestedViewNode{
              node.node_id, true,
              fmt::format("Viewing sink preview: {}", node.node_id)};
        }
      }
    }
  }

  // Only non-previewable SINK nodes available - return placeholder
  return SuggestedViewNode{
      PLACEHOLDER_NODE, true,
      "Project only contains sink nodes - no preview available"};
}

// ============================================================================
// Stage preview support
// ============================================================================

void PreviewRenderer::ensure_node_executed(const NodeID& node_id,
                                           bool disable_cache) const {
  if (!dag_) {
    return;
  }

  // For sink nodes, we need to execute their inputs to populate cached_input_
  // For other nodes, execute up to the node itself
  const auto& dag_nodes = dag_->nodes();
  auto node_it =
      std::find_if(dag_nodes.begin(), dag_nodes.end(),
                   [&node_id](const auto& n) { return n.node_id == node_id; });

  if (node_it == dag_nodes.end()) {
    ORC_LOG_ERROR("Node '{}' not found in DAG", node_id.to_string());
    return;
  }

  bool is_sink = (node_it->stage &&
                  node_it->stage->get_node_type_info().type == NodeType::SINK);

  // CRITICAL: Only disable artifact caching when explicitly requested (e.g.,
  // for actual rendering) We need execute() to be called on the stage instance
  // so it can populate its cached_output_ member for preview rendering. If
  // artifact caching is enabled, the executor returns cached artifacts without
  // calling execute(), leaving the stage's cached_output_ null. However, when
  // just querying available outputs, we can use cached results to avoid
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
    ORC_LOG_DEBUG(
        "Executed inputs for sink node '{}' (sink's cached_input_ should now "
        "be populated)",
        node_id.to_string());
  } else {
    ORC_LOG_DEBUG(
        "Executed DAG up to node '{}' - stage instance should have "
        "cached_output_ set",
        node_id.to_string());
  }
}

std::vector<PreviewOutputInfo> PreviewRenderer::get_capability_preview_outputs(
    const StagePreviewCapability& capability) {
  std::vector<PreviewOutputInfo> outputs;
  if (!capability.is_valid()) {
    return outputs;
  }

  if (!has_colour_domain_type(capability)) {
    return outputs;
  }

  // In VFR domain field1 is always first; first_field_offset is always 0.
  outputs.push_back(
      PreviewOutputInfo{PreviewOutputType::Frame_Field1_First, "Frame",
                        capability.navigation_extent.item_count, true,
                        capability.geometry.dar_correction_factor,
                        "phase2_colour_carrier", false, false, 0});

  return outputs;
}

PreviewRenderResult PreviewRenderer::render_colour_carrier_preview(
    const NodeID& stage_node_id, const IColourPreviewProvider& provider,
    const StagePreviewCapability& capability, PreviewOutputType type,
    uint64_t index, PreviewNavigationHint hint) {
  PreviewRenderResult result{};
  result.node_id = stage_node_id;
  result.output_type = type;
  result.output_index = index;
  result.success = false;

  if (!capability.is_valid()) {
    result.error_message = "Stage preview capability is invalid";
    return result;
  }

  if (type != PreviewOutputType::Frame_Field1_First) {
    result.error_message =
        "Colour carrier currently supports Frame output only";
    return result;
  }

  if (index >= capability.navigation_extent.item_count) {
    result.error_message = "Requested frame index is out of range";
    return result;
  }

  auto carrier_opt = provider.get_colour_preview_carrier(index, hint);
  if (!carrier_opt.has_value() || !carrier_opt->is_valid()) {
    result.error_message = "Failed to fetch colour preview carrier";
    result.image = create_placeholder_image(type, "Rendering failed");
    result.success = true;
    return result;
  }

  result.image = render_preview_from_colour_carrier(*carrier_opt);
  result.success = result.image.is_valid();
  result.image.vectorscope_data = carrier_opt->vectorscope_data;

  if (!result.success) {
    result.error_message =
        "Colour carrier conversion produced invalid preview image";
    result.image = create_placeholder_image(type, "Rendering failed");
    result.success = true;
    return result;
  }

  if (result.success) {
    render_dropouts(result.image);
  }

  return result;
}

std::vector<PreviewOutputInfo> PreviewRenderer::build_outputs_from_options(
    const NodeID& stage_node_id, const DAGNode& stage_node,
    const std::vector<PreviewOption>& options) {
  std::vector<PreviewOutputInfo> outputs;

  if (options.empty()) {
    ORC_LOG_WARN(
        "Stage node '{}' has no preview options after execution - "
        "cached output may be null",
        stage_node_id.to_string());
    if (stage_node.stage) {
      auto node_type_info = stage_node.stage->get_node_type_info();
      ORC_LOG_WARN("Node '{}' is type '{}' ({})", stage_node_id.to_string(),
                   node_type_info.stage_name, node_type_info.display_name);
    }
    return outputs;
  }

  bool has_separate_channels = false;
  if (frame_renderer_) {
    auto vfr_probe = frame_renderer_->render_frame_at_node(
        stage_node_id, static_cast<FrameID>(0));
    if (vfr_probe.is_valid && vfr_probe.representation) {
      has_separate_channels = vfr_probe.representation->has_separate_channels();
    }
  }

  const std::string stage_name =
      stage_node.stage ? stage_node.stage->get_node_type_info().stage_name : "";
  const bool is_chroma_decoder = (stage_name == "chroma_sink");

  for (const auto& option : options) {
    PreviewOutputType type = PreviewOutputType::Frame_Field1_First;
    if (option.id == "field" || option.id == "field_raw") {
      type = PreviewOutputType::Frame_Field1;
    } else if (option.id == "split" || option.id == "split_raw") {
      type = PreviewOutputType::Split;
    }

    uint64_t first_field_offset = 0;
    if (type == PreviewOutputType::Frame_Field1_First ||
        type == PreviewOutputType::Frame_Reversed ||
        type == PreviewOutputType::Split) {
      first_field_offset_cache_[stage_node_id] = 0;
    }

    outputs.push_back(PreviewOutputInfo{
        type,
        option.display_name,
        option.count,
        true,
        option.dar_aspect_correction,
        option.id,
        !is_chroma_decoder,
        has_separate_channels,
        first_field_offset,
    });
  }

  ORC_LOG_DEBUG("Stage node '{}' has {} preview options",
                stage_node_id.to_string(), outputs.size());
  return outputs;
}

}  // namespace orc
