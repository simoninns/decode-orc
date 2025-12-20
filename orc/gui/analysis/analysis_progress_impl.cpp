#include "analysis_progress_impl.h"
#include <QString>

namespace orc {
namespace gui {

GuiAnalysisProgress::GuiAnalysisProgress(QObject* parent)
    : QObject(parent), cancelled_(false) {
}

void GuiAnalysisProgress::setProgress(int percentage) {
    QMetaObject::invokeMethod(this, [this, percentage]() {
        emit progressChanged(percentage);
    }, Qt::QueuedConnection);
}

void GuiAnalysisProgress::setStatus(const std::string& message) {
    QString qmsg = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, qmsg]() {
        emit statusChanged(qmsg);
    }, Qt::QueuedConnection);
}

void GuiAnalysisProgress::setSubStatus(const std::string& message) {
    QString qmsg = QString::fromStdString(message);
    QMetaObject::invokeMethod(this, [this, qmsg]() {
        emit subStatusChanged(qmsg);
    }, Qt::QueuedConnection);
}

bool GuiAnalysisProgress::isCancelled() const {
    return cancelled_.load();
}

void GuiAnalysisProgress::reportPartialResult(
        const AnalysisResult::ResultItem& item) {
    QMetaObject::invokeMethod(this, [this, item]() {
        emit partialResultReady(item);
    }, Qt::QueuedConnection);
}

void GuiAnalysisProgress::cancel() {
    cancelled_.store(true);
}

} // namespace gui
} // namespace orc
