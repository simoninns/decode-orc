/*
 * File:        orcnodepainter.cpp
 * Module:      orc-gui
 * Purpose:     Custom node painter with proper "one" vs "many" port visualization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "orcnodepainter.h"
#include "orcgraphmodel.h"
#include "node_type_helper.h"
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/AbstractGraphModel.hpp>
#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/NodeState.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionIdUtils.hpp>
#include <QtNodes/StyleCollection>
#include <QtNodes/NodeData>
#include <QJsonDocument>

using namespace QtNodes;

void OrcNodePainter::paint(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // Draw everything using base class and custom versions
    drawNodeRect(painter, ngo);
    drawConnectionPointsCustom(painter, ngo);  // Use our custom version
    drawFilledConnectionPointsCustom(painter, ngo);  // Use our custom version
    drawNodeCaption(painter, ngo);  // Use our custom version with text wrapping
    drawNodeId(painter, ngo);  // Draw node ID in lower left corner
    drawEntryLabels(painter, ngo);
    drawResizeRect(painter, ngo);
    drawProcessingIndicator(painter, ngo);
    drawValidationIcon(painter, ngo);
}

void OrcNodePainter::drawNodeCaption(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    if (!model.nodeData(nodeId, NodeRole::CaptionVisible).toBool())
        return;

    QString const name = model.nodeData(nodeId, NodeRole::Caption).toString();

    QFont f = painter->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() - 1);

    // Get just the node size (not bounding rect which includes connection points)
    QSizeF nodeSize = geometry.size(nodeId);

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    painter->setFont(f);
    painter->setPen(nodeStyle.FontColor);
    
    // Calculate font height for positioning
    QFontMetrics fm(f);
    double fontHeight = fm.height();
    // Small offset to clear the top border/rounded corner
    double verticalOffset = 8.0;
    
    // Create a bounding rectangle for text wrapping with generous padding
    // Node rect is at (0, 0, width, height) - use size directly
    double horizontalPadding = 15.0;
    double verticalPadding = 10.0;
    QRectF textRect(horizontalPadding, 
                    verticalOffset, 
                    nodeSize.width() - 2 * horizontalPadding, 
                    nodeSize.height() - verticalOffset - verticalPadding);
    
    // Draw text with word wrapping and top-center alignment
    painter->drawText(textRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, name);

    f.setBold(false);
    painter->setFont(f);
}

void OrcNodePainter::drawNodeId(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    // Get the ORC node ID
    auto* orcModel = dynamic_cast<OrcGraphModel*>(&model);
    if (!orcModel) {
        return;  // Not an ORC model, nothing to draw
    }
    
    orc::NodeID orcNodeId = orcModel->getOrcNodeId(nodeId);
    if (!orcNodeId.is_valid()) {
        return;  // Invalid node ID
    }
    
    // Get node size
    QSizeF nodeSize = geometry.size(nodeId);
    
    // Setup font - smaller and not bold
    QFont f = painter->font();
    f.setBold(false);
    f.setPointSize(f.pointSize() - 1);  // Slightly smaller font
    
    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());
    
    painter->setFont(f);
    
    // Use a lighter color for the node ID
    QColor idColor = nodeStyle.FontColor;
    idColor.setAlpha(160);  // Make it slightly transparent
    painter->setPen(idColor);
    
    // Draw the node ID in the lower left corner
    QString nodeIdText = QString::fromStdString(orcNodeId.to_string());
    QFontMetrics fm(f);
    
    // Position in lower left with small padding
    double leftPadding = 8.0;
    double bottomPadding = 5.0;
    double textY = nodeSize.height() - bottomPadding;
    
    // Draw the text
    painter->drawText(QPointF(leftPadding, textY), nodeIdText);
    
    // Reset font
    f.setPointSize(f.pointSize() + 1);
    painter->setFont(f);
}

void OrcNodePainter::drawConnectionPointsCustom(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    auto diameter = nodeStyle.ConnectionPointDiameter;
    auto reducedDiameter = diameter * 0.6;

    auto const &connectionStyle = StyleCollection::connectionStyle();

    // Get ORC node info to determine if ports are "many"
    auto* orcModel = dynamic_cast<OrcGraphModel*>(&model);
    NodeTypeHelper::NodeVisualInfo visualInfo{true, true, false, false};  // Default
    
    if (orcModel) {
        orc::NodeID orcNodeId = orcModel->getOrcNodeId(nodeId);
        if (orcNodeId.is_valid()) {
            std::string stage_name = orcModel->getNodeStageName(orcNodeId);
            if (!stage_name.empty()) {
                visualInfo = NodeTypeHelper::getVisualInfo(stage_name);
            }
        }
    }

    for (PortType portType : {PortType::Out, PortType::In}) {
        size_t const n = model
                             .nodeData(nodeId,
                                       (portType == PortType::Out) ? NodeRole::OutPortCount
                                                                   : NodeRole::InPortCount)
                             .toUInt();

        bool isMany = (portType == PortType::Out) ? visualInfo.output_is_many 
                                                   : visualInfo.input_is_many;

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &dataType = model
                                       .portData(nodeId, portType, portIndex, PortRole::DataType)
                                       .value<NodeDataType>();

            double r = 1.0;
            if (ngo.nodeState().connectionForReaction()) {
                auto const *cgo = ngo.nodeState().connectionForReaction();
                
                PortType requiredPort = oppositePort(portType);
                
                ConnectionId possibleConnectionId = makeCompleteConnectionId(cgo->connectionId(),
                                                                             nodeId,
                                                                             portIndex);

                bool const possible = model.connectionPossible(possibleConnectionId);

                auto cp = cgo->sceneTransform().map(cgo->endPoint(requiredPort));
                cp = ngo.sceneTransform().inverted().map(cp);

                auto diff = cp - p;
                double dist = std::sqrt(QPointF::dotProduct(diff, diff));

                if (possible) {
                    double const thres = 40.0;
                    r = (dist < thres) ? (2.0 - dist / thres) : 1.0;
                } else {
                    double const thres = 80.0;
                    r = (dist < thres) ? (dist / thres) : 1.0;
                }
            }

            // Set color for outline
            if (connectionStyle.useDataDefinedColors()) {
                painter->setPen(connectionStyle.normalColor(dataType.id));
            } else {
                painter->setPen(nodeStyle.ConnectionPointColor);
            }
            
            // "One" ports: filled with background color (white) - looks like just an outline
            // "Many" ports: also filled with background color initially, then draw inner dot
            painter->setBrush(Qt::white);
            painter->drawEllipse(p, reducedDiameter * r, reducedDiameter * r);
            
            // For "many" ports, draw a filled dot in the center (same color as outline)
            if (isMany) {
                if (connectionStyle.useDataDefinedColors()) {
                    painter->setBrush(connectionStyle.normalColor(dataType.id));
                } else {
                    painter->setBrush(nodeStyle.ConnectionPointColor);
                }
                painter->setPen(Qt::NoPen);
                double dotSize = reducedDiameter * r * 0.4;  // Inner dot is 40% of port size
                painter->drawEllipse(p, dotSize, dotSize);
            }
        }
    }

    if (ngo.nodeState().connectionForReaction()) {
        ngo.nodeState().resetConnectionForReaction();
    }
}

void OrcNodePainter::drawFilledConnectionPointsCustom(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    auto diameter = nodeStyle.ConnectionPointDiameter;

    // Get ORC node info to determine if ports are "many"
    auto* orcModel = dynamic_cast<OrcGraphModel*>(&model);
    NodeTypeHelper::NodeVisualInfo visualInfo{true, true, false, false};  // Default
    
    if (orcModel) {
        orc::NodeID orcNodeId = orcModel->getOrcNodeId(nodeId);
        if (orcNodeId.is_valid()) {
            std::string stage_name = orcModel->getNodeStageName(orcNodeId);
            if (!stage_name.empty()) {
                visualInfo = NodeTypeHelper::getVisualInfo(stage_name);
            }
        }
    }

    for (PortType portType : {PortType::Out, PortType::In}) {
        size_t const n = model
                             .nodeData(nodeId,
                                       (portType == PortType::Out) ? NodeRole::OutPortCount
                                                                   : NodeRole::InPortCount)
                             .toUInt();

        bool isMany = (portType == PortType::Out) ? visualInfo.output_is_many 
                                                   : visualInfo.input_is_many;

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &connected = model.connections(nodeId, portType, portIndex);

            if (!connected.empty()) {
                auto const &dataType = model
                                           .portData(nodeId, portType, portIndex, PortRole::DataType)
                                           .value<NodeDataType>();

                // No additional rendering needed when connected
                // "Many" ports already have inner dot
                // "One" ports remain as just outline when connected
            }
        }
    }
}
