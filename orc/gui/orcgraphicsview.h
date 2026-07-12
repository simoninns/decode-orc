/*
 * File:        orcgraphicsview.h
 * Module:      orc-gui
 * Purpose:     Custom QtNodes view with validated deletion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <QString>
#include <QtNodes/GraphicsView>

class OrcGraphModel;

/**
 * Custom graphics view that validates node deletion before allowing it
 */
class OrcGraphicsView : public QtNodes::GraphicsView {
  Q_OBJECT

 public:
  explicit OrcGraphicsView(QWidget* parent = nullptr);
  ~OrcGraphicsView() override = default;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 public:
  void setShowWelcomeMessage(bool show);

 private slots:
  void onDeleteSelectedObjects() override;

 private:
  // Persist any node whose on-screen position no longer matches the position
  // stored in the model. QtNodes moves nodes visually during a drag but never
  // writes the new coordinates back to the graph model, so without this the
  // project keeps stale positions and every node snaps back to its old spot
  // the next time the scene is rebuilt (e.g. after editing parameters).
  void commitDraggedNodePositions();

  bool show_welcome_message_{true};
  QString welcome_message_;
};
