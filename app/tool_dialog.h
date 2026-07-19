// Tool library editor: list of tools on the left, an editable form on the
// right. Fields relevant to the selected tool type are shown; a live
// "isolation width at N mm" readout makes V-bit behaviour tangible.
#pragma once
#include <QDialog>

#include "cam/tool.h"

class QListWidget;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QPlainTextEdit;
class QFormLayout;

class ToolDialog : public QDialog {
    Q_OBJECT
public:
    explicit ToolDialog(QWidget* parent = nullptr);

private slots:
    void selectRow(int row);
    void addTool();
    void duplicateTool();
    void removeTool();
    void commitForm();

private:
    void refreshList(int selectId = -1);
    void loadForm(const scnc::Tool& t);
    void updateTypeVisibility();
    void updateWidthPreview();
    int currentToolId() const;

    QListWidget* list_;
    QLineEdit* name_;
    QComboBox* type_;
    QDoubleSpinBox *dia_, *shank_, *length_, *tipAngle_, *corner_, *feed_,
        *plunge_, *widthDepth_;
    QSpinBox *flutes_, *rpm_;
    QPlainTextEdit* notes_;
    QLabel *widthPreview_, *tipRow_, *cornerRow_;
    QFormLayout* form_;
    bool loading_ = false;
};
