#include "main_window.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include "cam_dialog.h"
#include "gcode/warp.h"
#include "gcode_view.h"
#include "machine_client.h"
#include "probe_dialog.h"

using namespace scnc;

MainWindow::MainWindow() {
    setWindowTitle("SharkCNC");
    resize(1280, 820);
    mc_ = new MachineClient(this);
    view_ = new GcodeView(this);
    setCentralWidget(view_);

    auto* side = new QDockWidget("Machine", this);
    side->setFeatures(QDockWidget::DockWidgetMovable);
    side->setWidget(buildSidePanel());
    addDockWidget(Qt::LeftDockWidgetArea, side);

    auto* consoleDock = new QDockWidget("Console", this);
    consoleDock->setFeatures(QDockWidget::DockWidgetMovable);
    auto* cw = new QWidget;
    auto* cl = new QVBoxLayout(cw);
    cl->setContentsMargins(4, 4, 4, 4);
    console_ = new QPlainTextEdit;
    console_->setReadOnly(true);
    console_->setMaximumBlockCount(5000);
    console_->setFont(QFont("monospace"));
    cmdEdit_ = new QLineEdit;
    cmdEdit_->setPlaceholderText("G-code or $ command...  (Enter to send)");
    cl->addWidget(console_);
    cl->addWidget(cmdEdit_);
    consoleDock->setWidget(cw);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);

    // menus
    auto* file = menuBar()->addMenu("&File");
    file->addAction("Open &G-code...", QKeySequence::Open, this,
                    &MainWindow::openGcode);
    file->addAction("Open Ger&ber (isolation)...", this,
                    &MainWindow::openGerber);
    file->addAction("Open &Drill (Excellon)...", this, &MainWindow::openDrill);
    file->addSeparator();
    file->addAction("&Save loaded G-code...", QKeySequence::Save, this,
                    &MainWindow::saveGcode);
    file->addSeparator();
    file->addAction("&Quit", QKeySequence::Quit, qApp, &QApplication::quit);

    auto* machine = menuBar()->addMenu("&Machine");
    machine->addAction("&Home ($H)", this, [this] { mc_->home(); });
    machine->addAction("&Unlock ($X)", this, [this] { mc_->unlock(); });
    machine->addAction("Soft &reset (ctrl-x)", this,
                       [this] { mc_->softReset(); });
    machine->addSeparator();
    machine->addAction("Zero &XY here", this,
                       [this] { mc_->zeroWork("XY"); });
    machine->addAction("Zero &Z here", this, [this] { mc_->zeroWork("Z"); });

    auto* probe = menuBar()->addMenu("&Probe");
    probe->addAction("&Z touch-off...", this, &MainWindow::zTouchOff);
    probe->addAction("&Height map / autolevel...", this,
                     &MainWindow::heightMapWizard);

    // machine client wiring
    connect(mc_, &MachineClient::connected, this, [this](const QString& w) {
        connectBtn_->setText("Disconnect");
        statusBar()->showMessage("Connected: " + w);
    });
    connect(mc_, &MachineClient::connectionFailed, this,
            [this](const QString& w) {
                statusBar()->showMessage("Connection failed: " + w, 5000);
            });
    connect(mc_, &MachineClient::disconnected, this, [this] {
        connectBtn_->setText("Connect");
        stateLabel_->setText("OFFLINE");
        statusBar()->showMessage("Disconnected");
    });
    connect(mc_, &MachineClient::statusUpdated, this,
            [this](const scnc::Status& st) {
                stateLabel_->setText(QString(toString(st.state)).toUpper());
                QString color = "#808080";
                switch (st.state) {
                    case MachineState::Idle: color = "#4caf50"; break;
                    case MachineState::Run:
                    case MachineState::Jog: color = "#2196f3"; break;
                    case MachineState::Hold: color = "#ff9800"; break;
                    case MachineState::Alarm: color = "#f44336"; break;
                    default: break;
                }
                stateLabel_->setStyleSheet(
                    QString("font-size:20px;font-weight:bold;color:%1")
                        .arg(color));
                wposLabel_->setText(QString("W  X %1  Y %2  Z %3")
                                        .arg(st.wx, 8, 'f', 3)
                                        .arg(st.wy, 8, 'f', 3)
                                        .arg(st.wz, 8, 'f', 3));
                mposLabel_->setText(QString("M  X %1  Y %2  Z %3")
                                        .arg(st.mx, 8, 'f', 3)
                                        .arg(st.my, 8, 'f', 3)
                                        .arg(st.mz, 8, 'f', 3));
                feedLabel_->setText(QString("F %1   S %2")
                                        .arg(st.feed, 0, 'f', 0)
                                        .arg(st.speed, 0, 'f', 0));
                view_->setToolPosition(st.wx, st.wy);
            });
    connect(mc_, &MachineClient::lineReceived, this, [this](const QString& l) {
        if (!l.startsWith('<')) appendConsole(l, false);
    });
    connect(mc_, &MachineClient::lineSent, this,
            [this](const QString& l) { appendConsole(l, true); });
    connect(mc_, &MachineClient::alarmRaised, this, [this](int code) {
        appendConsole(QString("!! ALARM:%1").arg(code), false);
        statusBar()->showMessage(
            QString("ALARM %1 - unlock with Machine > Unlock").arg(code));
    });
    connect(mc_, &MachineClient::jobProgress, this,
            [this](int acked, int total, bool running, bool paused) {
                jobBar_->setRange(0, total);
                jobBar_->setValue(acked);
                runBtn_->setEnabled(!running);
                holdBtn_->setEnabled(running);
                holdBtn_->setText(paused ? "Resume" : "Hold");
                stopBtn_->setEnabled(running);
                if (!running && acked == total && total > 0)
                    statusBar()->showMessage("Job complete", 10000);
            });
    connect(cmdEdit_, &QLineEdit::returnPressed, this, [this] {
        QString cmd = cmdEdit_->text().trimmed();
        if (cmd.isEmpty()) return;
        mc_->sendCommand(cmd);
        cmdEdit_->clear();
    });

    QSettings s;
    hostEdit_->setText(s.value("conn/host", "fluidnc.local").toString());
    portSpin_->setValue(s.value("conn/port", 23).toInt());
    deviceEdit_->setText(
        s.value("conn/device", "/dev/ttyACM0").toString());
    baudSpin_->setValue(s.value("conn/baud", 115200).toInt());
    connType_->setCurrentIndex(s.value("conn/type", 0).toInt());
}

