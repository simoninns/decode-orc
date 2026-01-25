#ifndef ORC_GUI_GENERIC_ANALYSIS_DIALOG_H
#define ORC_GUI_GENERIC_ANALYSIS_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <string>
#include <memory>
#include <map>
#include <orc_analysis.h>
#include <node_id.h>

namespace orc {
    class Project;
}

namespace orc::presenters {
    class AnalysisPresenter;
    class FieldCorruptionPresenter;
}

namespace orc {
namespace gui {

/**
 * @brief Generic analysis dialog for tools using the public API
 * 
 * This dialog:
 * - Auto-generates parameter UI from tool parameter descriptors
 * - Shows progress during analysis
 * - Displays results in the report widget
 * - Allows applying results to the graph
 */
class GenericAnalysisDialog : public QDialog {
    Q_OBJECT

public:
    GenericAnalysisDialog(
        const std::string& tool_id,
        const orc::public_api::AnalysisToolInfo& tool_info,
        orc::presenters::AnalysisPresenter* presenter,
        const orc::NodeID& node_id,
        orc::Project* project,
        QWidget* parent = nullptr);
    
    ~GenericAnalysisDialog();

signals:
    void analysisApplied();
    void applyResultsRequested(const orc::public_api::AnalysisResult& result);

private slots:
    void runAnalysis();
    void applyResults();
    void updateParameterDependencies();

private:
    void setupUI();
    void populateParameters();
    QWidget* createParameterWidget(const std::string& name, const orc::ParameterDescriptor& param);
    void collectParameters();
    void displayResults(const orc::public_api::AnalysisResult& result);

    std::string tool_id_;
    orc::public_api::AnalysisToolInfo tool_info_;
    orc::presenters::AnalysisPresenter* presenter_;  // Not owned
    orc::presenters::FieldCorruptionPresenter* field_corruption_presenter_;  // Owned (if used)
    orc::Project* project_;  // Not owned
    orc::NodeID node_id_;
    orc::public_api::AnalysisResult last_result_;
    std::vector<orc::ParameterDescriptor> parameter_descriptors_;

    // UI widgets
    QLabel* descriptionLabel_;
    QLabel* statusLabel_;
    QProgressBar* progressBar_;
    QTextEdit* reportText_;
    QPushButton* runButton_;
    QPushButton* applyButton_;
    QPushButton* closeButton_;
    QFormLayout* parametersLayout_;

    // Parameter widgets
    struct ParameterWidget {
        std::string name;
        QWidget* widget;
        orc::ParameterType type;
        QLabel* label = nullptr;
    };
    std::vector<ParameterWidget> parameterWidgets_;
};

} // namespace gui
} // namespace orc

#endif // ORC_GUI_GENERIC_ANALYSIS_DIALOG_H
