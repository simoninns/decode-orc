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

using QtNodes::ConnectionId;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortRole;
using QtNodes::PortType;

/**
 * QtNodes AbstractGraphModel implementation that wraps orc::Project
 * 
 * This adapter allows QtNodes to visualize and edit the ORC project DAG.
 */
class OrcGraphModel : public QtNodes::AbstractGraphModel
{
    Q_OBJECT

public:
    explicit OrcGraphModel(orc::Project& project, QObject* parent = nullptr);
    ~OrcGraphModel() override = default;

    // QtNodes AbstractGraphModel interface
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
    
    // Refresh from project (call after external changes)
    void refresh();
    
    // Get ORC node ID from QtNodes NodeId
    std::string getOrcNodeId(NodeId qtNodeId) const;
    
    // Access to underlying project (for context menu)
    orc::Project& project() { return project_; }
    const orc::Project& project() const { return project_; }
    
    // Helper to find ORC node by ID (public for custom painters)
    const orc::ProjectDAGNode* findOrcNode(const std::string& node_id) const;

private:
    orc::Project& project_;
    
    // Map between QtNodes IDs and ORC node IDs
    std::map<NodeId, std::string> qt_to_orc_nodes_;
    std::map<std::string, NodeId> orc_to_qt_nodes_;
    
    // Store all connections
    std::unordered_set<ConnectionId> connectivity_;
    
    // Helper functions
    NodeId getOrCreateQtNodeId(const std::string& orc_node_id);
    // Removed non-const version - use project_io for modifications
    
    void buildMappings();
};
