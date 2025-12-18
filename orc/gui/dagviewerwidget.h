/******************************************************************************
 * dagviewerwidget.h
 *
 * DAG visualization widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPointer>
#include <memory>
#include "../core/include/dag_executor.h"
#include "../core/include/stage_parameter.h"
#include "../core/include/dag_serialization.h"

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
class DAGNodeItem : public QGraphicsItem {
public:
    DAGNodeItem(const std::string& node_id, 
                const std::string& stage_name,
                bool is_source_node = false,
                QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void setState(NodeState state);
    NodeState state() const { return state_; }
    
    std::string nodeId() const { return node_id_; }
    bool isSourceNode() const { return is_source_node_; }
    
    std::string stageName() const { return stage_name_; }
    void setStageName(const std::string& stage_name);
    
    std::string displayName() const { return display_name_; }
    void setDisplayName(const std::string& display_name);
    
    // Source info for START nodes
    void setSourceInfo(int source_number, const QString& source_name);
    
    // Parameter storage
    void setParameters(const std::map<std::string, orc::ParameterValue>& params);
    std::map<std::string, orc::ParameterValue> getParameters() const { return parameters_; }
    
    // Connection points
    QPointF inputConnectionPoint() const;
    QPointF outputConnectionPoint() const;
    
    // Check if point is near a connection point
    bool isNearInputPoint(const QPointF& scene_pos) const;
    bool isNearOutputPoint(const QPointF& scene_pos) const;
    
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
    NodeState state_;
    bool is_source_node_;
    bool is_dragging_connection_;
    std::map<std::string, orc::ParameterValue> parameters_;  // Stage parameters
    
    // Source info for START nodes
    int source_number_;
    QString source_name_;
    
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
    
    // DAG management
    void setDAG(const orc::DAG& dag);
    void clearDAG();
    
    // Node state updates
    void setNodeState(const std::string& node_id, NodeState state);
    
    // Node type management
    void setNodeStageType(const std::string& node_id, const std::string& stage_name);
    std::string getNodeStageType(const std::string& node_id) const;
    
    // Node parameter management
    void setNodeParameters(const std::string& node_id, const std::map<std::string, orc::ParameterValue>& params);
    std::map<std::string, orc::ParameterValue> getNodeParameters(const std::string& node_id) const;
    
    // DAG serialization
    orc::GUIDAG exportDAG() const;
    void importDAG(const orc::GUIDAG& dag);
    
    // Layout operations
    void arrangeToGrid();
    
    void setSourceInfo(int source_number, const QString& source_name);

signals:
    void nodeSelected(const std::string& node_id);
    void nodeDoubleClicked(const std::string& node_id);
    void changeNodeTypeRequested(const std::string& node_id);
    void editParametersRequested(const std::string& node_id);
    void addNodeRequested(const std::string& after_node_id);
    void deleteNodeRequested(const std::string& node_id);
    void edgeCreated(const std::string& source_id, const std::string& target_id);
    
private slots:
    void onSceneSelectionChanged();
    void showContextMenu(const QPoint& pos);
    
private:
    void initializeWithStartNode();
    void buildGraphicsItems(const orc::DAG& dag);
    void layoutNodes(const orc::DAG& dag);
    std::vector<std::string> topologicalSort(const orc::DAG& dag) const;
    
    std::string generateNodeId();
    bool addNode(const std::string& node_id, const std::string& stage_name, const QPointF& pos, std::string* error = nullptr);
    void addNodeAtPosition(const QPointF& pos);
    bool hasNodeConnections(const std::string& node_id) const;
    bool deleteNode(const std::string& node_id, std::string* error = nullptr);
    bool deleteEdge(DAGEdgeItem* edge, std::string* error = nullptr);
    void deleteSelectedItems();
    
    // Safety validation helpers
    bool isNodeValid(DAGNodeItem* node) const;
    bool isEdgeValid(const DAGEdgeItem* edge) const;
    DAGNodeItem* findNodeById(const std::string& node_id) const;
    void cleanupStalePointers();
    
    void createEdge(const std::string& source_id, const std::string& target_id);
    void cleanupInvalidEdges();
    void startEdgeDrag(DAGNodeItem* source_node, const QPointF& start_pos);
    void updateEdgeDrag(const QPointF& current_pos);
    void finishEdgeDrag(const QPointF& end_pos);
    void cancelEdgeDrag();
    
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    QGraphicsScene* scene_;
    std::map<std::string, DAGNodeItem*> node_items_;
    std::vector<DAGEdgeItem*> edge_items_;
    
    orc::DAG current_dag_;
    bool has_dag_;
    
    // Edge creation state
    bool is_creating_edge_;
    DAGNodeItem* edge_source_node_;  // Not QPointer - QGraphicsItem doesn't inherit QObject
    TemporaryEdgeLine* temp_edge_line_;
    
    // Auto-ID generation
    int next_node_id_;
};
