/*
 * File:        orcgraphmodel.h
 * Module:      orc-gui
 * Purpose:     QtNodes AbstractGraphModel adapter for ORC projects
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <QtNodes/AbstractGraphModel>
#include <QJsonObject>
#include "project.h"
#include "../core/include/node_id.h"

using QtNodes::ConnectionId;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortRole;
using QtNodes::PortType;

/**
 * @brief QtNodes AbstractGraphModel adapter for ORC projects
 * 
 * This adapter allows QtNodes to visualize and edit the ORC project DAG.
 * It implements the QtNodes::AbstractGraphModel interface and translates
 * between QtNodes node IDs and orc::NodeID identifiers.
 * 
 * All modifications to the graph are immediately reflected in the underlying
 * orc::Project instance.
 */
class OrcGraphModel : public QtNodes::AbstractGraphModel
{
    Q_OBJECT

public:
    /**
     * @brief Construct a graph model for a project
     * @param project The ORC project to visualize/edit
     * @param parent Parent QObject
     */
    explicit OrcGraphModel(orc::Project& project, QObject* parent = nullptr);
    ~OrcGraphModel() override = default;

    /// @name QtNodes AbstractGraphModel Interface
    /// @{
    NodeId newNodeId() override;
    std::unordered_set<NodeId> allNodeIds() const override;
    std::unordered_set<ConnectionId> allConnectionIds(NodeId const nodeId) const override;
    std::unordered_set<ConnectionId> connections(NodeId nodeId,
                                                  PortType portType,
                                                  PortIndex index) const override;
    
    bool connectionExists(ConnectionId const connectionId) const override;
    NodeId addNode(QString const nodeType = QString()) override;
    bool connectionPossible(ConnectionId const connectionId) const override;
    void addConnection(ConnectionId const connectionId) override;
    bool nodeExists(NodeId const nodeId) const override;
    
    QVariant nodeData(NodeId nodeId, NodeRole role) const override;
    bool setNodeData(NodeId nodeId, NodeRole role, QVariant value) override;
    
    QVariant portData(NodeId nodeId,
                      PortType portType,
                      PortIndex portIndex,
                      PortRole role) const override;
    
    bool setPortData(NodeId nodeId,
                     PortType portType,
                     PortIndex portIndex,
                     QVariant const& value,
                     PortRole role) override;
    
    bool deleteConnection(ConnectionId const connectionId) override;
    bool deleteNode(NodeId const nodeId) override;
    
    QJsonObject saveNode(NodeId const nodeId) const override;
    void loadNode(QJsonObject const& nodeJson) override;
    /// @}
    
    /**
     * @brief Refresh model from project
     * 
     * Call this after external changes to the project to update the view.
     */
    void refresh();
    
    /**
     * @brief Convert QtNodes NodeId to ORC NodeID
     * @param qtNodeId QtNodes node identifier
     * @return Corresponding orc::NodeID
     */
    orc::NodeID getOrcNodeId(NodeId qtNodeId) const;
    
    /// @name Project Access
    /// @{
    orc::Project& project() { return project_; }  ///< Get mutable project reference
    const orc::Project& project() const { return project_; }  ///< Get const project reference
    /// @}
    
    // Helper to find ORC node by ID (public for custom painters)
    const orc::ProjectDAGNode* findOrcNode(const orc::NodeID& node_id) const;

private:
    orc::Project& project_;
    
    // Map between QtNodes IDs and ORC node IDs
    std::map<NodeId, orc::NodeID> qt_to_orc_nodes_;
    std::map<orc::NodeID, NodeId> orc_to_qt_nodes_;
    
    // Store all connections
    std::unordered_set<ConnectionId> connectivity_;
    
    // Helper functions
    NodeId getOrCreateQtNodeId(const orc::NodeID& orc_node_id);
    // Removed non-const version - use project_io for modifications
    
    void buildMappings();
};
