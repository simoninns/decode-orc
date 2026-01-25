#ifndef ORC_GUI_ANALYSIS_PROGRESS_IMPL_H
#define ORC_GUI_ANALYSIS_PROGRESS_IMPL_H

#include "../../core/analysis/analysis_progress.h"
#include <QObject>
#include <QMetaObject>
#include <atomic>

namespace orc {
namespace gui {

/**
 * @brief Qt-based progress implementation for GUI
 */
class GuiAnalysisProgress : public QObject, public AnalysisProgress {
    Q_OBJECT

public:
    GuiAnalysisProgress(QObject* parent = nullptr);

    // AnalysisProgress interface (std::string)
    void setProgress(int percentage) override;
    void setStatus(const std::string& message) override;
    void setSubStatus(const std::string& message) override;
    bool isCancelled() const override;
    void reportPartialResult(const AnalysisResult::ResultItem& item) override;

    void cancel();

signals:
    void progressChanged(int percentage);
    void statusChanged(const QString& message);
    void subStatusChanged(const QString& message);
    void partialResultReady(const AnalysisResult::ResultItem& item);

private:
    std::atomic<bool> cancelled_;
};

} // namespace gui
} // namespace orc

#endif // ORC_GUI_ANALYSIS_PROGRESS_IMPL_H
