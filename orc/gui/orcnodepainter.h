/*
 * File:        orcnodepainter.h
 * Module:      orc-gui
 * Purpose:     Custom node painter with proper "one" vs "many" port visualization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <QtNodes/internal/DefaultNodePainter.hpp>
#include <QPainter>

namespace QtNodes {
class NodeGraphicsObject;
}

/**
 * Custom node painter that renders:
 * - Hollow circles for "one" connection ports
 * - Filled circles for "many" connection ports
 */
class OrcNodePainter : public QtNodes::DefaultNodePainter
{
public:
    OrcNodePainter() = default;
    ~OrcNodePainter() override = default;

    // Override paint to use custom connection point drawing
    void paint(QPainter *painter, QtNodes::NodeGraphicsObject &ngo) const override;
    
    // Custom methods (not override - DefaultNodePainter methods are not virtual)
    void drawNodeCaption(QPainter *painter, QtNodes::NodeGraphicsObject &ngo) const;
    void drawNodeId(QPainter *painter, QtNodes::NodeGraphicsObject &ngo) const;
    void drawConnectionPointsCustom(QPainter *painter, QtNodes::NodeGraphicsObject &ngo) const;
    void drawFilledConnectionPointsCustom(QPainter *painter, QtNodes::NodeGraphicsObject &ngo) const;
};
