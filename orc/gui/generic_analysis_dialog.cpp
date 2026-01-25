#include "generic_analysis_dialog.h"
#include "presenters/include/analysis_presenter.h"
#include "presenters/include/field_corruption_presenter.h"
#include "presenters/include/disc_mapper_presenter.h"
#include "presenters/include/source_alignment_presenter.h"
#include "presenters/include/mask_line_presenter.h"
#include "presenters/include/ffmpeg_preset_presenter.h"
#include "presenters/include/dropout_editor_presenter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <limits>

namespace orc {
namespace gui {

GenericAnalysisDialog::GenericAnalysisDialog(
    const std::string& tool_id,
    const orc::public_api::AnalysisToolInfo& tool_info,
    orc::presenters::AnalysisPresenter* presenter,
    const orc::NodeID& node_id,
    orc::Project* project,
    QWidget* parent)
    : QDialog(parent), tool_id_(tool_id), tool_info_(tool_info), 
            presenter_(presenter), field_corruption_presenter_(nullptr),
            disc_mapper_presenter_(nullptr), source_alignment_presenter_(nullptr),
            mask_line_presenter_(nullptr), ffmpeg_preset_presenter_(nullptr),
            dropout_editor_presenter_(nullptr),
      project_(project), node_id_(node_id) {
    
        // Create specialized presenters when needed
        if (tool_id_ == "field_corruption") {
        field_corruption_presenter_ = new orc::presenters::FieldCorruptionPresenter(project_);
        } else if (tool_id_ == "field_mapping" || tool_id_ == "disc_mapper") {
                disc_mapper_presenter_ = new orc::presenters::DiscMapperPresenter(project_);
    } else if (tool_id_ == "source_alignment") {
        source_alignment_presenter_ = new orc::presenters::SourceAlignmentPresenter(project_);
    } else if (tool_id_ == "mask_line_config") {
        mask_line_presenter_ = new orc::presenters::MaskLinePresenter(project_);
    } else if (tool_id_ == "ffmpeg_preset_config") {
        ffmpeg_preset_presenter_ = new orc::presenters::FFmpegPresetPresenter(project_);
    } else if (tool_id_ == "dropout_editor") {
        dropout_editor_presenter_ = new orc::presenters::DropoutEditorPresenter(project_);
    }
    
    setupUI();
    populateParameters();
    setWindowTitle(QString::fromStdString(tool_info_.name));
    resize(900, 700);
}

GenericAnalysisDialog::~GenericAnalysisDialog() {
    delete presenter_;
    delete field_corruption_presenter_;
    delete disc_mapper_presenter_;
    delete source_alignment_presenter_;
    delete mask_line_presenter_;
    delete ffmpeg_preset_presenter_;
    delete dropout_editor_presenter_;
}

void GenericAnalysisDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Description
    descriptionLabel_ = new QLabel(QString::fromStdString(tool_info_.description));
    descriptionLabel_->setWordWrap(true);
    layout->addWidget(descriptionLabel_);
    
    // Parameters group
    auto* paramsGroup = new QGroupBox("Parameters");
    parametersLayout_ = new QFormLayout();
    paramsGroup->setLayout(parametersLayout_);
    layout->addWidget(paramsGroup);
    
    // Progress group
    auto* progressGroup = new QGroupBox("Progress");
    auto* progLayout = new QVBoxLayout();
    statusLabel_ = new QLabel("Ready");
    progressBar_ = new QProgressBar();
    progressBar_->setMaximum(100);
    progressBar_->setValue(0);
    progLayout->addWidget(statusLabel_);
    progLayout->addWidget(progressBar_);
    progressGroup->setLayout(progLayout);
    layout->addWidget(progressGroup);
    
    // Results/report text area
    auto* reportGroup = new QGroupBox("Report");
    auto* reportLayout = new QVBoxLayout();
    reportText_ = new QTextEdit();
    reportText_->setReadOnly(true);
    reportText_->setMinimumHeight(300);
    reportText_->setLineWrapMode(QTextEdit::WidgetWidth);
    reportLayout->addWidget(reportText_);
    reportGroup->setLayout(reportLayout);
    layout->addWidget(reportGroup);
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    runButton_ = new QPushButton("Run Analysis");
    applyButton_ = new QPushButton("Apply to Stage");
    closeButton_ = new QPushButton("OK");
    
    applyButton_->setEnabled(false);
    
    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(applyButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton_);
    
    layout->addLayout(buttonLayout);
    
