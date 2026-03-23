/*
 * File:        preview_view_registry.h
 * Module:      orc-core
 * Purpose:     Registry-driven preview view routing and export dispatch.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/include/preview_view_registry.h. Use RenderPresenter instead."
#endif
#if defined(ORC_CLI_BUILD)
#error "CLI code cannot include core/include/preview_view_registry.h. Use RenderPresenter instead."
#endif

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <node_id.h>

#include "dag_executor.h"
#include "stage_preview_capability.h"
#include "orc_preview_views.h"

namespace orc {

class PreviewRenderer;

/**
 * @brief Registry and request router for preview views.
 *
 * Views are registered by id using factories. A factory receives the active
 * node id and returns an IPreviewView bound to that node.
 */
class PreviewViewRegistry {
public:
    using ViewFactory = std::function<std::unique_ptr<IPreviewView>(NodeID)>;

    PreviewViewRegistry() = default;

    bool register_view(PreviewViewDescriptor descriptor, ViewFactory factory);
    bool unregister_view(const std::string& view_id);

    std::vector<PreviewViewDescriptor> list_views() const;

    std::vector<PreviewViewDescriptor> get_applicable_views(
        const DAG& dag,
        NodeID node_id,
        VideoDataType data_type) const;

    PreviewViewDataResult request_data(
        const DAG& dag,
        NodeID node_id,
        const std::string& view_id,
        VideoDataType data_type,
        const PreviewCoordinate& coordinate);

    PreviewViewExportResult export_as(
        NodeID node_id,
        const std::string& view_id,
        const std::string& format,
        const std::string& path) const;

    void clear_cache_for_node(NodeID node_id);

    static void register_default_views(
        PreviewViewRegistry& registry,
        const std::shared_ptr<const DAG>& dag,
        PreviewRenderer* renderer);

private:
    struct Entry {
        PreviewViewDescriptor descriptor;
        ViewFactory factory;
    };

    struct NodeViewKey {
        NodeID node_id;
        std::string view_id;

        bool operator==(const NodeViewKey& other) const
        {
            return node_id == other.node_id && view_id == other.view_id;
        }
    };

    struct NodeViewKeyHash {
        size_t operator()(const NodeViewKey& key) const;
    };

    static bool node_supports_data_type(const DAG& dag, NodeID node_id, VideoDataType data_type);
    static bool contains_data_type(const std::vector<VideoDataType>& list, VideoDataType data_type);

    std::unique_ptr<IPreviewView> create_view(NodeID node_id, const std::string& view_id) const;

    std::unordered_map<std::string, Entry> entries_;
    mutable std::unordered_map<NodeViewKey, std::unique_ptr<IPreviewView>, NodeViewKeyHash> view_cache_;
};

} // namespace orc
