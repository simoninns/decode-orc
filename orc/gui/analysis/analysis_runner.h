#ifndef ORC_GUI_ANALYSIS_RUNNER_H
#define ORC_GUI_ANALYSIS_RUNNER_H

#include "../../core/analysis/analysis_tool.h"
#include "../../core/analysis/analysis_context.h"
#include "../../core/analysis/analysis_result.h"
#include "analysis_progress_impl.h"
#include <QThread>
#include <memory>

namespace orc {
namespace gui {

/**
 * @brief Thread for running analysis tools in the background
 */
class AnalysisRunner : public QThread {
    Q_OBJECT

public:
    AnalysisRunner(AnalysisTool* tool,
                   const AnalysisContext& ctx,
                   QObject* parent = nullptr);

    void cancel();
    AnalysisProgress* progress();

signals:
    void analysisComplete(const AnalysisResult& result);
    void progressChanged(int percentage);
    void statusChanged(const QString& message);
    void subStatusChanged(const QString& message);
    void partialResultReady(const AnalysisResult::ResultItem& item);

protected:
    void run() override;

private:
    AnalysisTool* tool_;
    AnalysisContext context_;
    std::unique_ptr<GuiAnalysisProgress> progress_;
};

} // namespace gui
} // namespace orc

#endif // ORC_GUI_ANALYSIS_RUNNER_H