    // Connections
    connect(runButton_, &QPushButton::clicked, this, &GenericAnalysisDialog::runAnalysis);
    connect(applyButton_, &QPushButton::clicked, this, &GenericAnalysisDialog::applyResults);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void GenericAnalysisDialog::populateParameters() {
    // Get parameters from presenter
    orc::public_api::AnalysisSourceType source_type = orc::public_api::AnalysisSourceType::LaserDisc;
    parameter_descriptors_ = presenter_->getToolParameters(tool_id_, source_type);
    
    for (const auto& param : parameter_descriptors_) {
        QWidget* widget = createParameterWidget(param.name, param);
        
        // Create label with tooltip
        auto* label = new QLabel(QString::fromStdString(param.display_name) + ":");
        label->setToolTip(QString::fromStdString(param.description));
        if (widget) {
            widget->setToolTip(QString::fromStdString(param.description));
        }
        
        parametersLayout_->addRow(label, widget ? widget : new QLabel("N/A"));
        
        if (widget) {
            ParameterWidget pw;
            pw.name = param.name;
            pw.widget = widget;
            pw.type = param.type;
            pw.label = label;
            parameterWidgets_.push_back(pw);
            
            // Connect change signals for dependency updates
            switch (param.type) {
                case orc::ParameterType::STRING:
                    if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                                this, &GenericAnalysisDialog::updateParameterDependencies);
                    }
                    break;
                case orc::ParameterType::INT32:
                    connect(static_cast<QSpinBox*>(widget), QOverload<int>::of(&QSpinBox::valueChanged),
                            this, &GenericAnalysisDialog::updateParameterDependencies);
                    break;
                case orc::ParameterType::DOUBLE:
                    connect(static_cast<QDoubleSpinBox*>(widget), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                            this, &GenericAnalysisDialog::updateParameterDependencies);
                    break;
                case orc::ParameterType::BOOL:
                    {
                        auto* cb = static_cast<QCheckBox*>(widget);
                        connect(cb, &QCheckBox::checkStateChanged,
                                this, [this](Qt::CheckState){ updateParameterDependencies(); });
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

QWidget* GenericAnalysisDialog::createParameterWidget(const std::string& name, const orc::ParameterDescriptor& param) {
    switch (param.type) {
        case orc::ParameterType::BOOL: {
            auto* cb = new QCheckBox();
            if (param.constraints.default_value && std::holds_alternative<bool>(*param.constraints.default_value)) {
                cb->setChecked(std::get<bool>(*param.constraints.default_value));
            }
            return cb;
        }
        case orc::ParameterType::INT32: {
            auto* spin = new QSpinBox();
            if (param.constraints.min_value && std::holds_alternative<int32_t>(*param.constraints.min_value)) {
                spin->setMinimum(std::get<int32_t>(*param.constraints.min_value));
            } else {
                spin->setMinimum(std::numeric_limits<int32_t>::min());
            }
            if (param.constraints.max_value && std::holds_alternative<int32_t>(*param.constraints.max_value)) {
                spin->setMaximum(std::get<int32_t>(*param.constraints.max_value));
            } else {
                spin->setMaximum(std::numeric_limits<int32_t>::max());
            }
            if (param.constraints.default_value && std::holds_alternative<int32_t>(*param.constraints.default_value)) {
                spin->setValue(std::get<int32_t>(*param.constraints.default_value));
            }
            return spin;
        }
        case orc::ParameterType::DOUBLE: {
            auto* spin = new QDoubleSpinBox();
            if (param.constraints.default_value && std::holds_alternative<double>(*param.constraints.default_value)) {
                spin->setValue(std::get<double>(*param.constraints.default_value));
            }
            return spin;
        }
        case orc::ParameterType::STRING: {
            if (!param.constraints.allowed_strings.empty()) {
                auto* combo = new QComboBox();
                for (const auto& allowed : param.constraints.allowed_strings) {
                    combo->addItem(QString::fromStdString(allowed));
                }
                if (param.constraints.default_value && std::holds_alternative<std::string>(*param.constraints.default_value)) {
                    combo->setCurrentText(QString::fromStdString(std::get<std::string>(*param.constraints.default_value)));
                }
                return combo;
            } else {
                auto* edit = new QLineEdit();
                if (param.constraints.default_value && std::holds_alternative<std::string>(*param.constraints.default_value)) {
                    edit->setText(QString::fromStdString(std::get<std::string>(*param.constraints.default_value)));
                }
                return edit;
            }
        }
        default:
            return nullptr;
    }
}

void GenericAnalysisDialog::collectParameters() {
    // Note: Parameters are collected directly in runAnalysis() and passed to the presenter
    // This method is kept for potential future use
}

void GenericAnalysisDialog::runAnalysis() {
    collectParameters();
    
    runButton_->setEnabled(false);
    applyButton_->setEnabled(false);
    reportText_->clear();
    statusLabel_->setText("Running analysis...");
    progressBar_->setValue(0);
    
    // Prepare parameters
    std::map<std::string, orc::ParameterValue> parameters;
    for (const auto& pw : parameterWidgets_) {
        switch (pw.type) {
            case orc::ParameterType::BOOL: {
                auto* cb = qobject_cast<QCheckBox*>(pw.widget);
                if (cb) parameters[pw.name] = cb->isChecked();
                break;
            }
            case orc::ParameterType::INT32: {
                auto* spin = qobject_cast<QSpinBox*>(pw.widget);
                if (spin) parameters[pw.name] = spin->value();
                break;
            }
            case orc::ParameterType::DOUBLE: {
                auto* spin = qobject_cast<QDoubleSpinBox*>(pw.widget);
                if (spin) parameters[pw.name] = spin->value();
                break;
            }
            case orc::ParameterType::STRING: {
                auto* combo = qobject_cast<QComboBox*>(pw.widget);
                if (combo) {
                    parameters[pw.name] = combo->currentText().toStdString();
                } else {
                    auto* edit = qobject_cast<QLineEdit*>(pw.widget);
                    if (edit) parameters[pw.name] = edit->text().toStdString();
                }
                break;
            }
            default:
                break;
        }
    }
    
    // Progress callback to update UI for specialized presenter
    auto progress_callback = [this](int percentage, const std::string& status) {
        statusLabel_->setText(QString::fromStdString(status));
        progressBar_->setValue(percentage);
    };
    
    // Run analysis using specialized presenter if available
    orc::public_api::AnalysisResult result;
    
    if (disc_mapper_presenter_) {
        result = disc_mapper_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else if (field_corruption_presenter_) {
        result = field_corruption_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else if (source_alignment_presenter_) {
        result = source_alignment_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else if (mask_line_presenter_) {
        result = mask_line_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else if (ffmpeg_preset_presenter_) {
        result = ffmpeg_preset_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else if (dropout_editor_presenter_) {
        result = dropout_editor_presenter_->runAnalysis(node_id_, parameters, progress_callback);
    } else {
        // Use generic analysis presenter for other tools
        std::map<std::string, orc::ParameterValue> additional_context;
        orc::public_api::AnalysisSourceType source_type = orc::public_api::AnalysisSourceType::LaserDisc;

        // Adapt progress callback signature expected by AnalysisPresenter
        std::function<void(int, int, const std::string&, const std::string&)> generic_progress;
        generic_progress = [this](int current, int total, const std::string& status, const std::string& sub_status) {
            int pct = (total > 0) ? (current * 100 / total) : 0;
            std::string combined_status = status;
            if (!sub_status.empty()) {
                combined_status += " - " + sub_status;
            }
            statusLabel_->setText(QString::fromStdString(combined_status));
            progressBar_->setValue(pct);
        };

        result = presenter_->runGenericAnalysis(
            tool_id_,
            node_id_,
            source_type,
            parameters,
            additional_context,
            generic_progress
        );
    }
    
    last_result_ = result;
    displayResults(result);
    
    runButton_->setEnabled(true);
    if (result.status == orc::public_api::AnalysisResult::Status::Success) {
        applyButton_->setEnabled(true);
        statusLabel_->setText("Analysis complete");
        progressBar_->setValue(100);
    } else {
        statusLabel_->setText("Analysis failed");
        progressBar_->setValue(0);
    }
}

void GenericAnalysisDialog::displayResults(const orc::public_api::AnalysisResult& result) {
    QString text;
    
    if (result.status == orc::public_api::AnalysisResult::Status::Success) {
        text = "Analysis completed successfully.\n\n";
    } else if (result.status == orc::public_api::AnalysisResult::Status::Failed) {
        text = "Analysis failed.\n\n";
    } else {
        text = "Analysis cancelled.\n\n";
    }
    
    text += QString::fromStdString(result.summary);
    
    reportText_->setPlainText(text);
}

void GenericAnalysisDialog::applyResults() {
    if (last_result_.status != orc::public_api::AnalysisResult::Status::Success) {
        QMessageBox::warning(this, "Cannot Apply",
            "Analysis results are not valid. Please run the analysis again.");
        return;
    }
    
    // Apply results using the specialized presenter if available
    // This ensures the core tool's applyToGraph logic is executed
    bool applied = false;
    
    if (field_corruption_presenter_) {
        applied = field_corruption_presenter_->applyResultToGraph(last_result_, node_id_);
    } else if (disc_mapper_presenter_) {
        applied = disc_mapper_presenter_->applyResultToGraph(last_result_, node_id_);
    } else if (source_alignment_presenter_) {
        applied = source_alignment_presenter_->applyResultToGraph(last_result_, node_id_);
    } else if (presenter_) {
        // Fallback to generic presenter (though this shouldn't happen for modern tools)
        // The mainwindow will handle this case
        emit applyResultsRequested(last_result_);
        accept();
        return;
    }
    
    if (!applied) {
        QMessageBox::warning(this, "Apply Failed",
            "Failed to apply analysis results to the stage. Check the log for details.");
        return;
    }
    
    // Emit signal so mainwindow can update UI (rebuild DAG, refresh preview, etc.)
    emit applyResultsRequested(last_result_);
    
    // Close dialog
    accept();
}

void GenericAnalysisDialog::updateParameterDependencies() {
    // TODO: Implement parameter dependency logic
}

} // namespace gui
} // namespace orc