void MainWindow::openPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    loadProgramText(QString::fromUtf8(f.readAll()),
                    QFileInfo(path).fileName());
}

void MainWindow::autoConnectTcp(const QString& host, int port) {
    connType_->setCurrentIndex(0);
    hostEdit_->setText(host);
    portSpin_->setValue(port);
    mc_->connectTcp(host, port);
}

QWidget* MainWindow::buildSidePanel() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(6, 6, 6, 6);

    // --- connection
    auto* connBox = new QGroupBox("Connection");
    auto* cg = new QGridLayout(connBox);
    connType_ = new QComboBox;
    connType_->addItems({"Network (FluidNC)", "Serial USB"});
    hostEdit_ = new QLineEdit;
    portSpin_ = new QSpinBox;
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(23);
    deviceEdit_ = new QLineEdit("/dev/ttyACM0");
    baudSpin_ = new QSpinBox;
    baudSpin_->setRange(9600, 921600);
    baudSpin_->setValue(115200);
    connectBtn_ = new QPushButton("Connect");
    cg->addWidget(connType_, 0, 0, 1, 2);
    cg->addWidget(hostEdit_, 1, 0);
    cg->addWidget(portSpin_, 1, 1);
    cg->addWidget(deviceEdit_, 2, 0);
    cg->addWidget(baudSpin_, 2, 1);
    cg->addWidget(connectBtn_, 3, 0, 1, 2);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::doConnect);
    lay->addWidget(connBox);

    // --- DRO
    auto* droBox = new QGroupBox("Position");
    auto* dg = new QVBoxLayout(droBox);
    stateLabel_ = new QLabel("OFFLINE");
    stateLabel_->setStyleSheet("font-size:20px;font-weight:bold");
    wposLabel_ = new QLabel("W  X    0.000  Y    0.000  Z    0.000");
    mposLabel_ = new QLabel("M  X    0.000  Y    0.000  Z    0.000");
    feedLabel_ = new QLabel("F 0   S 0");
    for (auto* l : {wposLabel_, mposLabel_, feedLabel_})
        l->setFont(QFont("monospace", 11));
    wposLabel_->setStyleSheet("font-weight:bold");
    dg->addWidget(stateLabel_);
    dg->addWidget(wposLabel_);
    dg->addWidget(mposLabel_);
    dg->addWidget(feedLabel_);
    lay->addWidget(droBox);

    // --- jog
    auto* jogBox = new QGroupBox("Jog  (arrows / PgUp / PgDn)");
    auto* jg = new QGridLayout(jogBox);
    auto jbtn = [&](const QString& t, const QString& axes) {
        auto* b = new QPushButton(t);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QPushButton::clicked, this, [this, axes] {
            mc_->jog(axes.arg(jogStep()), jogFeed());
        });
        return b;
    };
    jg->addWidget(jbtn("Y+", "Y%1"), 0, 1);
    jg->addWidget(jbtn("Y-", "Y-%1"), 2, 1);
    jg->addWidget(jbtn("X-", "X-%1"), 1, 0);
    jg->addWidget(jbtn("X+", "X%1"), 1, 2);
    jg->addWidget(jbtn("Z+", "Z%1"), 0, 3);
    jg->addWidget(jbtn("Z-", "Z-%1"), 2, 3);
    stepCombo_ = new QComboBox;
    stepCombo_->addItems({"0.01", "0.1", "1", "10"});
    stepCombo_->setCurrentIndex(2);
    jogFeedSpin_ = new QDoubleSpinBox;
    jogFeedSpin_->setRange(1, 5000);
    jogFeedSpin_->setValue(500);
    jogFeedSpin_->setSuffix(" mm/min");
    jg->addWidget(new QLabel("Step"), 3, 0);
    jg->addWidget(stepCombo_, 3, 1);
    jg->addWidget(jogFeedSpin_, 3, 2, 1, 2);
    lay->addWidget(jogBox);

    // --- job
    auto* jobBox = new QGroupBox("Job");
    auto* bg = new QGridLayout(jobBox);
    runBtn_ = new QPushButton("Run");
    holdBtn_ = new QPushButton("Hold");
    stopBtn_ = new QPushButton("Stop");
    holdBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    jobBar_ = new QProgressBar;
    bg->addWidget(runBtn_, 0, 0);
    bg->addWidget(holdBtn_, 0, 1);
    bg->addWidget(stopBtn_, 0, 2);
    bg->addWidget(jobBar_, 1, 0, 1, 3);
    connect(runBtn_, &QPushButton::clicked, this, &MainWindow::runJob);
    connect(holdBtn_, &QPushButton::clicked, this, [this] {
        if (holdBtn_->text() == "Hold")
            mc_->pauseJob();
        else
            mc_->resumeJob();
    });
    connect(stopBtn_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Stop job",
                                  "Stop the job and reset the controller?") ==
            QMessageBox::Yes)
            mc_->stopJob();
    });
    lay->addWidget(jobBox);
    lay->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidget(w);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(280);
    return scroll;
}

