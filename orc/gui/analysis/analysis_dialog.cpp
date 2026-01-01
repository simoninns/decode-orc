#include "analysis_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QComboBox>
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <limits>

namespace orc {
namespace gui {

AnalysisDialog::AnalysisDialog(AnalysisTool* tool,
                               const AnalysisContext& context,
                               QWidget* parent)
    : QDialog(parent), tool_(tool), context_(context) {
    setupUI();
    populateParameters();
    
    setWindowTitle(QString::fromStdString(tool->name()));
    resize(800, 600);
}

void AnalysisDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Description
    descriptionLabel_ = new QLabel(QString::fromStdString(tool_->description()));
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
    subStatusLabel_ = new QLabel("");
    progressBar_ = new QProgressBar();
    progLayout->addWidget(statusLabel_);
    progLayout->addWidget(subStatusLabel_);
    progLayout->addWidget(progressBar_);
    progressGroup->setLayout(progLayout);
    layout->addWidget(progressGroup);
    
    // Results text area (combines results, summary, and statistics)
    statisticsText_ = new QTextEdit();
    statisticsText_->setReadOnly(true);
    statisticsText_->setMinimumHeight(300);
    statisticsText_->setLineWrapMode(QTextEdit::WidgetWidth);
    layout->addWidget(statisticsText_);
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    runButton_ = new QPushButton("Run Analysis");
    cancelButton_ = new QPushButton("Cancel");
    applyButton_ = new QPushButton("Apply to Node");
    closeButton_ = new QPushButton("Close");
    
    cancelButton_->setEnabled(false);
    applyButton_->setEnabled(false);
    
    buttonLayout->addWidget(runButton_);
    buttonLayout->addWidget(cancelButton_);
    buttonLayout->addWidget(applyButton_);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton_);
    
    layout->addLayout(buttonLayout);
    
    // Connections
    connect(runButton_, &QPushButton::clicked, this, &AnalysisDialog::runAnalysis);
    connect(cancelButton_, &QPushButton::clicked, this, &AnalysisDialog::cancelAnalysis);
    connect(applyButton_, &QPushButton::clicked, this, &AnalysisDialog::onApplyClicked);
    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void AnalysisDialog::populateParameters() {
    parameterDescriptors_ = tool_->parametersForContext(context_);
    
    for (const auto& param : parameterDescriptors_) {
        QWidget* widget = createParameterWidget(param);
        
        // Create label with tooltip
        auto* label = new QLabel(QString::fromStdString(param.display_name) + ":");
        label->setToolTip(QString::fromStdString(param.description));
        widget->setToolTip(QString::fromStdString(param.description));
        
        parametersLayout_->addRow(label, widget);
        
        ParameterWidget pw;
        pw.name = QString::fromStdString(param.name);
        pw.widget = widget;
        pw.type = param.type;
        pw.label = label;
        parameterWidgets_.push_back(pw);
        
        // Connect change signals to update dependencies
        switch (param.type) {
            case ParameterType::STRING:
                if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, &AnalysisDialog::update_dependencies);
                } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
                    connect(edit, &QLineEdit::textChanged,
                            this, &AnalysisDialog::update_dependencies);
                }
                break;
            case ParameterType::INT32:
                connect(static_cast<QSpinBox*>(widget), QOverload<int>::of(&QSpinBox::valueChanged),
                        this, &AnalysisDialog::update_dependencies);
                break;
            case ParameterType::DOUBLE:
                connect(static_cast<QDoubleSpinBox*>(widget), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                        this, &AnalysisDialog::update_dependencies);
                break;
            case ParameterType::BOOL:
                connect(static_cast<QCheckBox*>(widget), &QCheckBox::checkStateChanged,
                        this, &AnalysisDialog::update_dependencies);
                break;
            default:
                break;
        }
    }
    
    // Initial dependency update
    update_dependencies();
}

