/*
 * File:        dag_frame_renderer.cpp
 * Module:      orc-core
 * Purpose:     Frame rendering at DAG nodes using VideoFrameRepresentation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dag_frame_renderer.h"

#include <orc/stage/observation/biphase_observer.h>
#include <orc/stage/observation/black_psnr_observer.h>
#include <orc/stage/observation/burst_level_observer.h>
#include <orc/stage/observation/closed_caption_observer.h>
#include <orc/stage/observation/colour_frame_phase_observer.h>
#include <orc/stage/observation/field_quality_observer.h>
#include <orc/stage/observation/fm_code_observer.h>
#include <orc/stage/observation/white_flag_observer.h>
#include <orc/stage/observation/white_snr_observer.h>
#include <orc/support/logging.h>

#include <algorithm>
#include <sstream>

namespace orc {

DAGFrameRenderer::DAGFrameRenderer(std::shared_ptr<const DAG> dag)
    : dag_(std::move(dag)),
      dag_version_(1),
      cache_enabled_(true),
      render_cache_(kMaxCachedRenders),
      node_index_valid_(false) {
  if (!dag_) {
    throw DAGFrameRenderError("Cannot create renderer with null DAG");
  }

  if (!dag_->validate()) {
    auto errors = dag_->get_validation_errors();
    std::ostringstream oss;
    oss << "Cannot create renderer with invalid DAG:\n";
    for (const auto& error : errors) {
      oss << "  - " << error << "\n";
    }
    throw DAGFrameRenderError(oss.str());
  }

  executor_ = std::make_unique<DAGExecutor>();
  executor_->set_cache_enabled(true);
}

void DAGFrameRenderer::ensure_node_index() const {
  if (!node_index_valid_) {
    node_index_ = dag_->build_node_index();
    node_index_valid_ = true;
  }
}

bool DAGFrameRenderer::has_node(NodeID node_id) const {
  ensure_node_index();
  return node_index_.find(node_id) != node_index_.end();
}

std::vector<NodeID> DAGFrameRenderer::get_renderable_nodes() const {
  std::vector<NodeID> result;
  for (const auto& node : dag_->nodes()) {
    result.push_back(node.node_id);
  }
  return result;
}

void DAGFrameRenderer::update_dag(std::shared_ptr<const DAG> new_dag) {
  if (!new_dag) {
    throw DAGFrameRenderError("Cannot update to null DAG");
  }

  if (!new_dag->validate()) {
    auto errors = new_dag->get_validation_errors();
    std::ostringstream oss;
    oss << "Cannot update to invalid DAG:\n";
    for (const auto& error : errors) {
      oss << "  - " << error << "\n";
    }
    throw DAGFrameRenderError(oss.str());
  }

  dag_ = std::move(new_dag);
  ++dag_version_;
  node_index_valid_ = false;
  render_cache_.clear();

  executor_ = std::make_unique<DAGExecutor>();
  executor_->set_cache_enabled(true);
}

void DAGFrameRenderer::clear_cache() { render_cache_.clear(); }

FrameRenderResult DAGFrameRenderer::render_frame_at_node(NodeID node_id,
                                                         FrameID frame_id) {
  ORC_LOG_TRACE("DAGFrameRenderer: render_frame_at_node node='{}' frame={}",
                node_id.to_string(), frame_id);

  if (!has_node(node_id)) {
    ORC_LOG_ERROR("DAGFrameRenderer: node '{}' does not exist",
                  node_id.to_string());
    FrameRenderResult err;
    err.is_valid = false;
    err.error_message =
        "Node '" + node_id.to_string() + "' does not exist in DAG";
    err.node_id = node_id;
    err.frame_id = frame_id;
    err.from_cache = false;
    return err;
  }

  if (cache_enabled_) {
    CacheKey key{node_id, frame_id, dag_version_};
    auto cached = render_cache_.get(key);
    if (cached.has_value()) {
      ORC_LOG_TRACE("DAGFrameRenderer: cache hit node='{}' frame={}",
                    node_id.to_string(), frame_id);
      cached->from_cache = true;
      return *cached;
    }
    ORC_LOG_DEBUG("DAGFrameRenderer: cache miss node='{}' frame={}",
                  node_id.to_string(), frame_id);
  }

  auto result = execute_to_node(node_id, frame_id);

  if (cache_enabled_ && result.is_valid) {
    CacheKey key{node_id, frame_id, dag_version_};
    render_cache_.put(key, result);
  }

  return result;
}

FrameRenderResult DAGFrameRenderer::execute_to_node(NodeID node_id,
                                                    FrameID frame_id) {
  FrameRenderResult result;
  result.node_id = node_id;
  result.frame_id = frame_id;
  result.from_cache = false;

  ORC_LOG_DEBUG("DAGFrameRenderer: executing DAG to node '{}' for frame {}",
                node_id.to_string(), frame_id);

  try {
    auto node_outputs = executor_->execute_to_node(*dag_, node_id);

    auto it = node_outputs.find(node_id);
    if (it == node_outputs.end() || it->second.empty()) {
      // Check whether this is a sink (which produces no output by design).
      bool is_sink = false;
      const auto& dag_nodes = dag_->nodes();
      auto dag_it = std::find_if(
          dag_nodes.begin(), dag_nodes.end(),
          [&node_id](const auto& n) { return n.node_id == node_id; });
      if (dag_it != dag_nodes.end() && dag_it->stage) {
        auto ntype = dag_it->stage->get_node_type_info().type;
        is_sink = (ntype == NodeType::SINK || ntype == NodeType::ANALYSIS_SINK);
      }

      if (is_sink) {
        // Sink stages produce no output by design. Fall back to the nearest
        // upstream node that does produce a VFR so the host can show a
        // pass-through preview without requiring any boilerplate in the plugin.
        if (dag_it != dag_nodes.end() && !dag_it->input_node_ids.empty()) {
          const NodeID& upstream_id = dag_it->input_node_ids[0];
          auto up_it = node_outputs.find(upstream_id);
          if (up_it != node_outputs.end() && !up_it->second.empty()) {
            auto up_vfr = std::dynamic_pointer_cast<VideoFrameRepresentation>(
                up_it->second[0]);
            if (up_vfr) {
              ORC_LOG_DEBUG(
                  "DAGFrameRenderer: sink node '{}' — using upstream node "
                  "'{}' output for preview",
                  node_id.to_string(), upstream_id.to_string());
              it = up_it;
            }
          }
        }

        if (it == node_outputs.end() || it->second.empty()) {
          ORC_LOG_DEBUG(
              "DAGFrameRenderer: sink node '{}' has no upstream VFR for "
              "preview",
              node_id.to_string());
          result.is_valid = false;
          result.error_message =
              fmt::format("Node '{}' produced no output", node_id);
          return result;
        }
      } else {
        ORC_LOG_ERROR("DAGFrameRenderer: node '{}' produced no output",
                      node_id.to_string());
        result.is_valid = false;
        result.error_message =
            fmt::format("Node '{}' produced no output", node_id);
        return result;
      }
    }

    auto vfr =
        std::dynamic_pointer_cast<VideoFrameRepresentation>(it->second[0]);
    if (!vfr) {
      ORC_LOG_ERROR(
          "DAGFrameRenderer: node '{}' output is not a "
          "VideoFrameRepresentation",
          node_id.to_string());
      result.is_valid = false;
      result.error_message = "Node '" + node_id.to_string() +
                             "' did not produce a VideoFrameRepresentation";
      return result;
    }

    if (!vfr->has_frame(frame_id)) {
      ORC_LOG_WARN(
          "DAGFrameRenderer: node '{}' frame {} not present in representation",
          node_id.to_string(), frame_id);
      result.is_valid = false;
      result.error_message =
          fmt::format("Frame {} not present in node '{}'", frame_id, node_id);
      return result;
    }

    result.is_valid = true;
    result.representation = vfr;

    // Run all observers to populate observation context for both fields of this
    // frame.
    auto& obs_ctx = executor_->get_observation_context();
    ColourFramePhaseObserver colour_frame_phase_observer;
    colour_frame_phase_observer.process_frame(*vfr, frame_id, obs_ctx);

    BiphaseObserver biphase_observer;
    biphase_observer.process_frame(*vfr, frame_id, obs_ctx);

    FmCodeObserver fm_code_observer;
    fm_code_observer.process_frame(*vfr, frame_id, obs_ctx);

    WhiteFlagObserver white_flag_observer;
    white_flag_observer.process_frame(*vfr, frame_id, obs_ctx);

    FieldQualityObserver field_quality_observer;
    field_quality_observer.process_frame(*vfr, frame_id, obs_ctx);

    BurstLevelObserver burst_level_observer;
    burst_level_observer.process_frame(*vfr, frame_id, obs_ctx);

    WhiteSNRObserver white_snr_observer;
    white_snr_observer.process_frame(*vfr, frame_id, obs_ctx);

    BlackPSNRObserver black_psnr_observer;
    black_psnr_observer.process_frame(*vfr, frame_id, obs_ctx);

    ClosedCaptionObserver closed_caption_observer;
    closed_caption_observer.process_frame(*vfr, frame_id, obs_ctx);

    ORC_LOG_DEBUG("DAGFrameRenderer: node '{}' frame {} rendered successfully",
                  node_id.to_string(), frame_id);
    return result;

  } catch (const std::exception& e) {
    result.is_valid = false;
    result.error_message = std::string("Error rendering frame: ") + e.what();
    ORC_LOG_ERROR(
        "DAGFrameRenderer: exception rendering frame {} at node '{}': {}",
        frame_id, node_id.to_string(), e.what());
    return result;
  }
}

const ObservationContext& DAGFrameRenderer::get_observation_context() const {
  if (!executor_) {
    static ObservationContext empty;
    return empty;
  }
  return executor_->get_observation_context();
}

}  // namespace orc
