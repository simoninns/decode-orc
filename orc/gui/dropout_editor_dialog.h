/*
 * File:        dropout_editor_dialog.h
 * Module:      orc-gui
 * Purpose:     Dropout map editor dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DROPOUT_EDITOR_DIALOG_H
#define DROPOUT_EDITOR_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QImage>
#include <QPixmap>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QRubberBand>
#include <memory>
#include <map>
#include <vector>
#include "../core/include/field_id.h"
#include "../core/include/dropout_decision.h"
#include "../core/include/video_field_representation.h"
#include "../core/stages/dropout_map/dropout_map_stage.h"

/**
 * @brief Interactive widget for displaying and editing dropout regions on a field image
 * 
 * This widget displays a video field and allows the user to:
 * - View existing dropout regions (highlighted)
 * - Add new dropout regions by clicking and dragging
 * - Remove existing dropout regions by clicking on them
 */
class DropoutFieldView : public QLabel
{
    Q_OBJECT

public:
    explicit DropoutFieldView(QWidget *parent = nullptr);
    ~DropoutFieldView() = default;

    /**
     * @brief Set the field to display
     * @param field_data Field pixel data (grayscale)
     * @param width Field width in pixels
     * @param height Field height in pixels
     * @param source_dropouts Existing dropout regions from source (highlighted in yellow)
     * @param additions Dropout regions to add (highlighted in green)
     * @param removals Dropout regions to remove (highlighted in red)
     */
    void setField(const std::vector<uint8_t>& field_data, int width, int height,
                  const std::vector<orc::DropoutRegion>& source_dropouts,
                  const std::vector<orc::DropoutRegion>& additions,
                  const std::vector<orc::DropoutRegion>& removals);

    /**
     * @brief Get the current additions list
     */
    const std::vector<orc::DropoutRegion>& getAdditions() const { return additions_; }
    std::vector<orc::DropoutRegion>& getAdditionsMutable() { return additions_; }

    /**
     * @brief Get the current removals list
     */
    const std::vector<orc::DropoutRegion>& getRemovals() const { return removals_; }
    std::vector<orc::DropoutRegion>& getRemovalsMutable() { return removals_; }

    /**
     * @brief Clear all edits
     */
    void clearEdits();
    
    /**
     * @brief Update the display (redraw the field with current data)
     */
    void updateDisplay();
    
    /**
     * @brief Get field dimensions for external access
     */
    int getFieldWidth() const { return field_width_; }
    int getFieldHeight() const { return field_height_; }
    
    /**
     * @brief Get field data for external access
     */
    const std::vector<uint8_t>& getFieldData() const { return field_data_; }
    
    /**
     * @brief Get source dropouts for external access
     */
    const std::vector<orc::DropoutRegion>& getSourceDropouts() const { return source_dropouts_; }

protected:
    QSize sizeHint() const override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    /**
     * @brief Emitted when the user modifies dropout regions
     */
    void regionsModified();
    
    /**
     * @brief Emitted when zoom level changes via scroll wheel
     */
    void zoomChanged(float zoom_level);
    
    /**
     * @brief Emitted when a new addition is created (index in additions list)
     */
    void additionCreated(int index);
    
    /**
     * @brief Emitted when a new removal is marked (index in removals list)
     */
    void removalCreated(int index);
    
    /**
     * @brief Emitted when an addition is clicked for selection (index in additions list)
     */
    void additionClicked(int index);
    
    /**
     * @brief Emitted when a removal is clicked for selection (index in removals list)
     */
    void removalClicked(int index);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    bool isPointInRegion(int x, int y, const orc::DropoutRegion& region) const;
    void removeRegionAtPoint(int x, int y);

    // Field data
    std::vector<uint8_t> field_data_;
    int field_width_;
    int field_height_;

    // Dropout regions
    std::vector<orc::DropoutRegion> source_dropouts_;  // From source TBC
    std::vector<orc::DropoutRegion> additions_;
    std::vector<orc::DropoutRegion> removals_;

public:
    // Mouse interaction state
    enum class InteractionMode {
        None,
        AddingDropout,
        RemovingDropout
    };

    InteractionMode mode_;
    