QWidget* AnalysisDialog::createParameterWidget(const ParameterDescriptor& param) {
    // Simple stub - just create basic widgets
    switch (param.type) {
        case ParameterType::BOOL: {
            auto* cb = new QCheckBox();
            if (param.constraints.default_value && std::holds_alternative<bool>(*param.constraints.default_value)) {
                cb->setChecked(std::get<bool>(*param.constraints.default_value));
            }
            return cb;
        }
        case ParameterType::INT32: {
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
        case ParameterType::DOUBLE: {
            auto* spin = new QDoubleSpinBox();
            if (param.constraints.default_value && std::holds_alternative<double>(*param.constraints.default_value)) {
                spin->setValue(std::get<double>(*param.constraints.default_value));
            }
            return spin;
        }
        case ParameterType::STRING: {
            // Check if we have allowed_strings - if so, use combo box
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
        default: {
            auto* edit = new QLineEdit();
            if (param.constraints.default_value && std::holds_alternative<std::string>(*param.constraints.default_value)) {
                edit->setText(QString::fromStdString(std::get<std::string>(*param.constraints.default_value)));
            }
            return edit;
        }
    }
}

void AnalysisDialog::collectParameters() {
    // Collect parameters from UI and update context
    for (const auto& pw : parameterWidgets_) {
        std::string name = pw.name.toStdString();
        
        switch (pw.type) {
            case ParameterType::BOOL: {
                auto* cb = qobject_cast<QCheckBox*>(pw.widget);
                if (cb) context_.parameters[name] = cb->isChecked();
                break;
            }
            case ParameterType::INT32: {
                auto* spin = qobject_cast<QSpinBox*>(pw.widget);
                if (spin) context_.parameters[name] = spin->value();
                break;
            }
            case ParameterType::DOUBLE: {
                auto* spin = qobject_cast<QDoubleSpinBox*>(pw.widget);
                if (spin) context_.parameters[name] = spin->value();
                break;
            }
            case ParameterType::STRING: {
                // Try combo box first (for allowed_strings)
                auto* combo = qobject_cast<QComboBox*>(pw.widget);
                if (combo) {
                    context_.parameters[name] = combo->currentText().toStdString();
                } else {
                    // Fall back to line edit
                    auto* edit = qobject_cast<QLineEdit*>(pw.widget);
                    if (edit) context_.parameters[name] = edit->text().toStdString();
                }
                break;
            }
            default:
                break;
        }
    }
}

void AnalysisDialog::runAnalysis() {
    collectParameters();
    
    runButton_->setEnabled(false);
    cancelButton_->setEnabled(true);
    applyButton_->setEnabled(false);
    statisticsText_->clear();
    
    analysisRunner_ = new AnalysisRunner(tool_, context_, this);
    
    connect(analysisRunner_, &AnalysisRunner::progressChanged, progressBar_, &QProgressBar::setValue);
    connect(analysisRunner_, &AnalysisRunner::statusChanged, statusLabel_, &QLabel::setText);
    connect(analysisRunner_, &AnalysisRunner::subStatusChanged, subStatusLabel_, &QLabel::setText);
    connect(analysisRunner_, &AnalysisRunner::partialResultReady, this, &AnalysisDialog::addPartialResult);
    connect(analysisRunner_, &AnalysisRunner::analysisComplete, this, &AnalysisDialog::onAnalysisComplete);
    
    analysisRunner_->start();
}

void AnalysisDialog::cancelAnalysis() {
    if (analysisRunner_) {
        analysisRunner_->cancel();
    }
}

void AnalysisDialog::onApplyClicked() {
    emit applyToGraph(currentResult_);
}

void AnalysisDialog::addPartialResult(const AnalysisResult::ResultItem& item) {
    QString itemText = QString::fromStdString(item.message) + "\n";
    statisticsText_->append(itemText);
}

void AnalysisDialog::onAnalysisComplete(const AnalysisResult& result) {
    currentResult_ = result;
    
    runButton_->setEnabled(true);
    cancelButton_->setEnabled(false);
    
    if (result.status == AnalysisResult::Success) {
        statusLabel_->setText("Analysis complete");
        applyButton_->setEnabled(tool_->canApplyToGraph());
        
        // Clear previous results and display everything
        statisticsText_->clear();
        
        // Display any result items
        for (const auto& item : result.items) {
            statisticsText_->append(QString::fromStdString(item.message));
            statisticsText_->append("");  // Blank line between items
        }
        
        // Add separator if there were items
        if (!result.items.empty()) {
            statisticsText_->append("=" + QString("=").repeated(70));
            statisticsText_->append("");
        }
        
        displayFinalStatistics(result);
    } else if (result.status == AnalysisResult::Failed) {
        statusLabel_->setText("Analysis failed");
        QMessageBox::warning(this, "Analysis Error", QString::fromStdString(result.summary));
    } else {
        statusLabel_->setText("Analysis cancelled");
    }
}

void AnalysisDialog::displayFinalStatistics(const AnalysisResult& result) {
    QString stats = QString::fromStdString(result.summary) + "\n\nStatistics:\n";
    for (const auto& [key, value] : result.statistics) {
        QString valueStr;
        if (std::holds_alternative<bool>(value)) {
            valueStr = std::get<bool>(value) ? "true" : "false";
        } else if (std::holds_alternative<int>(value)) {
            valueStr = QString::number(std::get<int>(value));
        } else if (std::holds_alternative<long long>(value)) {
            valueStr = QString::number(std::get<long long>(value));
        } else if (std::holds_alternative<double>(value)) {
            valueStr = QString::number(std::get<double>(value));
        } else if (std::holds_alternative<std::string>(value)) {
            valueStr = QString::fromStdString(std::get<std::string>(value));
        }
        stats += QString::fromStdString(key) + ": " + valueStr + "\n";
    }
    statisticsText_->setPlainText(stats);
}

void AnalysisDialog::updateLiveStatistics() {
    // Not implemented yet
}

void AnalysisDialog::update_dependencies() {
    // Get current values of all parameters
    std::map<std::string, ParameterValue> current_values;
    for (const auto& pw : parameterWidgets_) {
        std::string name = pw.name.toStdString();
        
        switch (pw.type) {
            case ParameterType::BOOL: {
                auto* cb = qobject_cast<QCheckBox*>(pw.widget);
                if (cb) current_values[name] = cb->isChecked();
                break;
            }
            case ParameterType::INT32: {
                auto* spin = qobject_cast<QSpinBox*>(pw.widget);
                if (spin) current_values[name] = spin->value();
                break;
            }
            case ParameterType::DOUBLE: {
                auto* spin = qobject_cast<QDoubleSpinBox*>(pw.widget);
                if (spin) current_values[name] = spin->value();
                break;
            }
            case ParameterType::STRING: {
                auto* combo = qobject_cast<QComboBox*>(pw.widget);
                if (combo) {
                    current_values[name] = combo->currentText().toStdString();
                } else {
                    auto* edit = qobject_cast<QLineEdit*>(pw.widget);
                    if (edit) current_values[name] = edit->text().toStdString();
                }
                break;
            }
            default:
                break;
        }
    }
    
    // Check each parameter's dependencies
    for (size_t i = 0; i < parameterDescriptors_.size(); ++i) {
        const auto& desc = parameterDescriptors_[i];
        
        if (!desc.constraints.depends_on.has_value()) {
            continue;  // No dependency, always enabled
        }
        
        const auto& dep = *desc.constraints.depends_on;
        bool should_enable = false;
        
        // Find the value of the parameter we depend on
        auto it = current_values.find(dep.parameter_name);
        if (it != current_values.end()) {
            // Convert current value to string for comparison
            std::string current_val = orc::parameter_util::value_to_string(it->second);
            
            // Check if current value is in the list of required values
            should_enable = std::find(dep.required_values.begin(), 
                                     dep.required_values.end(), 
                                     current_val) != dep.required_values.end();
        }
        
        // Enable or disable the widget and label
        if (i < parameterWidgets_.size()) {
            parameterWidgets_[i].widget->setEnabled(should_enable);
            if (parameterWidgets_[i].label) {
                parameterWidgets_[i].label->setEnabled(should_enable);
            }
        }
    }
}

void AnalysisDialog::exportResults() {
    // Get last export directory from settings
    QSettings settings("orc-project", "orc-gui");
    QString lastDir = settings.value("lastAnalysisExportDirectory", QString()).toString();
    if (lastDir.isEmpty() || !QFileInfo(lastDir).isDir()) {
        lastDir = QDir::homePath();
    }
    
    QString fileName = QFileDialog::getSaveFileName(this, "Export Results", lastDir, "Text Files (*.txt)");
    if (fileName.isEmpty()) return;
    
    // Save directory for next time
    settings.setValue("lastAnalysisExportDirectory", QFileInfo(fileName).absolutePath());
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed", "Could not open file for writing");
        return;
    }
    
    QTextStream out(&file);
    out << statisticsText_->toPlainText();
}

} // namespace gui
} // namespace orc
