#include "analysis_runner.h"

namespace orc {
namespace gui {

AnalysisRunner::AnalysisRunner(AnalysisTool* tool,
                               const AnalysisContext& ctx,
                               QObject* parent)
    : QThread(parent), tool_(tool), context_(ctx) {
    progress_ = std::make_unique<GuiAnalysisProgress>();
}

void AnalysisRunner::cancel() {
    progress_->cancel();
}

AnalysisProgress* AnalysisRunner::progress() {
    return progress_.get();
}

void AnalysisRunner::run() {
    // Connect progress signals to our signals
    connect(progress_.get(), &GuiAnalysisProgress::progressChanged,
            this, &AnalysisRunner::progressChanged);
    connect(progress_.get(), &GuiAnalysisProgress::statusChanged,
            this, &AnalysisRunner::statusChanged);
    connect(progress_.get(), &GuiAnalysisProgress::subStatusChanged,
            this, &AnalysisRunner::subStatusChanged);
    connect(progress_.get(), &GuiAnalysisProgress::partialResultReady,
            this, &AnalysisRunner::partialResultReady);

    // Run analysis
    AnalysisResult result = tool_->analyze(context_, progress_.get());

    emit analysisComplete(result);
}

} // namespace gui
} // namespace orc
