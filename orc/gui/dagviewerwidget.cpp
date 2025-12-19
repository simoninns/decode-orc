/*
 * File:        dagviewerwidget.cpp
 * Module:      orc-gui
 * Purpose:     DAG viewer widget
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "dagviewerwidget.h"
#include "node_type_helper.h"
#include "../core/include/stage_registry.h"
#include "../core/include/stage_parameter.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QWidget>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <queue>
#include <set>
#include <iostream>

// ============================================================================
// DAGNodeItem Implementation
// ============================================================================

DAGNodeItem::DAGNodeItem(const std::string& node_id, 
                         const std::string& stage_name,
                         bool is_source_node,
                         QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , node_id_(node_id)
    , stage_name_(stage_name)
    , display_name_(stage_name)  // Default to stage_name if not set
    , state_(NodeState::Pending)
    , is_source_node_(is_source_node)
    , is_dragging_connection_(false)
    , viewer_(nullptr)
    , source_number_(-1)
{
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    
    // Initialize port positions based on node type
    updatePortPositions();
}

QRectF DAGNodeItem::boundingRect() const
{
    // Bounding rect must contain:
    // - The box: (0, 0, WIDTH, HEIGHT)
    // - Left circle center at (0, HEIGHT/2) with radius CONNECTION_POINT_RADIUS
    // - Right circle center at (WIDTH, HEIGHT/2) with radius CONNECTION_POINT_RADIUS
    // So we need to extend left by RADIUS and right by RADIUS, plus margin for pen
    qreal margin = CONNECTION_POINT_RADIUS + 2;
    return QRectF(-margin, -2, WIDTH + 2*margin, HEIGHT + 4);
}

void DAGNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    // Background color based on state or source node
    QColor bg_color;
    if (is_source_node_) {
        bg_color = QColor(180, 200, 255);  // Light blue for source node
    } else {
        switch (state_) {
            case NodeState::Pending:
                bg_color = QColor(220, 220, 220);
                break;
            case NodeState::Running:
                bg_color = QColor(255, 220, 100);
                break;
            case NodeState::Completed:
                bg_color = QColor(150, 220, 150);
                break;
            case NodeState::Failed:
                bg_color = QColor(220, 100, 100);
                break;
        }
    }
    
    // Draw box at origin
    QRectF box_rect(0, 0, WIDTH, HEIGHT);
    painter->setPen(QPen(isSelected() ? Qt::blue : Qt::black, 2));
    painter->setBrush(bg_color);
    painter->drawRoundedRect(box_rect, 5, 5);
    
    // Draw stage name
    painter->setPen(Qt::black);
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(10);
    painter->setFont(font);
    
    QRectF text_rect(5, 10, WIDTH - 10, 25);
    
    // Top line: For source nodes show stage name ("Source"), for others show display name
    if (is_source_node_) {
        painter->drawText(text_rect, Qt::AlignCenter, QString::fromStdString(stage_name_));
    } else {
        painter->drawText(text_rect, Qt::AlignCenter, QString::fromStdString(display_name_));
    }
    
    // Draw bottom text (source name for source nodes, nothing for others)
    font.setBold(false);
    font.setPointSize(8);
    painter->setFont(font);
    painter->setPen(QColor(80, 80, 80));
    
    QRectF id_rect(5, 40, WIDTH - 10, 30);
    
    // Bottom line: For source nodes show source filename, for others show nothing
    if (is_source_node_ && !source_name_.isEmpty()) {
        painter->drawText(id_rect, Qt::AlignCenter, source_name_);
    }
    
    // Draw connection points
    painter->setPen(QPen(Qt::darkGray, 2));
    painter->setBrush(Qt::white);
    
    // Get node type info
    NodeTypeHelper::NodeVisualInfo visual_info = NodeTypeHelper::getVisualInfo(stage_name_);
    
    // Input port (left edge) - single circle or concentric circles
    if (visual_info.has_input) {
        QPointF input_pt = NodeTypeHelper::getInputPortPosition(WIDTH, HEIGHT);
        
        if (visual_info.input_is_many) {
            // Draw concentric circles (outer circle with inner dot)
            painter->drawEllipse(input_pt, CONNECTION_POINT_RADIUS, CONNECTION_POINT_RADIUS);
            painter->setBrush(Qt::darkGray);
            painter->drawEllipse(input_pt, CONNECTION_POINT_RADIUS / 3, CONNECTION_POINT_RADIUS / 3);
            painter->setBrush(Qt::white);
        } else {
            // Draw simple circle
            painter->drawEllipse(input_pt, CONNECTION_POINT_RADIUS, CONNECTION_POINT_RADIUS);
        }
    }
    
    // Output port (right edge) - single circle or concentric circles
    if (visual_info.has_output) {
        QPointF output_pt = NodeTypeHelper::getOutputPortPosition(WIDTH, HEIGHT);
        
        if (visual_info.output_is_many) {
            // Draw concentric circles (outer circle with inner dot)
            painter->drawEllipse(output_pt, CONNECTION_POINT_RADIUS, CONNECTION_POINT_RADIUS);
            painter->setBrush(Qt::darkGray);
            painter->drawEllipse(output_pt, CONNECTION_POINT_RADIUS / 3, CONNECTION_POINT_RADIUS / 3);
            painter->setBrush(Qt::white);
        } else {
            // Draw simple circle
            painter->drawEllipse(output_pt, CONNECTION_POINT_RADIUS, CONNECTION_POINT_RADIUS);
        }
    }
}

void DAGNodeItem::setState(NodeState state)
{
    state_ = state;
    update();
}

void DAGNodeItem::setStageName(const std::string& stage_name)
{
    stage_name_ = stage_name;
    updatePortPositions();  // Recalculate ports for new stage type
    update();  // Redraw with new name and ports
}

void DAGNodeItem::setDisplayName(const std::string& display_name)
{
    display_name_ = display_name;
    update();  // Redraw with new name
}

void DAGNodeItem::setSourceInfo(int source_number, const QString& source_name)
{
    source_number_ = source_number;
    source_name_ = source_name;
    update();  // Redraw with source info
}

void DAGNodeItem::setParameters(const std::map<std::string, orc::ParameterValue>& params)
{
    parameters_ = params;
}

void DAGNodeItem::updatePortPositions()
{
    // Simplified - we just store single positions
    input_port_positions_.clear();
    output_port_positions_.clear();
    
    NodeTypeHelper::NodeVisualInfo visual_info = NodeTypeHelper::getVisualInfo(stage_name_);
    
    if (visual_info.has_input) {
        input_port_positions_.push_back(NodeTypeHelper::getInputPortPosition(WIDTH, HEIGHT));
    }
    
    if (visual_info.has_output) {
        output_port_positions_.push_back(NodeTypeHelper::getOutputPortPosition(WIDTH, HEIGHT));
    }
}

QPointF DAGNodeItem::inputConnectionPoint(int port_index) const
{
    Q_UNUSED(port_index);
    // Always return center-left
    return pos() + QPointF(0, HEIGHT / 2);
}

QPointF DAGNodeItem::outputConnectionPoint(int port_index) const
{
    Q_UNUSED(port_index);
    // Always return center-right
    return pos() + QPointF(WIDTH, HEIGHT / 2);
}

int DAGNodeItem::findNearestInputPort(const QPointF& scene_pos) const
{
    if (input_port_positions_.empty()) return -1;
    
    QPointF input_pt = pos() + QPointF(0, HEIGHT / 2);
    qreal distance = (scene_pos - input_pt).manhattanLength();
    
    if (distance < CONNECTION_POINT_RADIUS * 3) {
        return 0;  // Always return port 0 if within range
    }
    
    return -1;
}

int DAGNodeItem::findNearestOutputPort(const QPointF& scene_pos) const
{
    if (output_port_positions_.empty()) return -1;
    
    QPointF output_pt = pos() + QPointF(WIDTH, HEIGHT / 2);
    qreal distance = (scene_pos - output_pt).manhattanLength();
    
    if (distance < CONNECTION_POINT_RADIUS * 3) {
        return 0;  // Always return port 0 if within range
    }
    
    return -1;
}

bool DAGNodeItem::isNearInputPoint(const QPointF& scene_pos) const
{
    return findNearestInputPort(scene_pos) >= 0;
}

bool DAGNodeItem::isNearOutputPoint(const QPointF& scene_pos) const
{
    return findNearestOutputPort(scene_pos) >= 0;
}

void DAGNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // Check if clicking near a connection point to start edge creation
    if (isNearOutputPoint(event->scenePos())) {
        is_dragging_connection_ = true;
        event->accept();
        // Signal will be sent from DAGViewerWidget
        return;
    }
    
    QGraphicsItem::mousePressEvent(event);
}

void DAGNodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (is_dragging_connection_) {
        // Edge dragging handled by DAGViewerWidget
        event->accept();
        return;
    }
    
    QGraphicsItem::mouseMoveEvent(event);
}

void DAGNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (is_dragging_connection_) {
        is_dragging_connection_ = false;
        event->accept();
        return;
    }
    
    QGraphicsItem::mouseReleaseEvent(event);
}

void DAGNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    Q_UNUSED(event);
    // Signal will be emitted from DAGViewerWidget when it detects double-click
    QGraphicsItem::mouseDoubleClickEvent(event);
}

QVariant DAGNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged && scene()) {
        // Update only connected edges for better performance
        for (QGraphicsItem* item : scene()->items()) {
            if (auto* edge = dynamic_cast<DAGEdgeItem*>(item)) {
                if (edge->source() == this || edge->target() == this) {
                    edge->updatePosition();
                }
            }
        }
        
        // Notify viewer of position change to save to project
        if (viewer_) {
            QPointF new_pos = pos();
            viewer_->onNodePositionChanged(node_id_, new_pos.x(), new_pos.y());
        }
    }
    return QGraphicsItem::itemChange(change, value);
}

// ============================================================================
// DAGEdgeItem Implementation
// ============================================================================

DAGEdgeItem::DAGEdgeItem(DAGNodeItem* source, DAGNodeItem* target, QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , source_(source)
    , target_(target)
    , being_deleted_(false)
{
    setZValue(-1);  // Draw behind nodes
    setFlag(QGraphicsItem::ItemIsSelectable);
}

QRectF DAGEdgeItem::boundingRect() const
{
    if (being_deleted_ || !source_ || !target_) return QRectF();
    
    QPointF p1 = source_->outputConnectionPoint();
    QPointF p2 = target_->inputConnectionPoint();
    
    // Calculate control points for bezier curve (same as in paint())
    qreal control_offset = std::abs(p2.x() - p1.x()) * 0.4;
    QPointF c1(p1.x() + control_offset, p1.y());
    QPointF c2(p2.x() - control_offset, p2.y());
    
    // Find min/max x and y that includes all points (p1, p2, c1, c2)
    qreal minX = std::min({p1.x(), p2.x(), c1.x(), c2.x()});
    qreal maxX = std::max({p1.x(), p2.x(), c1.x(), c2.x()});
    qreal minY = std::min({p1.y(), p2.y(), c1.y(), c2.y()});
    qreal maxY = std::max({p1.y(), p2.y(), c1.y(), c2.y()});
    
    // Add padding for arrow head and line width
    qreal extra = 15;
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-extra, -extra, extra, extra);
}

void DAGEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    if (being_deleted_ || !source_ || !target_) return;
    
    QPointF p1 = source_->outputConnectionPoint();
    QPointF p2 = target_->inputConnectionPoint();
    
    // Draw curved edge
    QColor edge_color = isSelected() ? QColor(100, 150, 255) : Qt::darkGray;
    qreal edge_width = isSelected() ? 3.0 : 2.0;
    painter->setPen(QPen(edge_color, edge_width));
    painter->setBrush(Qt::NoBrush);
    
    QPainterPath path;
    path.moveTo(p1);
    
    // Control points for horizontal bezier curve
    qreal control_offset = std::abs(p2.x() - p1.x()) * 0.4;
    QPointF c1(p1.x() + control_offset, p1.y());
    QPointF c2(p2.x() - control_offset, p2.y());
    
    path.cubicTo(c1, c2, p2);
    painter->drawPath(path);
    
    // Draw arrow head
    QLineF line(c2, p2);
    double angle = std::atan2(-line.dy(), line.dx());
    
    QPointF arrow_p1 = p2 - QPointF(sin(angle + M_PI / 3) * 10,
                                     cos(angle + M_PI / 3) * 10);
    QPointF arrow_p2 = p2 - QPointF(sin(angle + M_PI - M_PI / 3) * 10,
                                     cos(angle + M_PI - M_PI / 3) * 10);
    
    QPolygonF arrow_head;
    arrow_head << p2 << arrow_p1 << arrow_p2;
    painter->setBrush(edge_color);
    painter->drawPolygon(arrow_head);
}

void DAGEdgeItem::updatePosition()
{
    if (!isValid()) return;
    
    // Call prepareGeometryChange() first, before the geometry actually changes
    // This is required by Qt to properly update the BSP tree
    prepareGeometryChange();
    
    // Just trigger a repaint - boundingRect() will be recalculated automatically
    update();
}

void DAGEdgeItem::invalidate()
{
    being_deleted_ = true;
    source_ = nullptr;
    target_ = nullptr;
    setVisible(false);  // Hide immediately
    // Don't call update() - item should be removed from scene before this is called
}

// ============================================================================
// TemporaryEdgeLine Implementation
// ============================================================================

TemporaryEdgeLine::TemporaryEdgeLine(QGraphicsItem* parent)
    : QGraphicsLineItem(parent)
{
    setPen(QPen(QColor(100, 100, 255), 2, Qt::DashLine));
    setZValue(-0.5);
}

// ============================================================================
// DAGViewerWidget Implementation
// ============================================================================

DAGViewerWidget::DAGViewerWidget(QWidget* parent)
    : QGraphicsView(parent)
    , scene_(new QGraphicsScene(this))
    , has_dag_(false)
    , project_(nullptr)
    , is_creating_edge_(false)
    , edge_source_node_(nullptr)
    , temp_edge_line_(nullptr)
{
    setScene(scene_);
    
    // Disable BSP tree indexing to avoid crashes during geometry changes
    // This makes item lookups slightly slower but prevents BSP tree corruption
    // when items are moved/updated during paint events
    scene_->setItemIndexMethod(QGraphicsScene::NoIndex);
    
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);  // Handle dragging manually
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setMouseTracking(true);
    
    // Set background
    setBackgroundBrush(QColor(250, 250, 250));
    
    connect(scene_, &QGraphicsScene::selectionChanged, 
            this, &DAGViewerWidget::onSceneSelectionChanged);
    
    // Connect double-click handling
    connect(this, &QWidget::customContextMenuRequested,
            this, &DAGViewerWidget::showContextMenu);
    
    // Initialize with start node
    initializeWithStartNode();
}

DAGViewerWidget::~DAGViewerWidget()
{
    // Disconnect from scene to prevent signals during destruction
    if (scene_) {
        disconnect(scene_, nullptr, this, nullptr);
    }
    
    // Clear the DAG to properly clean up all items
    clearDAG();
}

void DAGViewerWidget::setProject(orc::Project* project)
{
    project_ = project;
}

void DAGViewerWidget::initializeWithStartNode()
{
    clearDAG();
    
    // Create permanent start node near left edge of workspace
    // Use default source stage from core
    std::string source_stage = orc::StageRegistry::get_default_source_stage();
    auto* start_node = new DAGNodeItem("start_0", source_stage, true);
    start_node->setViewer(this);
    start_node->setDisplayName("TBC Source");
    start_node->setPos(-450, 0);  // Position near left edge (50 pixels from edge)
    scene_->addItem(start_node);
    node_items_["start_0"] = start_node;
    
    scene_->setSceneRect(-500, -500, 1000, 1000);
}

// Note: setDAG() removed - viewer works with GUIDAG from project, not executable DAG
// The executable DAG is owned by GUIProject and accessed only for field rendering

void DAGViewerWidget::clearDAG()
{
    // Cancel any ongoing edge creation
    is_creating_edge_ = false;
    edge_source_node_ = nullptr;
    
    // Delete temp edge line if it exists
    if (temp_edge_line_ && scene_) {
        scene_->removeItem(temp_edge_line_);
        delete temp_edge_line_;
        temp_edge_line_ = nullptr;
    }
    
    // First, delete all edges (which reference nodes)
    for (auto* edge : edge_items_) {
        if (edge) {
            if (scene_) scene_->removeItem(edge);
            delete edge;
        }
    }
    edge_items_.clear();
    
    // Then delete all nodes
    for (auto& pair : node_items_) {
        if (pair.second) {
            if (scene_) scene_->removeItem(pair.second);
            delete pair.second;
        }
    }
    node_items_.clear();
    
    has_dag_ = false;
}

void DAGViewerWidget::setNodeState(const std::string& node_id, NodeState state)
{
    DAGNodeItem* node = findNodeById(node_id);
    if (node) {
        node->setState(state);
    }
}

void DAGViewerWidget::setNodeStageType(const std::string& node_id, const std::string& stage_name)
{
    if (!project_) {
        return;
    }
    
    DAGNodeItem* node = findNodeById(node_id);
    if (!node) {
        return;
    }
    
    // Update in project first
    try {
        orc::project_io::change_node_type(*project_, node_id, stage_name);
    } catch (const std::exception& e) {
        QMessageBox::warning(nullptr, "Change Node Type Failed",
            QString("Failed to change node type: %1").arg(e.what()));
        return;
    }
    
    // Update GUI representation
    node->setStageName(stage_name);
    
    // Update display name from node type info
    const orc::NodeTypeInfo* type_info = orc::get_node_type_info(stage_name);
    if (type_info) {
        node->setDisplayName(type_info->display_name);
    }
    
    // Notify that DAG was modified
    emit dagModified();
}

std::string DAGViewerWidget::getNodeStageType(const std::string& node_id) const
{
    DAGNodeItem* node = findNodeById(node_id);
    if (node) {
        return node->stageName();
    }
    return "";
}

void DAGViewerWidget::setNodeParameters(const std::string& node_id, const std::map<std::string, orc::ParameterValue>& params)
{
    if (!project_) {
        return;
    }
    
    DAGNodeItem* node = findNodeById(node_id);
    if (!node) {
        return;
    }
    
    // Update in project first
    try {
        orc::project_io::set_node_parameters(*project_, node_id, params);
    } catch (const std::exception& e) {
        QMessageBox::warning(nullptr, "Set Parameters Failed",
            QString("Failed to set node parameters: %1").arg(e.what()));
        return;
    }
    
    // Update GUI representation
    node->setParameters(params);
    
    // Notify that DAG was modified
    emit dagModified();
}

std::map<std::string, orc::ParameterValue> DAGViewerWidget::getNodeParameters(const std::string& node_id) const
{
    DAGNodeItem* node = findNodeById(node_id);
    if (node) {
        return node->getParameters();
    }
    return {};
}

void DAGViewerWidget::setSourceInfo(int source_number, const QString& source_name)
{
    // Find START nodes and update their source info
    for (auto& [node_id, node_item] : node_items_) {
        if (node_item && node_item->isSourceNode()) {
            node_item->setSourceInfo(source_number, source_name);
        }
    }
}

void DAGViewerWidget::arrangeToGrid()
{
    if (node_items_.empty()) return;
    
    // Disable scene indexing during bulk repositioning to avoid BSP tree issues
    QGraphicsScene::ItemIndexMethod old_index_method = QGraphicsScene::BspTreeIndex;
    if (scene_) {
        old_index_method = scene_->itemIndexMethod();
        scene_->setItemIndexMethod(QGraphicsScene::NoIndex);
    }
    
    // Build adjacency list from current edges
    std::map<std::string, std::vector<std::string>> adj_list;  // node_id -> output nodes
    std::map<std::string, int> in_degree;
    
    // Initialize all nodes
    for (const auto& [node_id, node_item] : node_items_) {
        adj_list[node_id] = {};
        in_degree[node_id] = 0;
    }
    
    // Build graph from edges
    for (const auto* edge : edge_items_) {
        if (!edge || !isEdgeValid(edge)) continue;
        
        std::string source_id = edge->source()->nodeId();
        std::string target_id = edge->target()->nodeId();
        
        adj_list[source_id].push_back(target_id);
        in_degree[target_id]++;
    }
    
    // Topological sort using BFS (Kahn's algorithm)
    std::queue<std::string> queue;
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(node_id);
        }
    }
    
    std::vector<std::string> sorted_nodes;
    while (!queue.empty()) {
        std::string node_id = queue.front();
        queue.pop();
        sorted_nodes.push_back(node_id);
        
        for (const auto& neighbor : adj_list[node_id]) {
            in_degree[neighbor]--;
            if (in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }
    
    // Calculate levels (column positions) based on longest path from source
    std::map<std::string, int> node_levels;
    for (const auto& node_id : sorted_nodes) {
        int level = 0;
        
        // Find maximum level of all input nodes
        for (const auto* edge : edge_items_) {
            if (!edge || !isEdgeValid(edge)) continue;
            if (edge->target()->nodeId() == node_id) {
                std::string source_id = edge->source()->nodeId();
                if (node_levels.count(source_id)) {
                    level = std::max(level, node_levels[source_id] + 1);
                }
            }
        }
        
        node_levels[node_id] = level;
    }
    
    // Group nodes by level (column)
    std::map<int, std::vector<std::string>> columns;
    for (const auto& [node_id, level] : node_levels) {
        columns[level].push_back(node_id);
    }
    
    // Layout parameters
    constexpr double COLUMN_SPACING = 250.0;  // Horizontal distance between columns
    constexpr double ROW_SPACING = 120.0;     // Vertical distance between nodes
    constexpr double START_X = -450.0;        // Left-justified starting position
    constexpr double START_Y = 0.0;           // Vertical center
    
    // Position nodes column by column
    for (const auto& [level, node_ids] : columns) {
        double x = START_X + level * COLUMN_SPACING;
        
        // Center nodes vertically in this column
        double total_height = (node_ids.size() - 1) * ROW_SPACING;
        double start_y = START_Y - total_height / 2.0;
        
        for (size_t i = 0; i < node_ids.size(); ++i) {
            const auto& node_id = node_ids[i];
            auto* node_item = findNodeById(node_id);
            
            if (node_item) {
                double y = start_y + i * ROW_SPACING;
                node_item->setPos(x, y);
            }
        }
    }
    
    // Update all edges after repositioning
    for (auto* edge : edge_items_) {
        if (edge && isEdgeValid(edge)) {
            edge->updatePosition();
        }
    }
    
    // Re-enable scene indexing
    if (scene_) {
        scene_->setItemIndexMethod(old_index_method);
    }
    
    // Scene will update automatically - do not force immediate update
}

void DAGViewerWidget::onNodePositionChanged(const std::string& node_id, double x, double y)
{
    if (!project_) {
        return;
    }
    
    // Update position in project
    try {
        orc::project_io::set_node_position(*project_, node_id, x, y);
        // Position changes mark project as modified, and we emit signal
        emit dagModified();
    } catch (const std::exception& e) {
        // Silently ignore errors - position updates shouldn't block UI
    }
}

orc::GUIDAG DAGViewerWidget::exportDAG() const
{
    orc::GUIDAG dag;
    dag.name = "Untitled DAG";
    dag.version = "1.0";
    
    // Export nodes
    for (const auto& [node_id, node_item] : node_items_) {
        orc::GUIDAGNode node;
        node.node_id = node_id;
        node.stage_name = node_item->stageName();
        
        // Get node type from stage name
        auto* type_info = orc::get_node_type_info(node.stage_name);
        node.node_type = type_info ? type_info->type : orc::NodeType::TRANSFORM;
        
        node.display_name = node_item->displayName();
        node.x_position = node_item->pos().x();
        node.y_position = node_item->pos().y();
        node.parameters = node_item->getParameters();
        dag.nodes.push_back(node);
    }
    
    // Export edges
    for (const auto& edge_item : edge_items_) {
        orc::GUIDAGEdge edge;
        edge.source_node_id = edge_item->source()->nodeId();
        edge.target_node_id = edge_item->target()->nodeId();
        dag.edges.push_back(edge);
    }
    
    return dag;
}

void DAGViewerWidget::importDAG(const orc::GUIDAG& dag)
{
    // Clear existing DAG
    clearDAG();
    
    // Import nodes
    for (const auto& node : dag.nodes) {
        auto* node_item = new DAGNodeItem(
            node.node_id,
            node.stage_name,
            node.node_type == orc::NodeType::SOURCE  // Check node type, not stage name
        );
        node_item->setViewer(this);
        
        // Set display name from DAG (provided by core)
        if (!node.display_name.empty()) {
            node_item->setDisplayName(node.display_name);
        }
        
        node_item->setParameters(node.parameters);
        
        scene_->addItem(node_item);
        node_item->setPos(node.x_position, node.y_position);
        node_items_[node.node_id] = node_item;
    }
    
    // Import edges
    for (const auto& edge : dag.edges) {
        auto source_it = node_items_.find(edge.source_node_id);
        auto target_it = node_items_.find(edge.target_node_id);
        
        if (source_it != node_items_.end() && target_it != node_items_.end()) {
            auto* edge_item = new DAGEdgeItem(source_it->second, target_it->second);
            scene_->addItem(edge_item);
            edge_items_.push_back(edge_item);
        }
    }
}

void DAGViewerWidget::onSceneSelectionChanged()
{
    auto selected = scene_->selectedItems();
    if (!selected.isEmpty()) {
        if (auto* node_item = dynamic_cast<DAGNodeItem*>(selected.first())) {
            emit nodeSelected(node_item->nodeId());
        }
    }
}

void DAGViewerWidget::showContextMenu(const QPoint& pos)
{
    QPoint global_pos = mapToGlobal(pos);
    QPointF scene_pos = mapToScene(pos);
    
    QGraphicsItem* item = scene_->itemAt(scene_pos, transform());
    DAGNodeItem* node_item = dynamic_cast<DAGNodeItem*>(item);
    DAGEdgeItem* edge_item = dynamic_cast<DAGEdgeItem*>(item);
    
    QMenu menu;
    
    if (edge_item && isEdgeValid(edge_item)) {
        // Edge-specific actions
        // Capture edge pointer and validate before use
        menu.addAction("Delete Edge", [this, edge_item]() {
            if (isEdgeValid(edge_item)) {
                deleteEdge(edge_item);
            }
        });
    } else if (node_item && isNodeValid(node_item) && !node_item->isSourceNode()) {
        // Node-specific actions (not for start node)
        // Capture node_id by value to avoid accessing deleted pointer
        std::string node_id = node_item->nodeId();
        std::string stage_name = node_item->stageName();
        
        // Check if node type can be changed (requires project connection)
        bool can_change_type = false;
        std::string change_type_reason;
        if (project_) {
            can_change_type = orc::project_io::can_change_node_type(*project_, node_id, &change_type_reason);
        }
        
        auto* change_type_action = menu.addAction("Change Node Type...", [this, node_id]() {
            if (findNodeById(node_id)) {
                emit changeNodeTypeRequested(node_id);
            }
        });
        change_type_action->setEnabled(can_change_type);
        if (!can_change_type && !change_type_reason.empty()) {
            change_type_action->setToolTip(QString::fromStdString(change_type_reason));
        }
        
        // Check if stage has parameters (by checking if it implements ParameterizedStage)
        bool has_parameters = false;
        auto& registry = orc::StageRegistry::instance();
        if (registry.has_stage(stage_name)) {
            try {
                auto stage = registry.create_stage(stage_name);
                if (stage) {
                    auto* param_stage = dynamic_cast<orc::ParameterizedStage*>(stage.get());
                    if (param_stage) {
                        auto descriptors = param_stage->get_parameter_descriptors();
                        has_parameters = !descriptors.empty();
                    }
                }
            } catch (...) {
                // Failed to check - assume no parameters
                has_parameters = false;
            }
        }
        
        auto* edit_params_action = menu.addAction("Edit Parameters...", [this, node_id]() {
            if (findNodeById(node_id)) {
                emit editParametersRequested(node_id);
            }
        });
        edit_params_action->setEnabled(has_parameters);
        
        menu.addSeparator();
        
        // Check if node has connections
        bool has_connections = hasNodeConnections(node_id);
        auto* delete_action = menu.addAction("Delete Node", [this, node_id]() {
            if (findNodeById(node_id)) {
                deleteNode(node_id);
            }
        });
        delete_action->setEnabled(!has_connections);
        if (has_connections) {
            delete_action->setToolTip("Cannot delete node with connections. Delete edges first.");
        }
        menu.addSeparator();
    }
    
    // Add node action (always available)
    // Capture position by value to avoid dangling reference
    QPointF pos_copy = scene_pos;
    menu.addAction("Add Node", [this, pos_copy]() {
        addNodeAtPosition(pos_copy);
    });
    
    menu.exec(global_pos);
}

bool DAGViewerWidget::hasNodeConnections(const std::string& node_id) const
{
    // Check if the node has any incoming or outgoing edges
    for (const auto* edge : edge_items_) {
        if (!edge || !edge->isValid()) {
            continue;
        }
        if (edge->source() && edge->source()->nodeId() == node_id) {
            return true;  // Has outgoing edge
        }
        if (edge->target() && edge->target()->nodeId() == node_id) {
            return true;  // Has incoming edge
        }
    }
    return false;
}

bool DAGViewerWidget::isNodeValid(DAGNodeItem* node) const
{
    if (!node || !scene_) return false;
    
    // Check if node is still in the scene
    return scene_->items().contains(node);
}

bool DAGViewerWidget::isEdgeValid(const DAGEdgeItem* edge) const
{
    if (!edge || !scene_) return false;
    
    // Check if edge is still in scene and has valid endpoints
    return scene_->items().contains(const_cast<DAGEdgeItem*>(edge)) && edge->isValid();
}

DAGNodeItem* DAGViewerWidget::findNodeById(const std::string& node_id) const
{
    auto it = node_items_.find(node_id);
    if (it != node_items_.end() && isNodeValid(it->second)) {
        return it->second;
    }
    return nullptr;
}

void DAGViewerWidget::cleanupStalePointers()
{
    // Remove nodes that are no longer in scene
    std::vector<std::string> invalid_nodes;
    for (const auto& pair : node_items_) {
        if (!isNodeValid(pair.second)) {
            invalid_nodes.push_back(pair.first);
        }
    }
    for (const auto& node_id : invalid_nodes) {
        node_items_.erase(node_id);
    }
    
    // Remove edges that are no longer valid
    edge_items_.erase(
        std::remove_if(edge_items_.begin(), edge_items_.end(),
            [this](DAGEdgeItem* edge) { return !isEdgeValid(edge); }),
        edge_items_.end()
    );
    
    // Clear edge creation state if source node is invalid
    if (is_creating_edge_ && !isNodeValid(edge_source_node_)) {
        cancelEdgeDrag();
    }
}

std::string DAGViewerWidget::generateNodeId()
{
    // Generate unique ID by checking existing nodes
    int max_id = 0;
    
    for (const auto& [node_id, node_item] : node_items_) {
        // Check if node_id follows "node_N" pattern
        if (node_id.find("node_") == 0) {
            try {
                int id_num = std::stoi(node_id.substr(5));
                if (id_num > max_id) {
                    max_id = id_num;
                }
            } catch (...) {
                // Not a valid node_N format, skip
            }
        }
    }
    
    // Return next available ID
    return "node_" + std::to_string(max_id + 1);
}

bool DAGViewerWidget::addNode(const std::string& node_id, const std::string& stage_name, const QPointF& pos, std::string* error)
{
    if (!scene_) {
        if (error) *error = "Scene not initialized";
        return false;
    }
    
    // Check if node already exists
    if (node_items_.find(node_id) != node_items_.end()) {
        if (error) *error = "Node with ID '" + node_id + "' already exists";
        return false;
    }
    
    try {
        // Validate that stage exists
        const orc::NodeTypeInfo* type_info = orc::get_node_type_info(stage_name);
        if (!type_info) {
            if (error) *error = "Stage '" + stage_name + "' is not registered";
            std::cerr << "ERROR: Attempt to create node with unknown stage '" << stage_name << "'" << std::endl;
            return false;
        }
        
        // Ensure scene is in a good state
        scene_->clearSelection();
        scene_->update();
        
        auto* node = new DAGNodeItem(node_id, stage_name, false);
        if (!node) {
            if (error) *error = "Failed to allocate node";
            return false;
        }
        node->setViewer(this);
        node->setViewer(this);
        
        // Set proper display name from node type info
        node->setDisplayName(type_info->display_name);
        
        scene_->addItem(node);  // Add to scene BEFORE setPos to avoid crash
        node->setPos(pos);
        node_items_[node_id] = node;
        
        scene_->update();
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("Failed to create node: ") + e.what();
        return false;
    } catch (...) {
        if (error) *error = "Unknown error creating node";
        return false;
    }
}

void DAGViewerWidget::addNodeAtPosition(const QPointF& pos)
{
    if (!scene_) {
        QMessageBox::warning(nullptr, "Add Node Failed", 
            "Scene is not initialized");
        return;
    }
    
    if (!project_) {
        QMessageBox::warning(nullptr, "Add Node Failed", 
            "No project is connected");
        return;
    }
    
    // Clean up any invalid edges first
    cleanupInvalidEdges();
    
    // Process events to ensure clean state
    QApplication::processEvents();
    
    // Ensure scene is in clean state - clear focus and selection
    if (scene_) {
        scene_->clearSelection();
        scene_->clearFocus();
        scene_->update();
    }
    
    // Process events again after scene clearing
    QApplication::processEvents();
    
    // Create a default node using the default transform stage from core
    // User can later edit to select a different stage type
    std::string stage_name = orc::StageRegistry::get_default_transform_stage();
    
    try {
        std::string node_id = orc::project_io::add_node(*project_, stage_name, pos.x(), pos.y());
        
        // Now create the GUI representation
        std::string error;
        if (!addNode(node_id, stage_name, pos, &error)) {
            QMessageBox::warning(nullptr, "Add Node Failed", 
                QString("Failed to add node to GUI: %1").arg(QString::fromStdString(error)));
            return;
        }
        
        // Select the newly created node
        auto it = node_items_.find(node_id);
        if (it != node_items_.end() && it->second) {
            it->second->setSelected(true);
            emit nodeSelected(node_id);
        }
        
        // Notify that DAG was modified
        emit dagModified();
    } catch (const std::exception& e) {
        QMessageBox::warning(nullptr, "Add Node Failed", 
            QString("Failed to add node: %1").arg(e.what()));
    }
}

bool DAGViewerWidget::deleteNode(const std::string& node_id, std::string* error)
{
    if (!project_) {
        if (error) *error = "No project is connected";
        return false;
    }
    
    // Don't allow deleting start node
    if (node_id.find("start_") == 0) {
        if (error) *error = "Cannot delete source nodes";
        return false;
    }
    
    auto it = node_items_.find(node_id);
    if (it == node_items_.end()) {
        if (error) *error = "Node not found: " + node_id;
        return false;
    }
    
    DAGNodeItem* node_to_delete = it->second;
    if (!node_to_delete) {
        if (error) *error = "Invalid node pointer for: " + node_id;
        return false;
    }
    
    // Cancel edge creation if this node is involved
    if (edge_source_node_ == node_to_delete) {
        cancelEdgeDrag();
    }
    
    // Remove from project first (this will also remove connected edges in the project)
    try {
        orc::project_io::remove_node(*project_, node_id);
    } catch (const std::exception& e) {
        if (error) *error = std::string("Failed to remove node from project: ") + e.what();
        return false;
    }
    
    // Now clean up GUI representation
    // Collect edges that reference this node and invalidate them
    std::vector<size_t> edge_indices_to_remove;
    for (size_t i = 0; i < edge_items_.size(); ++i) {
        auto* edge = edge_items_[i];
        if (edge && edge->isValid()) {
            if (edge->source() == node_to_delete || edge->target() == node_to_delete) {
                edge->invalidate();  // Mark as invalid immediately
                edge_indices_to_remove.push_back(i);
            }
        }
    }
    
    // Remove edges in reverse order to maintain indices
    for (auto it = edge_indices_to_remove.rbegin(); it != edge_indices_to_remove.rend(); ++it) {
        size_t idx = *it;
        if (idx < edge_items_.size()) {
            auto* edge = edge_items_[idx];
            if (edge) {
                if (edge->isSelected()) edge->setSelected(false);
                if (scene_) {
                    scene_->removeItem(edge);
                    scene_->update();
                }
                delete edge;
            }
            edge_items_.erase(edge_items_.begin() + idx);
        }
    }
    
    // Deselect node if selected
    if (node_to_delete->isSelected()) {
        node_to_delete->setSelected(false);
    }
    
    // Remove node from map first
    node_items_.erase(it);
    
    // Remove from scene first
    if (scene_) {
        scene_->removeItem(node_to_delete);
    }
    
    // Delete the node
    delete node_to_delete;
    node_to_delete = nullptr;
    
    // Clean up any stale pointers (without processing events)
    cleanupStalePointers();
    
    // Notify that DAG was modified
    emit dagModified();
    
    return true;
}

bool DAGViewerWidget::deleteEdge(DAGEdgeItem* edge, std::string* error)
{
    if (!project_) {
        if (error) *error = "No project is connected";
        return false;
    }
    
    if (!edge) {
        if (error) *error = "Invalid edge pointer";
        return false;
    }
    
    // Validate edge is still in our tracking list
    auto it = std::find(edge_items_.begin(), edge_items_.end(), edge);
    if (it == edge_items_.end()) {
        // Edge already deleted
        return true;
    }
    
    // Validate edge is still in scene
    if (scene_ && !scene_->items().contains(edge)) {
        // Edge was removed from scene but still in our list - clean up
        edge_items_.erase(it);
        return true;
    }
    
    // Get edge IDs before deleting
    std::string source_id, target_id;
    if (edge->source()) source_id = edge->source()->nodeId();
    if (edge->target()) target_id = edge->target()->nodeId();
    
    // Remove from project first
    if (!source_id.empty() && !target_id.empty()) {
        try {
            orc::project_io::remove_edge(*project_, source_id, target_id);
        } catch (const std::exception& e) {
            if (error) *error = std::string("Failed to remove edge from project: ") + e.what();
            return false;
        }
    }
    
    // Clear any focus or selection on the edge
    if (edge->isSelected()) {
        edge->setSelected(false);
    }
    if (scene_ && scene_->focusItem() == edge) {
        scene_->setFocusItem(nullptr);
    }
    
    // Remove from scene FIRST
    if (scene_) {
        scene_->removeItem(edge);
    }
    
    // Remove from our tracking list
    edge_items_.erase(it);
    
    // Mark as invalid
    edge->invalidate();
    
    // Delete the object
    delete edge;
    
    // Clean up any stale pointers (without processing events)
    cleanupStalePointers();
    
    // Notify that DAG was modified
    emit dagModified();
    
    return true;
}

void DAGViewerWidget::deleteSelectedItems()
{
    if (!scene_) return;
    
    auto selected = scene_->selectedItems();
    if (selected.isEmpty()) return;
    
    // Collect items to delete - store node IDs instead of pointers
    std::vector<std::string> node_ids_to_delete;
    std::vector<DAGEdgeItem*> edges_to_delete;
    
    for (auto* item : selected) {
        if (auto* node = dynamic_cast<DAGNodeItem*>(item)) {
            if (!node->isSourceNode()) {
                node_ids_to_delete.push_back(node->nodeId());
            }
        } else if (auto* edge = dynamic_cast<DAGEdgeItem*>(item)) {
            // Verify edge is still in our list
            if (std::find(edge_items_.begin(), edge_items_.end(), edge) != edge_items_.end()) {
                edges_to_delete.push_back(edge);
            }
        }
    }
    
    std::vector<std::string> errors;
    
    // Delete edges first
    for (auto* edge : edges_to_delete) {
        std::string error;
        if (!deleteEdge(edge, &error)) {
            errors.push_back("Failed to delete edge: " + error);
        }
    }
    
    // Delete nodes by ID (this also deletes their connected edges)
    for (const auto& node_id : node_ids_to_delete) {
        std::string error;
        if (!deleteNode(node_id, &error)) {
            errors.push_back("Failed to delete node '" + node_id + "': " + error);
        }
    }
    
    // Report errors if any occurred
    if (!errors.empty()) {
        QString error_msg = "Some items could not be deleted:\n";
        for (const auto& err : errors) {
            error_msg += "\n• " + QString::fromStdString(err);
        }
        QMessageBox::warning(nullptr, "Deletion Errors", error_msg);
    }
}

void DAGViewerWidget::createEdge(const std::string& source_id, const std::string& target_id)
{
    if (!project_) {
        return;
    }
    
    DAGNodeItem* source_node = findNodeById(source_id);
    DAGNodeItem* target_node = findNodeById(target_id);
    
    if (!source_node || !target_node) {
        return;
    }
    
    // Clean up any invalid edges and check if edge already exists
    std::vector<DAGEdgeItem*> edges_to_remove;
    bool edge_exists = false;
    
    for (auto* edge : edge_items_) {
        if (!edge || !edge->isValid()) {
            edges_to_remove.push_back(edge);
            continue;
        }
        
        // Check if this edge already exists
        if (edge->source() && edge->target() &&
            edge->source()->nodeId() == source_id && 
            edge->target()->nodeId() == target_id) {
            edge_exists = true;
            break;
        }
    }
    
    // Remove invalid edges
    for (auto* edge : edges_to_remove) {
        edge_items_.erase(std::remove(edge_items_.begin(), edge_items_.end(), edge), edge_items_.end());
        if (edge) {
            if (scene_) scene_->removeItem(edge);
            delete edge;
        }
    }
    
    if (edge_exists) {
        return;  // Edge already exists
    }
    
    // Try to add edge to project first (this validates the connection)
    try {
        orc::project_io::add_edge(*project_, source_id, target_id);
    } catch (const std::exception& e) {
        // Connection invalid - silently ignore (core already validated)
        return;
    }
    
    // Connection valid, create GUI representation
    auto* edge = new DAGEdgeItem(source_node, target_node);
    scene_->addItem(edge);
    edge_items_.push_back(edge);
    
    emit edgeCreated(source_id, target_id);
    emit dagModified();
}

int DAGViewerWidget::countInputConnections(const std::string& node_id) const
{
    int count = 0;
    for (const auto* edge : edge_items_) {
        if (edge && edge->isValid() && edge->target() && 
            edge->target()->nodeId() == node_id) {
            count++;
        }
    }
    return count;
}

int DAGViewerWidget::countOutputConnections(const std::string& node_id) const
{
    int count = 0;
    for (const auto* edge : edge_items_) {
        if (edge && edge->isValid() && edge->source() && 
            edge->source()->nodeId() == node_id) {
            count++;
        }
    }
    return count;
}

void DAGViewerWidget::cleanupInvalidEdges()
{
    // Create a new vector with only valid edges
    std::vector<DAGEdgeItem*> valid_edges;
    std::vector<DAGEdgeItem*> edges_to_delete;
    
    for (auto* edge : edge_items_) {
        if (!edge) {
            // Null pointer, skip
            continue;
        }
        
        // Try to check if it's valid - wrap in try-catch in case of deleted memory
        bool is_valid = false;
        try {
            is_valid = edge->isValid();
        } catch (...) {
            // If we get any exception, assume it's invalid
            is_valid = false;
        }
        
        if (is_valid) {
            valid_edges.push_back(edge);
        } else {
            edges_to_delete.push_back(edge);
        }
    }
    
    // Replace edge list with only valid edges
    edge_items_ = valid_edges;
    
    // Now safely delete invalid edges
    for (auto* edge : edges_to_delete) {
        if (edge) {
            try {
                if (scene_) {
                    scene_->removeItem(edge);
                    scene_->update();
                }
                delete edge;
            } catch (...) {
                // Ignore errors during cleanup
            }
        }
    }
    
    // Process events to flush Qt's internal caches
    QApplication::processEvents();
}

void DAGViewerWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF scene_pos = mapToScene(event->pos());
        QGraphicsItem* item = scene_->itemAt(scene_pos, transform());
        
        if (auto* node_item = dynamic_cast<DAGNodeItem*>(item)) {
            if (node_item->isNearOutputPoint(scene_pos)) {
                startEdgeDrag(node_item, scene_pos);
                return;
            }
        }
    }
    
    QGraphicsView::mousePressEvent(event);
}

void DAGViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (is_creating_edge_) {
        QPointF scene_pos = mapToScene(event->pos());
        updateEdgeDrag(scene_pos);
        return;
    }
    
    QGraphicsView::mouseMoveEvent(event);
}

void DAGViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (is_creating_edge_ && event->button() == Qt::LeftButton) {
        QPointF scene_pos = mapToScene(event->pos());
        finishEdgeDrag(scene_pos);
        return;
    }
    
    QGraphicsView::mouseReleaseEvent(event);
}

void DAGViewerWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedItems();
        event->accept();
        return;
    }
    
    QGraphicsView::keyPressEvent(event);
}

void DAGViewerWidget::startEdgeDrag(DAGNodeItem* source_node, const QPointF& start_pos)
{
    if (!isNodeValid(source_node)) return;
    
    is_creating_edge_ = true;
    edge_source_node_ = source_node;
    
    temp_edge_line_ = new TemporaryEdgeLine();
    temp_edge_line_->setLine(QLineF(start_pos, start_pos));
    scene_->addItem(temp_edge_line_);
}

void DAGViewerWidget::updateEdgeDrag(const QPointF& current_pos)
{
    if (!temp_edge_line_ || !isNodeValid(edge_source_node_)) {
        cancelEdgeDrag();
        return;
    }
    
    QPointF start = edge_source_node_->outputConnectionPoint();
    temp_edge_line_->setLine(QLineF(start, current_pos));
}

void DAGViewerWidget::finishEdgeDrag(const QPointF& end_pos)
{
    if (!isNodeValid(edge_source_node_)) {
        cancelEdgeDrag();
        return;
    }
    
    // Find target node
    QGraphicsItem* item = scene_->itemAt(end_pos, transform());
    DAGNodeItem* target_node = dynamic_cast<DAGNodeItem*>(item);
    
    if (target_node && 
        target_node != edge_source_node_ &&
        target_node->isNearInputPoint(end_pos)) {
        // Create edge
        createEdge(edge_source_node_->nodeId(), target_node->nodeId());
    }
    
    cancelEdgeDrag();
}

void DAGViewerWidget::cancelEdgeDrag()
{
    if (temp_edge_line_) {
        scene_->removeItem(temp_edge_line_);
        delete temp_edge_line_;
        temp_edge_line_ = nullptr;
    }
    
    is_creating_edge_ = false;
    edge_source_node_ = nullptr;
}

void DAGViewerWidget::buildGraphicsItems(const orc::DAG& dag)
{
    // Create node items
    for (const auto& node : dag.nodes()) {
        auto* item = new DAGNodeItem(node.node_id, node.stage->get_node_type_info().stage_name);
        scene_->addItem(item);
        node_items_[node.node_id] = item;
    }
    
    // Create edge items
    for (const auto& node : dag.nodes()) {
        auto* target = node_items_[node.node_id];
        
        for (const auto& input_id : node.input_node_ids) {
            auto it = node_items_.find(input_id);
            if (it != node_items_.end()) {
                auto* source = it->second;
                auto* edge = new DAGEdgeItem(source, target);
                scene_->addItem(edge);
                edge_items_.push_back(edge);
            }
        }
    }
}

void DAGViewerWidget::layoutNodes(const orc::DAG& dag)
{
    // Simple hierarchical layout using topological sort
    auto sorted_nodes = topologicalSort(dag);
    
    // Assign levels (depth in DAG)
    std::map<std::string, int> node_levels;
    auto node_index = dag.build_node_index();
    
    for (const auto& node_id : sorted_nodes) {
        const auto& node = dag.nodes()[node_index[node_id]];
        
        int level = 0;
        for (const auto& input_id : node.input_node_ids) {
            if (node_levels.count(input_id)) {
                level = std::max(level, node_levels[input_id] + 1);
            }
        }
        node_levels[node_id] = level;
    }
    
    // Group nodes by level
    std::map<int, std::vector<std::string>> levels;
    for (const auto& [node_id, level] : node_levels) {
        levels[level].push_back(node_id);
    }
    
    // Position nodes
    constexpr double HORIZONTAL_SPACING = 200.0;
    constexpr double VERTICAL_SPACING = 150.0;
    
    for (const auto& [level, nodes] : levels) {
        double x_offset = -(nodes.size() - 1) * HORIZONTAL_SPACING / 2.0;
        
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node_id = nodes[i];
            auto* item = node_items_[node_id];
            
            double x = x_offset + i * HORIZONTAL_SPACING;
            double y = level * VERTICAL_SPACING;
            
            item->setPos(x, y);
        }
    }
    
    // Update all edges
    for (auto* edge : edge_items_) {
        edge->updatePosition();
    }
}

std::vector<std::string> DAGViewerWidget::topologicalSort(const orc::DAG& dag) const
{
    auto node_index = dag.build_node_index();
    
    // Calculate in-degrees
    std::map<std::string, size_t> in_degree;
    for (const auto& node : dag.nodes()) {
        in_degree[node.node_id] = 0;
    }
    for (const auto& node : dag.nodes()) {
        for (const auto& input_id : node.input_node_ids) {
            in_degree[input_id]++;
        }
    }
    
    // Kahn's algorithm
    std::queue<std::string> queue;
    for (const auto& [node_id, degree] : in_degree) {
        if (degree == 0) {
            queue.push(node_id);
        }
    }
    
    std::vector<std::string> result;
    while (!queue.empty()) {
        std::string node_id = queue.front();
        queue.pop();
        result.push_back(node_id);
        
        const auto& node = dag.nodes()[node_index[node_id]];
        for (const auto& input_id : node.input_node_ids) {
            in_degree[input_id]--;
            if (in_degree[input_id] == 0) {
                queue.push(input_id);
            }
        }
    }
    
    // Reverse for display (root at top)
    std::reverse(result.begin(), result.end());
    
    return result;
}
