// Integrated PCB CAM panel (dockable): copper isolation + drilling in one
// view. Load a Gerber and/or Excellon, tweak parameters with live preview
// overlaid on the toolpath view, then send either job to the sender.
#pragma once
#include <QWidget>
#include <QString>

#include <clipper2/clipper.h>

class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QComboBox;

class CamPanel : public QWidget {
    Q_OBJECT
public:
    explicit CamPanel(QWidget* parent = nullptr);

    void loadGerber(const QString& path);
    void loadDrill(const QString& path);

signals:
    // A layer's toolpaths changed; host shows copper (may be empty) + gcode
    // preview without loading it as the active job.
    void previewReady(const Clipper2Lib::PathsD& copper, const QString& gcode,
                      const QString& title);
    // User pressed "Load into sender".
    void sendToJob(const QString& gcode, const QString& title);

public:
    void setFacingArea(double x0, double y0, double w, double h);

private slots:
    void browseGerber();
    void browseDrill();
    void regenIso();
    void regenDrill();
    void regenFace();

private:
    QWidget* buildIsoTab();
    QWidget* buildDrillTab();
    QWidget* buildFaceTab();

    QTabWidget* tabs_;

    // isolation
    QLineEdit* gerberPath_;
    QString gerberText_, gerberName_;
    Clipper2Lib::PathsD copper_;
    QComboBox* isoToolPick_;
    QDoubleSpinBox *isoTool_, *isoOverlap_, *isoDepth_, *isoTravel_, *isoFeed_,
        *isoPlunge_;
    QSpinBox *isoPasses_, *isoRpm_;
    QCheckBox* isoMirror_;
    QLabel *isoSummary_, *isoToolNote_;
    QPushButton *isoPreviewBtn_, *isoSendBtn_;
    QString isoGcode_;
    void applyPickedTool();      // fills width/feed/rpm from the chosen tool
    void refreshToolPicker();

    // drill
    QLineEdit* drillPath_;
    QString drillText_, drillName_;
    QDoubleSpinBox *drillDepth_, *drillTravel_, *drillPlunge_;
    QSpinBox* drillRpm_;
    QCheckBox *drillMirror_, *drillSingle_;
    QLabel* drillSummary_;
    QPushButton* drillSendBtn_;
    QString drillGcode_;

    // facing
    QDoubleSpinBox *faceX_, *faceY_, *faceW_, *faceH_, *faceTool_,
        *faceStepover_, *faceDepth_, *facePass_, *faceFeed_, *facePlunge_,
        *faceTravel_;
    QSpinBox* faceRpm_;
    QCheckBox* faceSpiral_;
    QLabel* faceSummary_;
    QPushButton* faceSendBtn_;
    QString faceGcode_;
};
