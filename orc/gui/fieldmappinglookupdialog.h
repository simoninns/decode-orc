/*
 * File:        fieldmappinglookup dialog.h
 * Module:      orc-gui
 * Purpose:     Field mapping lookup dialog for frame/timecode translation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef FIELDMAPPINGLOOKUPDIALOG_H
#define FIELDMAPPINGLOOKUPDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>
#include <memory>

namespace orc {
    class FieldMappingLookup;
}

/**
 * @brief Dialog for translating between frame numbers, timecodes, and field IDs
 * 
 * This dialog provides a utility for users to:
 * - Convert frame numbers to field ID ranges
 * - Convert timecodes to field ID ranges
 * - Convert field IDs to frame numbers/timecodes
 * - Query ranges (e.g., frames 1000-2000 or timecodes)
 * 
 * Example use cases:
 * - "I want to extract frames 1000-2000, what field IDs do I need?"
 * - "What's the timecode for field ID 5000?"
 * - "What field IDs cover timecode 0:10:10.28 to 0:20:10.03?"
 */
class FieldMappingLookupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FieldMappingLookupDialog(QWidget *parent = nullptr);
    ~FieldMappingLookupDialog();

    /**
     * @brief Set the lookup utility to use
     * @param lookup Shared pointer to FieldMappingLookup instance
     */
    void setLookup(std::shared_ptr<orc::FieldMappingLookup> lookup);
    
    /**
     * @brief Clear the lookup and disable queries
     */
    void clearLookup();

private slots:
    void onQueryTypeChanged(int index);
    void onLookupButtonClicked();
    void onClearButtonClicked();

private:
    void setupUI();
    void updateUIState();
    void performFrameLookup();
    void performTimecodeLookup();
    void performFieldIdLookup();
    void displayResult(const QString& result);
    void displayError(const QString& error);
    
    // UI widgets
    QComboBox *queryTypeCombo_;
    QGroupBox *frameQueryGroup_;
    QGroupBox *timecodeQueryGroup_;
    QGroupBox *fieldIdQueryGroup_;
    
    // Frame query widgets
    QLineEdit *frameStartEdit_;
    QLineEdit *frameEndEdit_;
    QLabel *frameRangeLabel_;
    
    // Timecode query widgets
    QLineEdit *timecodeStartEdit_;
    QLineEdit *timecodeEndEdit_;
    QLabel *timecodeFormatLabel_;
    
    // Field ID query widgets
    QLineEdit *fieldIdStartEdit_;
    QLineEdit *fieldIdEndEdit_;
    
    // Common widgets
    QPushButton *lookupButton_;
    QPushButton *clearButton_;
    QTextEdit *resultsText_;
    QLabel *statusLabel_;
    
    // Data
    std::shared_ptr<orc::FieldMappingLookup> lookup_;
};

#endif // FIELDMAPPINGLOOKUPDIALOG_H
