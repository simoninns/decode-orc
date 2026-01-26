#ifndef ORC_GUI_INSPECTION_DIALOG_H
#define ORC_GUI_INSPECTION_DIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QVBoxLayout>
#include "presenters/include/stage_inspection_view_models.h"

namespace orc {

class InspectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit InspectionDialog(const orc::presenters::StageInspectionView& report, QWidget* parent = nullptr);
    ~InspectionDialog() override = default;

private:
    void buildUI(const orc::presenters::StageInspectionView& report);
    
    QTextEdit* reportText_;
};

} // namespace orc

#endif // ORC_GUI_INSPECTION_DIALOG_H