    // Hover/highlight region type
    enum class HoverRegionType { None, Source, Addition, Removal };

public:
    void setZoomLevel(float zoom);
    float getZoomLevel() const { return zoom_level_; }
    
    /**
     * @brief Set highlighted region (e.g., from list selection)
     * @param type Type of region (Addition, Removal, None)
     * @param index Index in the respective list (-1 for none)
     */
    void setHighlightedRegion(HoverRegionType type, int index);
    
private:
    QPoint drag_start_;
    QPoint drag_current_;
    bool dragging_;
    QRubberBand* rubber_band_;
    
    // Hover highlighting
    int hover_region_index_;  // Index in combined list, -1 for none
    HoverRegionType hover_region_type_;
    
    // Zoom support
    float zoom_level_;  // 1.0 = 100%, 2.0 = 200%, etc.
};

/**
 * @brief Dialog for editing dropout map for a stage
 * 
 * This dialog allows the user to:
 * - Navigate through fields in the source
 * - Mark new dropout regions by clicking and dragging
 * - Remove false positive dropout regions
 * - Save changes back to the dropout map stage parameter
 */
class DropoutEditorDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Create a dropout editor dialog
     * @param source_repr Source video field representation to edit
     * @param existing_map Existing dropout map from the stage parameter
     * @param parent Parent widget
     */
    explicit DropoutEditorDialog(
        std::shared_ptr<const orc::VideoFieldRepresentation> source_repr,
        const std::map<uint64_t, orc::FieldDropoutMap>& existing_map,
        QWidget *parent = nullptr);
    
    ~DropoutEditorDialog() = default;

    /**
     * @brief Get the edited dropout map
     * @return Map of field IDs to dropout modifications
     */
    std::map<uint64_t, orc::FieldDropoutMap> getDropoutMap() const;

private slots:
    void onPreviousField();
    void onNextField();
    void onFieldNumberChanged(int value);
    void onClearCurrentField();
    void onRegionsModified();
    void onAddDropout();
    void onRemoveDropout();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onMoveDropoutUp();
    void onMoveDropoutDown();
    void onDeleteDropout();
    void onAdditionsListItemClicked(QListWidgetItem* item);
    void onRemovalsListItemClicked(QListWidgetItem* item);
    void onFieldViewZoomChanged(float zoom_level);
    void onAdditionCreated(int index);
    void onRemovalCreated(int index);
    void onAdditionClicked(int index);
    void onRemovalClicked(int index);
    void onAdditionsListSelectionChanged();
    void onRemovalsListSelectionChanged();

private:
    void setupUI();
    void loadField(uint64_t field_id);
    void saveCurrentField();
    void updateFieldInfo();
    void keyPressEvent(QKeyEvent *event) override;
    
    // Helper to set button states based on whether an addition or removal is selected
    void updateButtonStatesForSelection(bool is_addition);

    // Source data
    std::shared_ptr<const orc::VideoFieldRepresentation> source_repr_;
    
    // Current state
    uint64_t current_field_id_;
    uint64_t total_fields_;
    std::map<uint64_t, orc::FieldDropoutMap> dropout_map_;

    // UI elements
    QSpinBox* field_spin_box_;
    QLabel* field_info_label_;
    QPushButton* prev_button_;
    QPushButton* next_button_;
    QPushButton* clear_field_button_;
    QPushButton* add_dropout_button_;
    QPushButton* remove_dropout_button_;
    QListWidget* additions_list_;
    QListWidget* removals_list_;
    DropoutFieldView* field_view_;
    QScrollArea* scroll_area_;
    QPushButton* zoom_in_button_;
    QPushButton* zoom_out_button_;
    QPushButton* zoom_reset_button_;
    QLabel* zoom_label_;
    QPushButton* move_up_button_;
    QPushButton* move_down_button_;
    QPushButton* delete_dropout_button_;
    int selected_addition_index_;  // -1 if none selected
    int selected_removal_index_;   // -1 if none selected
    
    // Interaction mode
    enum class EditMode {
        Add,
        Remove
    };
    EditMode edit_mode_;
};

#endif // DROPOUT_EDITOR_DIALOG_H