double MainWindow::jogStep() const {
    return stepCombo_->currentText().toDouble();
}
double MainWindow::jogFeed() const { return jogFeedSpin_->value(); }

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (mc_->isConnected() && !cmdEdit_->hasFocus()) {
        double s = jogStep();
        auto j = [&](const QString& axes) {
            mc_->jog(axes, jogFeed());
            e->accept();
        };
        switch (e->key()) {
            case Qt::Key_Left: return j(QString("X-%1").arg(s));
            case Qt::Key_Right: return j(QString("X%1").arg(s));
            case Qt::Key_Up: return j(QString("Y%1").arg(s));
            case Qt::Key_Down: return j(QString("Y-%1").arg(s));
            case Qt::Key_PageUp: return j(QString("Z%1").arg(s));
            case Qt::Key_PageDown: return j(QString("Z-%1").arg(s));
            default: break;
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::doConnect() {
    if (mc_->isConnected()) {
        mc_->disconnectMachine();
        return;
    }
    QSettings s;
    s.setValue("conn/host", hostEdit_->text());
    s.setValue("conn/port", portSpin_->value());
    s.setValue("conn/device", deviceEdit_->text());
    s.setValue("conn/baud", baudSpin_->value());
    s.setValue("conn/type", connType_->currentIndex());
    if (connType_->currentIndex() == 0)
        mc_->connectTcp(hostEdit_->text(), portSpin_->value());
    else
        mc_->connectSerial(deviceEdit_->text(), baudSpin_->value());
}

void MainWindow::loadProgramText(const QString& text, const QString& title) {
    programText_ = text;
    program_ = parseGcode(text.toStdString());
    view_->setProgram(program_);
    setWindowTitle("SharkCNC - " + title);
    statusBar()->showMessage(
        QString("%1 lines, %2 motion segments")
            .arg(programText_.count('\n'))
            .arg(program_.segments.size()));
}

void MainWindow::openGcode() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open G-code", s.value("dir/gcode").toString(),
        "G-code (*.nc *.gcode *.ngc *.tap *.txt);;All files (*)");
    if (fn.isEmpty()) return;
    s.setValue("dir/gcode", QFileInfo(fn).absolutePath());
    QFile f(fn);
    if (!f.open(QIODevice::ReadOnly)) return;
    loadProgramText(QString::fromUtf8(f.readAll()), QFileInfo(fn).fileName());
}

