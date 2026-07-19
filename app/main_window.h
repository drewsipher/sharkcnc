#pragma once
#include <QMainWindow>

#include "gcode/parser.h"

class MachineClient;
class GcodeView;
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

private:
    void loadProgramText(const QString& text, const QString& title);
    void appendConsole(const QString& line, bool sent);
    QWidget* buildSidePanel();
    double jogStep() const;
    double jogFeed() const;

    MachineClient* mc_;
    GcodeView* view_;
    scnc::Program program_;
    QString programText_;

    // side panel widgets
    QComboBox *connType_, *stepCombo_;
    QLineEdit *hostEdit_, *deviceEdit_;
    QSpinBox *portSpin_, *baudSpin_;
    QPushButton* connectBtn_;
    QLabel *stateLabel_, *wposLabel_, *mposLabel_, *feedLabel_;
    QDoubleSpinBox* jogFeedSpin_;
    QPushButton *runBtn_, *holdBtn_, *stopBtn_;
    QProgressBar* jobBar_;
    QPlainTextEdit* console_;
    QLineEdit* cmdEdit_;
};
