#include "inspection_dialog.h"
#include <QPushButton>
#include <QLabel>
#include <sstream>

namespace orc {

InspectionDialog::InspectionDialog(const StageReport& report, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Stage Inspection");
    resize(600, 400);
    
    buildUI(report);
}

void InspectionDialog::buildUI(const StageReport& report)
{
    auto* layout = new QVBoxLayout(this);
    
    // Summary label
    if (!report.summary.empty()) {
        auto* summaryLabel = new QLabel(QString::fromStdString("<b>" + report.summary + "</b>"));
        layout->addWidget(summaryLabel);
    }
    
    // Build the report text
    std::ostringstream oss;
    
    // Configuration items
    if (!report.items.empty()) {
        oss << "Configuration:\n";
        oss << "==============\n\n";
        for (const auto& [label, value] : report.items) {
            oss << label << ": " << value << "\n";
        }
        oss << "\n";
    }
    
    // Metrics
    if (!report.metrics.empty()) {
        oss << "Metrics:\n";
        oss << "========\n\n";
        for (const auto& [name, value] : report.metrics) {
            oss << name << ": ";
            std::visit([&oss](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    oss << val;
                } else {
                    oss << val;
                }
            }, value);
            oss << "\n";
        }
    }
    
    // Text display
    reportText_ = new QTextEdit(this);
    reportText_->setReadOnly(true);
    reportText_->setPlainText(QString::fromStdString(oss.str()));
    reportText_->setMinimumHeight(300);
    layout->addWidget(reportText_);
    
    // Close button
    auto* closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeButton);
}

} // namespace orc