void MainWindow::openGerber() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open Gerber (copper layer)", s.value("dir/gerber").toString(),
        "Gerber (*.gbr *.gtl *.gbl *.g *.pho);;All files (*)");
    if (fn.isEmpty()) return;
    s.setValue("dir/gerber", QFileInfo(fn).absolutePath());
    CamDialog dlg(CamDialog::Mode::Isolation, fn, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasGcode())
        loadProgramText(dlg.gcode(),
                        QFileInfo(fn).fileName() + " [isolation]");
}

void MainWindow::openDrill() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open Excellon drill", s.value("dir/gerber").toString(),
        "Excellon (*.drl *.xln *.txt);;All files (*)");
    if (fn.isEmpty()) return;
    s.setValue("dir/gerber", QFileInfo(fn).absolutePath());
    CamDialog dlg(CamDialog::Mode::Drill, fn, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasGcode())
        loadProgramText(dlg.gcode(), QFileInfo(fn).fileName() + " [drill]");
}

void MainWindow::saveGcode() {
    if (programText_.isEmpty()) return;
    QString fn = QFileDialog::getSaveFileName(this, "Save G-code", "out.nc",
                                              "G-code (*.nc)");
    if (fn.isEmpty()) return;
    QFile f(fn);
    if (f.open(QIODevice::WriteOnly)) f.write(programText_.toUtf8());
}

void MainWindow::runJob() {
    if (programText_.isEmpty()) {
        statusBar()->showMessage("Load a G-code file first", 4000);
        return;
    }
    if (!mc_->isConnected()) {
        statusBar()->showMessage("Not connected", 4000);
        return;
    }
    if (program_.sawInches || program_.sawRelative) {
        if (QMessageBox::warning(
                this, "Heads up",
                "Program uses " +
                    QString(program_.sawInches ? "G20 inches" : "G91 relative")
                    + " - stream anyway?",
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
    }
    mc_->runProgram(programText_.split('\n'));
}

void MainWindow::zTouchOff() {
    if (!mc_->isConnected()) return;
    bool ok = false;
    double thickness = QInputDialog::getDouble(
        this, "Z touch-off", "Probe plate thickness (mm):", 0.0, 0, 50, 3,
        &ok);
    if (!ok) return;
    auto* conn = new QMetaObject::Connection;
    *conn = connect(mc_, &MachineClient::probeFinished, this,
                    [this, thickness, conn](bool good, double, double, double) {
                        QObject::disconnect(*conn);
                        delete conn;
                        if (!good) {
                            statusBar()->showMessage("Probe failed", 5000);
                            return;
                        }
                        mc_->sendCommand(
                            QString("G10 L20 P0 Z%1").arg(thickness));
                        mc_->sendCommand("G0 Z5");
                        statusBar()->showMessage("Z zeroed at surface", 5000);
                    });
    mc_->probe("G38.2 Z-25 F40");
}

void MainWindow::heightMapWizard() {
    double x0 = 0, y0 = 0, x1 = 30, y1 = 20;
    if (program_.hasBounds()) {
        x0 = program_.min.x;
        y0 = program_.min.y;
        x1 = program_.max.x;
        y1 = program_.max.y;
    }
    ProbeDialog dlg(mc_, x0, y0, x1, y1, this);
    if (dlg.exec() == QDialog::Accepted && dlg.hasMap() &&
        !programText_.isEmpty()) {
        auto r = warpGcode(program_, dlg.map(), {});
        if (!r.ok) {
            QMessageBox::warning(this, "Warp failed",
                                 QString::fromStdString(r.error));
            return;
        }
        loadProgramText(QString::fromStdString(r.gcode),
                        windowTitle().section(" - ", 1) + " [warped]");
        statusBar()->showMessage(
            "Height map applied - review the preview, then Run", 8000);
    }
}

void MainWindow::appendConsole(const QString& line, bool sent) {
    console_->appendHtml(
        QString("<span style='color:%1'>%2%3</span>")
            .arg(sent ? "#7a7a7a" : "#c8e6c9", sent ? "&gt; " : "",
                 line.toHtmlEscaped()));
}
