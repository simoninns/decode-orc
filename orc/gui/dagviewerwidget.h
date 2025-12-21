/*
 * File:        dagviewerwidget.h
 * Module:      orc-gui
 * Purpose:     DAG viewer widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPointer>
#include <memory>
#include "../core/include/dag_executor.h"
#include "../core/include/stage_parameter.h"
#include "../core/include/dag_serialization.h"
#include "../core/include/project.h"

// Forward declarations
namespace orc {
    class AnalysisTool;
}

/**
 * @brief Node state for visualization
 */
enum class NodeState {
    Pending,
    Running,
    Completed,
    Failed
};

/**
 * @brief Graphics item representing a DAG node
 */
class DAGViewerWidget;  // Forward declaration

class DAGNodeItem : public QGraphicsItem {
public:
    DAGNodeItem(const std::string& node_id, 
                const std::string& stage_name,
                QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void setState(NodeState state);
    NodeState state() const { return state_; }
    
    std::string nodeId() const { return node_id_; }
    bool isSourceNode() const { 
        const orc::NodeTypeInfo* info = orc::get_node_type_info(stage_name_);
        return info && info->max_inputs == 0;
    }
    bool isSinkNode() const {
        const orc::NodeTypeInfo* info = orc::get_node_type_info(stage_name_);
        return info && info->type == orc::NodeType::SINK;
    }
    
    std::string stageName() const { return stage_name_; }
    void setStageName(const std::string& stage_name);
    
    std::string displayName() const { return display_name_; }
    void setDisplayName(const std::string& display_name);
    
    std::string userLabel() const { return user_label_; }
    void setUserLabel(const std::string& user_label);
    
    // Viewer connection for position updates
    void setViewer(DAGViewerWidget* viewer) { viewer_ = viewer; }
    
    // Parameter storage
    void setParameters(const std::map<std::string, orc::ParameterValue>& params);
    std::map<std::string, orc::ParameterValue> getParameters() const { return parameters_; }
    
    // Connection points - multi-port support
    QPointF inputConnectionPoint(int port_index = 0) const;
    QPointF outputConnectionPoint(int port_index = 0) const;
    int getInputPortCount() const { return input_port_positions_.size(); }
    int getOutputPortCount() const { return output_port_positions_.size(); }
    
    // Check if point is near a connection point (returns port index or -1)
    int findNearestInputPort(const QPointF& scene_pos) const;
    int findNearestOutputPort(const QPointF& scene_pos) const;
    bool isNearInputPoint(const QPointF& scene_pos) const;  // Legacy compatibility
    bool isNearOutputPoint(const QPointF& scene_pos) const;  // Legacy compatibility
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    
private:
    std::string node_id_;
    std::string stage_name_;
    std::string display_name_;  // Display name from core
    std::string user_label_;    // User-editable label
    NodeState state_;
    bool is_source_node_;
    bool is_dragging_connection_;
    bool is_dragging_;  // Track if node is being dragged
    std::map<std::string, orc::ParameterValue> parameters_;  // Stage parameters
    
    DAGViewerWidget* viewer_;  // For notifying position changes
    
    // Multi-port support
    std::vector<QPointF> input_port_positions_;   // Positions relative to node origin
    std::vector<QPointF> output_port_positions_;  // Positions relative to node origin
    
    // Helper to recalculate port positions
    void updatePortPositions();
    
    static constexpr double WIDTH = 150.0;
    static constexpr double HEIGHT = 80.0;
    static constexpr double CONNECTION_POINT_RADIUS = 8.0;
};

/**
 * @brief Graphics item representing a connection between nodes
 */
class DAGEdgeItem : public QGraphicsItem {
public:
    DAGEdgeItem(DAGNodeItem* source, DAGNodeItem* target, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void updatePosition();
    
    DAGNodeItem* source() const { return source_; }
    DAGNodeItem* target() const { return target_; }
    
    void invalidate();  // Null out pointers when nodes are deleted
    bool isValid() const { return !being_deleted_ && source_ != nullptr && target_ != nullptr; }
    
private:
    DAGNodeItem* source_;
    DAGNodeItem* target_;
    bool being_deleted_;
};

/**
 * @brief Temporary line shown while dragging to create connection
 */
class TemporaryEdgeLine : public QGraphicsLineItem {
public:
    TemporaryEdgeLine(QGraphicsItem* parent = nullptr);
};

/**
 * @brief Widget for visualizing and interacting with DAGs
 */
class DAGViewerWidget : public QGraphicsView {
    Q_OBJECT

public:
    explicit DAGViewerWidget(QWidget* parent = nullptr);
    ~DAGViewerWidget() override;
    
