#pragma once
#include <QMainWindow>

#include "gcode/parser.h"

class MachineClient;
class GcodeView;
class GcodeView3D;
class CamPanel;
class QStackedWidget;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QProgressBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void openPath(const QString& path);
    void autoConnectTcp(const QString& host, int port);
    void openCamGerber(const QString& path);
    void forceView3D();
    void loadStlPath(const QString& p);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void doConnect();
    void openGcode();
    void openGerber();
    void openDrill();
    void saveGcode();
    void runJob();
    void heightMapWizard();
    void zTouchOff();

protected:
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    void loadProgramText(const QString& text, const QString& title);
    void appendConsole(const QString& line, bool sent);
    QWidget* buildSidePanel();
    QWidget* buildConnectionBox();
    QWidget* buildDroBox();
    QWidget* buildJogBox();
    QWidget* buildJobBox();
    void setStep(double mm);
    double jogFeed() const;
    void jogAxis(const QString& axes);   // keyboard/held jog helper

    MachineClient* mc_;
    GcodeView* view_;
    GcodeView3D* view3d_ = nullptr;
    QStackedWidget* viewStack_;
    CamPanel* cam_;
    void showProgram(const scnc::Program& p);  // push to both views
    void openStl();
    scnc::Program program_;
    QString programText_;

    // connection
    QComboBox* connType_;
    QLineEdit *hostEdit_, *deviceEdit_;
    QSpinBox *portSpin_, *baudSpin_;
    QWidget *netRow_, *serialRow_;
    QPushButton* connectBtn_;

    // DRO
    QLabel* stateLabel_;
    QLabel* work_[3];
    QLabel* mach_[3];
    QLabel* feedLabel_;
    QPushButton* unlockBtn_;

    // jog
    double curStep_ = 1.0;
    QList<QPushButton*> stepBtns_;
    QDoubleSpinBox* jogFeedSpin_;
    bool jogging_ = false;
    bool jogHeld_ = false;

    // job
    QPushButton *runBtn_, *holdBtn_, *stopBtn_;
    QProgressBar* jobBar_;

    QPlainTextEdit* console_;
    QLineEdit* cmdEdit_;
};
