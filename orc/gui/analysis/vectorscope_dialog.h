/*
 * File:        vectorscope_dialog.h
 * Module:      orc-gui
 * Purpose:     Vectorscope visualization dialog
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_GUI_ANALYSIS_VECTORSCOPE_DIALOG_H
#define ORC_GUI_ANALYSIS_VECTORSCOPE_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QImage>
#include <string>
#include <optional>
#include "../../core/include/node_id.h"
#include "../../core/analysis/vectorscope/vectorscope_data.h"

/**
 * @brief QLabel subclass that maintains aspect ratio of the displayed pixmap
 */
class AspectRatioLabel : public QLabel {
    Q_OBJECT

public:
    explicit AspectRatioLabel(QWidget* parent = nullptr);
    
    void setPixmap(const QPixmap& pixmap);
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    
private:
    void updateScaledPixmap();
    
    QPixmap original_pixmap_;
};

namespace orc {
    struct VectorscopeData;
    class ChromaSinkStage;
}

/**
 * @brief Live vectorscope visualization for chroma decoder output
 * 
 * This dialog displays U/V color components on a vectorscope for decoded
 * chroma output from a ChromaSinkStage. It's a live visualization tool that
 * updates in real-time as the user navigates through fields.
 */
class VectorscopeDialog : public QDialog {
    Q_OBJECT

public:
    explicit VectorscopeDialog(QWidget *parent = nullptr);
    ~VectorscopeDialog() override;
    
    void setStage(orc::NodeID node_id);
    
    /**
     * @brief Update vectorscope for a specific field
     * @param field_number Field number to display
     */
    void updateForField(uint64_t field_number, const orc::VectorscopeData* data);
    
    /**
     * @brief Render vectorscope from extracted U/V data
     * @param data Vectorscope data containing U/V samples
     */
    void renderVectorscope(const orc::VectorscopeData& data);
    
    /**
     * @brief Clear the vectorscope display
     */
    void clearDisplay();

Q_SIGNALS:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onBlendColorToggled();
    void onDefocusToggled();
    void onFieldSelectionChanged();
    void onGraticuleChanged();

private:
    void setupUI();
    void connectSignals();
    void drawGraticule(QPainter& painter, const orc::VectorscopeData& data);
    
    // Associated stage
    orc::NodeID node_id_;
    orc::ChromaSinkStage* stage_;  // Not owned
    
    // UI components
    AspectRatioLabel* scope_label_;
    QLabel* info_label_;
    
    // Display options
    QCheckBox* blend_color_checkbox_;
    QCheckBox* defocus_checkbox_;
    
    // Field selection options
    QRadioButton* field_select_all_radio_;
    QRadioButton* field_select_first_radio_;
    QRadioButton* field_select_second_radio_;
    QButtonGroup* field_select_group_;
    
    // Graticule options
    QRadioButton* graticule_none_radio_;
    QRadioButton* graticule_full_radio_;
    QRadioButton* graticule_75_radio_;
    QButtonGroup* graticule_group_;
    
    // Current field info
    uint64_t current_field_number_;
    std::optional<orc::VectorscopeData> last_data_;
};

#endif // ORC_GUI_ANALYSIS_VECTORSCOPE_DIALOG_H
