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
#include <QGroupBox>
#include <string>
#include <memory>
#include <map>
#include <orc_analysis.h>
#include <node_id.h>

namespace orc::presenters {
    class AnalysisPresenter;
    class FieldCorruptionPresenter;
    class DiscMapperPresenter;
    class FieldMapRangePresenter;
    class SourceAlignmentPresenter;
    class MaskLinePresenter;
    class FFmpegPresetPresenter;
    class DropoutEditorPresenter;
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
        const orc::AnalysisToolInfo& tool_info,
        orc::presenters::AnalysisPresenter* presenter,
        const orc::NodeID& node_id,
        void* project,
        QWidget* parent = nullptr);
    
    ~GenericAnalysisDialog();

signals:
    void analysisApplied();
    void applyResultsRequested(const orc::AnalysisResult& result);

private slots:
    void runAnalysis();
    void applyResults();
    void updateParameterDependencies();

private:
    void setupUI();
    void populateParameters();
    QWidget* createParameterWidget(const std::string& name, const orc::ParameterDescriptor& param);
    void collectParameters();
    void displayResults(const orc::AnalysisResult& result);
    void setupFieldMapRangeWidgets();
    void syncTimecodeFromPicture(bool is_start);
    void syncPictureFromTimecode(bool is_start);
    int timecodeFps() const;

    std::string tool_id_;
    orc::AnalysisToolInfo tool_info_;
    orc::presenters::AnalysisPresenter* presenter_;  // Not owned
    orc::presenters::FieldCorruptionPresenter* field_corruption_presenter_;  // Owned (if used)
    orc::presenters::DiscMapperPresenter* disc_mapper_presenter_;  // Owned (if used)
    orc::presenters::FieldMapRangePresenter* field_map_range_presenter_;  // Owned (if used)
    orc::presenters::SourceAlignmentPresenter* source_alignment_presenter_;  // Owned (if used)
    orc::presenters::MaskLinePresenter* mask_line_presenter_;  // Owned (if used)
    orc::presenters::FFmpegPresetPresenter* ffmpeg_preset_presenter_;  // Owned (if used)
    orc::presenters::DropoutEditorPresenter* dropout_editor_presenter_;  // Owned (if used)
    void* project_;  // Not owned (opaque handle)
    orc::NodeID node_id_;
    orc::AnalysisResult last_result_;
    std::vector<orc::ParameterDescriptor> parameter_descriptors_;

    // Field map range custom controls
    bool field_map_range_sync_in_progress_ = false;
    int field_map_range_fps_ = 30;
    QSpinBox* picture_start_spin_ = nullptr;
    QSpinBox* picture_end_spin_ = nullptr;
    QSpinBox* tc_start_hours_ = nullptr;
    QSpinBox* tc_start_minutes_ = nullptr;
    QSpinBox* tc_start_seconds_ = nullptr;
    QSpinBox* tc_start_pictures_ = nullptr;
    QSpinBox* tc_end_hours_ = nullptr;
    QSpinBox* tc_end_minutes_ = nullptr;
    QSpinBox* tc_end_seconds_ = nullptr;
    QSpinBox* tc_end_pictures_ = nullptr;

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
