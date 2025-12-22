/*
 * File:        orcgraphmodel.cpp
 * Module:      orc-gui
 * Purpose:     QtNodes AbstractGraphModel adapter for ORC projects
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "orcgraphmodel.h"
#include "node_type_helper.h"
#include "logging.h"
#include "../core/include/project.h"
#include <QtNodes/ConnectionIdUtils>
#include <QtNodes/StyleCollection>

using QtNodes::getNodeId;
using QtNodes::getPortIndex;

OrcGraphModel::OrcGraphModel(orc::Project& project, QObject* parent)
    : QtNodes::AbstractGraphModel()
    , project_(project)
{
    setParent(parent);
    buildMappings();
}

void OrcGraphModel::buildMappings()
{
    qt_to_orc_nodes_.clear();
    orc_to_qt_nodes_.clear();
    connectivity_.clear();
    
    // Build node mappings
    NodeId qt_id = 0;
    const auto& nodes = project_.get_nodes();
    ORC_LOG_DEBUG("OrcGraphModel::buildMappings - Project has {} nodes", nodes.size());
    
    for (const auto& node : nodes) {
        qt_to_orc_nodes_[qt_id] = node.node_id;
        orc_to_qt_nodes_[node.node_id] = qt_id;
        ORC_LOG_DEBUG("  Mapped QtNode {} -> ORC node '{}'", qt_id, node.node_id);
        qt_id++;
    }
    
    // Build connection mappings
    const auto& edges = project_.get_edges();
    ORC_LOG_DEBUG("OrcGraphModel::buildMappings - Project has {} edges", edges.size());
    
    for (const auto& edge : edges) {
        auto it_out = orc_to_qt_nodes_.find(edge.source_node_id);
        auto it_in = orc_to_qt_nodes_.find(edge.target_node_id);
        
        if (it_out != orc_to_qt_nodes_.end() && it_in != orc_to_qt_nodes_.end()) {
            // All nodes have single ports (index 0)
            ConnectionId conn_id{it_out->second, 0, it_in->second, 0};
            connectivity_.insert(conn_id);
            ORC_LOG_DEBUG("  Mapped connection: {} -> {}", edge.source_node_id, edge.target_node_id);
        }
    }
}

NodeId OrcGraphModel::newNodeId()
{
    // Find the maximum QtNodes NodeId currently in use
    NodeId max_id = 0;
    for (const auto& pair : qt_to_orc_nodes_) {
        if (pair.first >= max_id) {
            max_id = pair.first + 1;
        }
    }
    return max_id;
}

NodeId OrcGraphModel::getOrCreateQtNodeId(const std::string& orc_node_id)
{
    auto it = orc_to_qt_nodes_.find(orc_node_id);
    if (it != orc_to_qt_nodes_.end()) {
        return it->second;
    }
    
    NodeId qt_id = newNodeId();
    qt_to_orc_nodes_[qt_id] = orc_node_id;
    orc_to_qt_nodes_[orc_node_id] = qt_id;
    return qt_id;
}

const orc::ProjectDAGNode* OrcGraphModel::findOrcNode(const std::string& node_id) const
{
    for (const auto& node : project_.get_nodes()) {
        if (node.node_id == node_id) {
            return &node;
        }
    }
    return nullptr;
}

// Removed non-const version since we use project_io functions for all modifications
// Original function tried to return mutable pointer from const get_nodes() - not possible

void OrcGraphModel::refresh()
{
    ORC_LOG_DEBUG("OrcGraphModel::refresh - Rebuilding node mappings");
    buildMappings();
    ORC_LOG_DEBUG("OrcGraphModel::refresh - Emitting modelReset signal");
    Q_EMIT modelReset();
}

std::unordered_set<NodeId> OrcGraphModel::allNodeIds() const
{
    std::unordered_set<NodeId> ids;
    for (const auto& [qt_id, orc_id] : qt_to_orc_nodes_) {
        ids.insert(qt_id);
    }
    return ids;
}

std::unordered_set<ConnectionId> OrcGraphModel::allConnectionIds(NodeId const nodeId) const
{
    std::unordered_set<ConnectionId> result;
    
    std::copy_if(connectivity_.begin(),
                 connectivity_.end(),
                 std::inserter(result, std::end(result)),
                 [&nodeId](ConnectionId const& cid) {
                     return cid.inNodeId == nodeId || cid.outNodeId == nodeId;
                 });
    
    return result;
}

std::unordered_set<ConnectionId> OrcGraphModel::connections(
    NodeId nodeId,
    PortType portType,
    PortIndex index) const
{
    std::unordered_set<ConnectionId> result;
    
    std::copy_if(connectivity_.begin(),
                 connectivity_.end(),
                 std::inserter(result, std::end(result)),
                 [&portType, &index, &nodeId](ConnectionId const& cid) {
                     return (getNodeId(portType, cid) == nodeId
                             && getPortIndex(portType, cid) == index);
                 });
    
    return result;
}

bool OrcGraphModel::connectionExists(ConnectionId const connectionId) const
{
    return connectivity_.find(connectionId) != connectivity_.end();
}

NodeId OrcGraphModel::addNode(QString const nodeType)
{
    // Use core's add_node function which generates unique IDs properly
    std::string stage_name = nodeType.isEmpty() ? "TBCSource" : nodeType.toStdString();
    
    try {
        std::string node_id = orc::project_io::add_node(project_, stage_name, 0.0, 0.0);
        
        NodeId qt_id = getOrCreateQtNodeId(node_id);
        
        Q_EMIT nodeCreated(qt_id);
        
        return qt_id;
    } catch (const std::exception& e) {
        // Invalid stage name or other error
        return QtNodes::InvalidNodeId;
    }
}

bool OrcGraphModel::connectionPossible(ConnectionId const connectionId) const
{
    // Check if connection already exists
    if (connectionExists(connectionId)) {
        return false;
    }
    
    // Check if nodes exist
    if (!nodeExists(connectionId.outNodeId) || !nodeExists(connectionId.inNodeId)) {
        return false;
    }
    
    // Don't allow self-connections
    if (connectionId.outNodeId == connectionId.inNodeId) {
        return false;
    }
    
    return true;
}

void OrcGraphModel::addConnection(ConnectionId const connectionId)
{
    if (!connectionPossible(connectionId)) {
        return;
    }
    
    // Get ORC node IDs
    auto it_out = qt_to_orc_nodes_.find(connectionId.outNodeId);
    auto it_in = qt_to_orc_nodes_.find(connectionId.inNodeId);
    
    if (it_out == qt_to_orc_nodes_.end() || it_in == qt_to_orc_nodes_.end()) {
        return;
    }
    
    // Use core's add_edge which handles validation and modification tracking
    try {
        orc::project_io::add_edge(project_, it_out->second, it_in->second);
        
        // Add to local connectivity
        connectivity_.insert(connectionId);
        
        Q_EMIT connectionCreated(connectionId);
    } catch (const std::exception& e) {
        // Connection validation failed (e.g., invalid connection type, exceeded limits)
        // Silently ignore - QtNodes will show the connection isn't possible
    }
}

bool OrcGraphModel::nodeExists(NodeId const nodeId) const
{
    return qt_to_orc_nodes_.find(nodeId) != qt_to_orc_nodes_.end();
}

QVariant OrcGraphModel::nodeData(NodeId nodeId, NodeRole role) const
{
    auto it = qt_to_orc_nodes_.find(nodeId);
    if (it == qt_to_orc_nodes_.end()) {
        return QVariant();
    }
    
    const orc::ProjectDAGNode* node = findOrcNode(it->second);
    if (!node) {
        return QVariant();
    }
    
    switch (role) {
        case NodeRole::Type:
            return QString::fromStdString(node->stage_name);
            
        case NodeRole::Caption:
            return QString::fromStdString(node->user_label);
            
        case NodeRole::Position:
            return QPointF(node->x_position, node->y_position);
            
        case NodeRole::Size:
            return QSize(120, 60);  // Default node size
            
        case NodeRole::CaptionVisible:
            return true;
            
        case NodeRole::InPortCount: {
            // Get port count from NodeTypeInfo
            const orc::NodeTypeInfo* info = orc::get_node_type_info(node->stage_name);
            return info ? static_cast<unsigned int>(info->max_inputs) : 1u;
        }
            
        case NodeRole::OutPortCount: {
            // Get port count from NodeTypeInfo
            const orc::NodeTypeInfo* info = orc::get_node_type_info(node->stage_name);
            return info ? static_cast<unsigned int>(info->max_outputs) : 1u;
        }
            
        case NodeRole::Widget:
            return QVariant();
            
        case NodeRole::Style: {
            auto style = QtNodes::StyleCollection::nodeStyle();
            return style.toJson().toVariantMap();
        }
            
        default:
            return QVariant();
    }
}

bool OrcGraphModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    auto it = qt_to_orc_nodes_.find(nodeId);
    if (it == qt_to_orc_nodes_.end()) {
        return false;
    }
    
    const orc::ProjectDAGNode* node = findOrcNode(it->second);
    if (!node) {
        return false;
    }
    
    switch (role) {
        case NodeRole::Caption:
            try {
                orc::project_io::set_node_label(project_, it->second, value.toString().toStdString());
                Q_EMIT nodeUpdated(nodeId);
                return true;
            } catch (const std::exception&) {
                return false;
            }
            
        case NodeRole::Position: {
            QPointF pos = value.toPointF();
            try {
                orc::project_io::set_node_position(project_, it->second, pos.x(), pos.y());
                Q_EMIT nodePositionUpdated(nodeId);
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
            
        default:
            return false;
    }
}

QVariant OrcGraphModel::portData(NodeId nodeId,
                                  PortType portType,
                                  PortIndex portIndex,
                                  PortRole role) const
{
    auto it = qt_to_orc_nodes_.find(nodeId);
    if (it == qt_to_orc_nodes_.end()) {
        return QVariant();
    }
    
    const orc::ProjectDAGNode* node = findOrcNode(it->second);
    if (!node) {
        return QVariant();
    }
    
    // Get node type info for port capabilities
    const orc::NodeTypeInfo* info = orc::get_node_type_info(node->stage_name);
    
    switch (role) {
        case PortRole::Data:
            return QVariant();
            
        case PortRole::DataType:
            return QString("VideoField");
            
        case PortRole::ConnectionPolicyRole:
            // Always return One - multi-port support is handled by port count,
            // not by allowing multiple connections per port
            return QVariant::fromValue(QtNodes::ConnectionPolicy::One);
            
        case PortRole::CaptionVisible:
            return false;
            
        case PortRole::Caption:
            return QString();
            
        default:
            return QVariant();
    }
}

bool OrcGraphModel::setPortData(NodeId nodeId,
                                 PortType portType,
                                 PortIndex portIndex,
                                 QVariant const& value,
                                 PortRole role)
{
    // Ports are not directly editable in our model
    return false;
}

bool OrcGraphModel::deleteConnection(ConnectionId const connectionId)
{
    if (!connectionExists(connectionId)) {
        return false;
    }
    
    // Get ORC node IDs
    auto it_out = qt_to_orc_nodes_.find(connectionId.outNodeId);
    auto it_in = qt_to_orc_nodes_.find(connectionId.inNodeId);
    
    if (it_out == qt_to_orc_nodes_.end() || it_in == qt_to_orc_nodes_.end()) {
        return false;
    }
    
    const std::string& source_id = it_out->second;
    const std::string& target_id = it_in->second;
    
    // Use core's remove_edge which handles modification tracking
    try {
        orc::project_io::remove_edge(project_, source_id, target_id);
        
        // Remove from local connectivity
        connectivity_.erase(connectionId);
        
        Q_EMIT connectionDeleted(connectionId);
        
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_WARN("Failed to delete connection: {}", e.what());
        return false;
    }
}

bool OrcGraphModel::deleteNode(NodeId const nodeId)
{
    auto it = qt_to_orc_nodes_.find(nodeId);
    if (it == qt_to_orc_nodes_.end()) {
        return false;
    }
    
    const std::string& orc_node_id = it->second;
    
    // Remove all connections involving this node from GUI state
    std::vector<ConnectionId> to_remove;
    for (const auto& conn : connectivity_) {
        if (conn.outNodeId == nodeId || conn.inNodeId == nodeId) {
            to_remove.push_back(conn);
        }
    }
    for (const auto& conn : to_remove) {
        connectivity_.erase(conn);
        Q_EMIT connectionDeleted(conn);
    }
    
    // Use core's remove_node which handles removing edges and modification tracking
    try {
        orc::project_io::remove_node(project_, orc_node_id);
        
        // Update mappings
        qt_to_orc_nodes_.erase(nodeId);
        orc_to_qt_nodes_.erase(orc_node_id);
        
        Q_EMIT nodeDeleted(nodeId);
        
        return true;
    } catch (const std::exception& e) {
        // Log error - validation failed (likely has connections)
        ORC_LOG_WARN("Failed to delete node '{}': {}", orc_node_id, e.what());
        return false;
    }
}

QJsonObject OrcGraphModel::saveNode(NodeId const nodeId) const
{
    QJsonObject json;
    
    // QtNodes expects the integer NodeId in the "id" field for undo/redo
    json["id"] = static_cast<int>(nodeId);
    
    // Also save ORC-specific data for completeness
    auto it = qt_to_orc_nodes_.find(nodeId);
    if (it != qt_to_orc_nodes_.end()) {
        const orc::ProjectDAGNode* node = findOrcNode(it->second);
        if (node) {
            json["orc_node_id"] = QString::fromStdString(node->node_id);
            json["stage_name"] = QString::fromStdString(node->stage_name);
            json["display_name"] = QString::fromStdString(node->display_name);
            json["user_label"] = QString::fromStdString(node->user_label);
            json["x"] = node->x_position;
            json["y"] = node->y_position;
        }
    }
    
    return json;
}

void OrcGraphModel::loadNode(QJsonObject const& nodeJson)
{
    // Project loading is handled by project_io, not by QtNodes
    ORC_LOG_WARN("OrcGraphModel::loadNode not implemented - use project_io instead");
}

std::string OrcGraphModel::getOrcNodeId(NodeId qtNodeId) const
{
    auto it = qt_to_orc_nodes_.find(qtNodeId);
    if (it != qt_to_orc_nodes_.end()) {
        return it->second;
    }
    return "";
}