    // Project connection
    void setProject(orc::Project* project);
    orc::Project* project() const { return project_; }
    
    // DAG management
    // Note: setDAG() removed - viewer works with GUIDAG, not executable DAG
    void clearDAG();
    
    // Node state updates
    void setNodeState(const std::string& node_id, NodeState state);
    
    // Node selection
    void selectNode(const std::string& node_id);
    
    // Node type management
    void setNodeStageType(const std::string& node_id, const std::string& stage_name);
    std::string getNodeStageType(const std::string& node_id) const;
    
    // Node parameter management
    void setNodeParameters(const std::string& node_id, const std::map<std::string, orc::ParameterValue>& params);
    std::map<std::string, orc::ParameterValue> getNodeParameters(const std::string& node_id) const;
    
    // Node label management (called by DAGNodeItem)
    void onNodeLabelChanged(const std::string& node_id, const std::string& label);
    
    // Node query methods (used by context menu)
    bool hasNodeConnections(const std::string& node_id) const;
    bool deleteNode(const std::string& node_id, std::string* error = nullptr);
    
    // DAG serialization
    orc::GUIDAG exportDAG() const;
    void importDAG(const orc::GUIDAG& dag);
    
    // Layout operations
    void arrangeToGrid();
    
    // Called by DAGNodeItem when position changes
    void onNodePositionChanged(const std::string& node_id, double x, double y);
    
    void setSourceInfo(int source_number, const QString& source_name);

signals:
    void nodeSelected(const std::string& node_id);
    void nodeDoubleClicked(const std::string& node_id);
    void editParametersRequested(const std::string& node_id);
    void triggerStageRequested(const std::string& node_id);
    void addNodeRequested(const std::string& after_node_id);
    void deleteNodeRequested(const std::string& node_id);
    void edgeCreated(const std::string& source_id, const std::string& target_id);
    void dagModified();  // Emitted whenever DAG is modified
    
private slots:
    void onSceneSelectionChanged();
    
private:
    void initializeWithStartNode();
    void buildGraphicsItems(const orc::DAG& dag);
    void layoutNodes(const orc::DAG& dag);
    std::vector<std::string> topologicalSort(const orc::DAG& dag) const;
    
    std::string generateNodeId();
    bool addNode(const std::string& node_id, const std::string& stage_name, const QPointF& pos, std::string* error = nullptr);
    void addNodeAtPosition(const QPointF& pos);
    void addNodeWithType(const QPointF& pos, const std::string& stage_name);
    bool deleteEdge(DAGEdgeItem* edge, std::string* error = nullptr);
    void deleteSelectedItems();
    
    // Safety validation helpers
    bool isNodeValid(DAGNodeItem* node) const;
    bool isEdgeValid(const DAGEdgeItem* edge) const;
    DAGNodeItem* findNodeById(const std::string& node_id) const;
    void cleanupStalePointers();
    
    // Connection counting for validation
    int countInputConnections(const std::string& node_id) const;
    int countOutputConnections(const std::string& node_id) const;
    
    void createEdge(const std::string& source_id, const std::string& target_id);
    void cleanupInvalidEdges();
    void startEdgeDrag(DAGNodeItem* source_node, const QPointF& start_pos);
    void updateEdgeDrag(const QPointF& current_pos);
    void finishEdgeDrag(const QPointF& end_pos);
    void cancelEdgeDrag();
    void runAnalysisForNode(orc::AnalysisTool* tool, const std::string& node_id, const std::string& stage_name);
    
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    
    QGraphicsScene* scene_;
    std::map<std::string, DAGNodeItem*> node_items_;
    std::vector<DAGEdgeItem*> edge_items_;
    
    // Note: No local DAG copy - DAG is owned by GUIProject
    bool has_dag_;
    
    // Project pointer for CRUD operations
    orc::Project* project_;
    
    // Edge creation state
    bool is_creating_edge_;
    DAGNodeItem* edge_source_node_;  // Not QPointer - QGraphicsItem doesn't inherit QObject
    TemporaryEdgeLine* temp_edge_line_;
};
