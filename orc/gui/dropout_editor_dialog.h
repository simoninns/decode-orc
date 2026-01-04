/*
 * File:        dropout_editor_dialog.h
 * Module:      orc-gui
 * Purpose:     Dropout map editor dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef DROPOUT_EDITOR_DIALOG_H
#define DROPOUT_EDITOR_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QImage>
#include <QPixmap>
#include <QMouseEvent>
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

    /**
     * @brief Get the current removals list
     */
    const std::vector<orc::DropoutRegion>& getRemovals() const { return removals_; }

    /**
     * @brief Clear all edits
     */
    void clearEdits();

protected:
    QSize sizeHint() const override;
    void resizeEvent(QResizeEvent *event) override;

signals:
    /**
     * @brief Emitted when the user modifies dropout regions
     */
    void regionsModified();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateDisplay();
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

private:
    QPoint drag_start_;
    QPoint drag_current_;
    bool dragging_;
    QRubberBand* rubber_band_;
    
    // Hover highlighting
    int hover_region_index_;  // Index in combined list, -1 for none
    enum class HoverRegionType { None, Source, Addition, Removal };
    HoverRegionType hover_region_type_;
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

private:
    void setupUI();
    void loadField(uint64_t field_id);
    void saveCurrentField();
    void updateFieldInfo();

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
    
    // Interaction mode
    enum class EditMode {
        Add,
        Remove
    };
    EditMode edit_mode_;
};

#endif // DROPOUT_EDITOR_DIALOG_H
