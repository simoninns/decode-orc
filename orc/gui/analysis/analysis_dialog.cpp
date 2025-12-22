#include "analysis_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>

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
    for (const auto& param : tool_->parameters()) {
        QWidget* widget = createParameterWidget(param);
        parametersLayout_->addRow(QString::fromStdString(param.name), widget);
        
        ParameterWidget pw;
        pw.name = QString::fromStdString(param.name);
        pw.widget = widget;
        pw.type = param.type;
        parameterWidgets_.push_back(pw);
    }
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
            }
            if (param.constraints.max_value && std::holds_alternative<int32_t>(*param.constraints.max_value)) {
                spin->setMaximum(std::get<int32_t>(*param.constraints.max_value));
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
        case ParameterType::STRING:
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
                auto* edit = qobject_cast<QLineEdit*>(pw.widget);
                if (edit) context_.parameters[name] = edit->text().toStdString();
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

void AnalysisDialog::exportResults() {
    QString fileName = QFileDialog::getSaveFileName(this, "Export Results", "", "Text Files (*.txt)");
    if (fileName.isEmpty()) return;
    
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
