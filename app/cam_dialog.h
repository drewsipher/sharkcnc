// PCB CAM dialog: gerber -> isolation and excellon -> drill G-code,
// with instant regeneration. The anti-FlatCAM.
#pragma once
#include <QDialog>
#include <QString>

class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;

class CamDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Isolation, Drill };
    CamDialog(Mode mode, const QString& inputPath, QWidget* parent = nullptr);

    QString gcode() const { return gcode_; }
    bool hasGcode() const { return !gcode_.isEmpty(); }

private slots:
    void regenerate();

private:
    Mode mode_;
    QString inputText_;
    QString gcode_;

    QDoubleSpinBox *tool_, *overlap_, *depth_, *travel_, *feed_, *plunge_;
    QSpinBox *passes_, *rpm_;
    QCheckBox *mirror_, *singleTool_;
    QLabel* summary_;
};
