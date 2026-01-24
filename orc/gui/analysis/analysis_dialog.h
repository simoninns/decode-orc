#ifndef ORC_GUI_ANALYSIS_DIALOG_H
#define ORC_GUI_ANALYSIS_DIALOG_H

// ============================================================================
// DEPRECATED - NOT PART OF BUILD
// ============================================================================
// This file is legacy code from the old analysis system and is not compiled
// into orc-gui. It has been superseded by AnalysisPresenter and specific
// analysis dialog implementations (e.g., dropoutanalysisdialog.cpp).
// The relative path includes below violate MVP architecture and are preserved
// only for historical reference.
// ============================================================================

#include "../../core/analysis/analysis_tool.h"
#include "../../core/analysis/analysis_result.h"
#include "../../core/analysis/analysis_context.h"
#include "analysis_runner.h"
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>

namespace orc {
namespace gui {

/**
 * @brief Generic analysis dialog that works with any AnalysisTool
 * 
 * This dialog:
 * - Auto-generates parameter UI from tool definitions
 * - Shows progress during analysis
 * - Displays results in a generic format
 * - Allows applying results to graph
 */
class AnalysisDialog : public QDialog {
    Q_OBJECT

public:
    AnalysisDialog(AnalysisTool* tool,
                   const AnalysisContext& context,
                   QWidget* parent = nullptr);

signals:
    void applyToGraph(const AnalysisResult& result);

private slots:
    void runAnalysis();
    void cancelAnalysis();
    void onApplyClicked();
    void addPartialResult(const AnalysisResult::ResultItem& item);
    void onAnalysisComplete(const AnalysisResult& result);
    void update_dependencies();

private:
    void setupUI();
    void populateParameters();
    QWidget* createParameterWidget(const ParameterDescriptor& param);
    void collectParameters();
    void displayFinalStatistics(const AnalysisResult& result);
    void updateLiveStatistics();

    AnalysisTool* tool_;
    AnalysisContext context_;
    AnalysisRunner* analysisRunner_ = nullptr;
    AnalysisResult currentResult_;

    // UI widgets
    QLabel* descriptionLabel_;
    QLabel* statusLabel_;
    QLabel* subStatusLabel_;
    QLabel* estimateLabel_;
    QLabel* summaryLabel_;
    QProgressBar* progressBar_;
    QTextEdit* statisticsText_;
    QPushButton* runButton_;
    QPushButton* cancelButton_;
    QPushButton* applyButton_;
    QPushButton* closeButton_;
    QFormLayout* parametersLayout_;

    // Parameter widgets (for collecting values)
    struct ParameterWidget {
        QString name;
        QWidget* widget;
        ParameterType type;
        QLabel* label = nullptr;
    };
    QVector<ParameterWidget> parameterWidgets_;
    std::vector<ParameterDescriptor> parameterDescriptors_;
};

} // namespace gui
} // namespace orc

#endif // ORC_GUI_ANALYSIS_DIALOG_H
