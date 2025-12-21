#ifndef ORC_GUI_INSPECTION_DIALOG_H
#define ORC_GUI_INSPECTION_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QVBoxLayout>
#include "../core/stages/stage.h"

namespace orc {

class InspectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit InspectionDialog(const StageReport& report, QWidget* parent = nullptr);
    ~InspectionDialog() override = default;

private:
    void buildUI(const StageReport& report);
    
    QTextEdit* reportText_;
};

} // namespace orc

#endif // ORC_GUI_INSPECTION_DIALOG_H
