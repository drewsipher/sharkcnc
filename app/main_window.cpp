#include "main_window.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
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
#include <QShortcut>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <QStackedWidget>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

#include "cam_panel.h"
#include "gcode/warp.h"
#include "gcode_view.h"
#include "gcode_view3d.h"
#include "machine_client.h"
#include "probe_dialog.h"
#include "tool_dialog.h"

using namespace scnc;

MainWindow::MainWindow() {
    setWindowTitle("SharkCNC");
    resize(1360, 920);
    mc_ = new MachineClient(this);
    view_ = new GcodeView(this);
    view3d_ = new GcodeView3D(this);
    viewStack_ = new QStackedWidget(this);
    viewStack_->addWidget(view_);     // index 0 = 2D
    viewStack_->addWidget(view3d_);   // index 1 = 3D
    setCentralWidget(viewStack_);

    // side docks own the bottom corners, so they run full-height and the
    // console only spans beneath the preview
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto* side = new QDockWidget("Machine", this);
    side->setFeatures(QDockWidget::DockWidgetMovable);
    side->setWidget(buildSidePanel());
    addDockWidget(Qt::LeftDockWidgetArea, side);

    // integrated PCB CAM panel (right dock)
    auto* camDock = new QDockWidget("PCB CAM", this);
    camDock->setFeatures(QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetClosable);
    cam_ = new CamPanel(camDock);
    camDock->setWidget(cam_);
    addDockWidget(Qt::RightDockWidgetArea, camDock);
    connect(cam_, &CamPanel::previewReady, this,
            [this](const Clipper2Lib::PathsD& copper, const QString& gcode,
                   const QString& title) {
                program_ = parseGcode(gcode.toStdString());
                programText_ = gcode;
                showProgram(program_);
                view_->setCopper(copper);
                setWindowTitle("SharkCNC - " + title + " (preview)");
                statusBar()->showMessage(
                    "CAM preview - adjust params, then Load into sender",
                    4000);
            });
    connect(cam_, &CamPanel::sendToJob, this,
            [this](const QString& gcode, const QString& title) {
                loadProgramText(gcode, title);
                view_->clearCopper();
                statusBar()->showMessage("Loaded into sender - ready to Run",
                                         5000);
            });

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
    file->addAction("Open Ger&ber → CAM panel...", this,
                    &MainWindow::openGerber);
    file->addAction("Open &Drill → CAM panel...", this,
                    &MainWindow::openDrill);
    file->addAction("Open &STL (3D stock/part)...", this, &MainWindow::openStl);
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

    auto* view = menuBar()->addMenu("&View");
    auto* mode3d = view->addAction("&3D view");
    mode3d->setCheckable(true);
    mode3d->setShortcut(QKeySequence(Qt::Key_Tab));
    connect(mode3d, &QAction::toggled, this, [this](bool on) {
        viewStack_->setCurrentIndex(on ? 1 : 0);
    });
    auto* persp = view->addAction("&Perspective");
    persp->setCheckable(true);
    connect(persp, &QAction::toggled, this,
            [this](bool on) { view3d_->setPerspective(on); });
    view->addSeparator();
    view->addAction("&Top", QKeySequence(Qt::Key_7), this, [this, mode3d] {
        mode3d->setChecked(true);
        view3d_->viewTop();
    });
    view->addAction("&Front", QKeySequence(Qt::Key_1), this, [this, mode3d] {
        mode3d->setChecked(true);
        view3d_->viewFront();
    });
    view->addAction("&Isometric", QKeySequence(Qt::Key_0), this,
                    [this, mode3d] {
                        mode3d->setChecked(true);
                        view3d_->viewIso();
                    });
    view->addSeparator();
    view->addAction("Zoom &in", QKeySequence::ZoomIn, this,
                    [this] { view_->zoom(1.25); });
    view->addAction("Zoom &out", QKeySequence::ZoomOut, this,
                    [this] { view_->zoom(0.8); });
    view->addAction("&Fit", QKeySequence(Qt::Key_F), this, [this] {
        view_->fit();
        view3d_->fit();
    });

    auto* tools = menuBar()->addMenu("&Tools");
    tools->addAction("Tool &library...", this, [this] {
        ToolDialog dlg(this);
        dlg.exec();
    });

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
                unlockBtn_->setVisible(st.state == MachineState::Alarm);
                const double wv[3] = {st.wx, st.wy, st.wz};
                const double mv[3] = {st.mx, st.my, st.mz};
                for (int i = 0; i < 3; ++i) {
                    work_[i]->setText(QString::number(wv[i], 'f', 3));
                    mach_[i]->setText("M " + QString::number(mv[i], 'f', 3));
                }
                feedLabel_->setText(QString("F %1   S %2")
                                        .arg(st.feed, 0, 'f', 0)
                                        .arg(st.speed, 0, 'f', 0));
                view_->setToolPosition(st.wx, st.wy);
                view3d_->setToolPosition(st.wx, st.wy, st.wz);
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
    connect(mc_, &MachineClient::toolChangeRequested, this,
            [this](const QString& msg) {
                appendConsole("== TOOL CHANGE: " + msg, false);
                QMessageBox box(this);
                box.setWindowTitle("Tool change");
                box.setIcon(QMessageBox::Information);
                box.setText("<b>" + msg.toHtmlEscaped() + "</b>");
                box.setInformativeText(
                    "Insert the tool, re-touch off Z if needed, then Resume.");
                auto* resumeB = box.addButton("Resume", QMessageBox::AcceptRole);
                box.addButton("Cancel job", QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == resumeB)
                    mc_->resumeJob();
                else
                    mc_->stopJob();
            });
    connect(cmdEdit_, &QLineEdit::returnPressed, this, [this] {
        QString cmd = cmdEdit_->text().trimmed();
        if (cmd.isEmpty()) return;
        mc_->sendCommand(cmd);
        cmdEdit_->clear();
    });

    setAcceptDrops(true);

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

void MainWindow::openCamGerber(const QString& path) { cam_->loadGerber(path); }

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
    lay->setSpacing(8);
    lay->addWidget(buildConnectionBox());
    lay->addWidget(buildDroBox());
    lay->addWidget(buildJogBox());
    lay->addWidget(buildJobBox());
    lay->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidget(w);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(300);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget* MainWindow::buildConnectionBox() {
    auto* box = new QGroupBox("Connection");
    auto* v = new QVBoxLayout(box);
    connType_ = new QComboBox;
    connType_->addItems({"Network (FluidNC)", "Serial USB"});
    v->addWidget(connType_);

    netRow_ = new QWidget;
    auto* nh = new QHBoxLayout(netRow_);
    nh->setContentsMargins(0, 0, 0, 0);
    hostEdit_ = new QLineEdit;
    hostEdit_->setPlaceholderText("host / IP");
    portSpin_ = new QSpinBox;
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(23);
    portSpin_->setMaximumWidth(72);
    nh->addWidget(hostEdit_, 1);
    nh->addWidget(portSpin_);
    v->addWidget(netRow_);

    serialRow_ = new QWidget;
    auto* sh = new QHBoxLayout(serialRow_);
    sh->setContentsMargins(0, 0, 0, 0);
    deviceEdit_ = new QLineEdit("/dev/ttyACM0");
    baudSpin_ = new QSpinBox;
    baudSpin_->setRange(9600, 921600);
    baudSpin_->setValue(115200);
    baudSpin_->setMaximumWidth(90);
    sh->addWidget(deviceEdit_, 1);
    sh->addWidget(baudSpin_);
    v->addWidget(serialRow_);

    connectBtn_ = new QPushButton("Connect");
    connectBtn_->setMinimumHeight(30);
    v->addWidget(connectBtn_);

    auto showFields = [this](int idx) {
        netRow_->setVisible(idx == 0);
        serialRow_->setVisible(idx == 1);
    };
    connect(connType_, &QComboBox::currentIndexChanged, this, showFields);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::doConnect);
    showFields(0);
    return box;
}

QWidget* MainWindow::buildDroBox() {
    auto* box = new QGroupBox("Position");
    auto* v = new QVBoxLayout(box);

    auto* top = new QHBoxLayout;
    stateLabel_ = new QLabel("OFFLINE");
    stateLabel_->setStyleSheet("font-size:20px;font-weight:bold;color:#808080");
    unlockBtn_ = new QPushButton("Unlock");
    unlockBtn_->setVisible(false);
    unlockBtn_->setStyleSheet(
        "background:#c62828;font-weight:bold;border-color:#c62828");
    connect(unlockBtn_, &QPushButton::clicked, this, [this] { mc_->unlock(); });
    top->addWidget(stateLabel_);
    top->addStretch(1);
    top->addWidget(unlockBtn_);
    v->addLayout(top);

    const char* names[3] = {"X", "Y", "Z"};
    const char* axisColor[3] = {"#e06c6c", "#7bc47b", "#6ca6e0"};
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    for (int i = 0; i < 3; ++i) {
        auto* lbl = new QLabel(names[i]);
        lbl->setStyleSheet(QString("font-weight:bold;font-size:16px;color:%1")
                               .arg(axisColor[i]));
        work_[i] = new QLabel("0.000");
        work_[i]->setFont(QFont("monospace", 15, QFont::Bold));
        work_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mach_[i] = new QLabel("M 0.000");
        mach_[i]->setFont(QFont("monospace", 8));
        mach_[i]->setStyleSheet("color:#808488");
        mach_[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* zero = new QPushButton("0");
        zero->setToolTip(QString("Zero %1 at current position").arg(names[i]));
        zero->setMaximumWidth(30);
        zero->setFocusPolicy(Qt::NoFocus);
        QString ax = names[i];
        connect(zero, &QPushButton::clicked, this,
                [this, ax] { mc_->zeroWork(ax); });
        grid->addWidget(lbl, i, 0);
        grid->addWidget(work_[i], i, 1);
        grid->addWidget(mach_[i], i, 2);
        grid->addWidget(zero, i, 3);
    }
    v->addLayout(grid);

    auto* bottom = new QHBoxLayout;
    feedLabel_ = new QLabel("F 0   S 0");
    feedLabel_->setStyleSheet("color:#9aa0a6");
    auto* zeroAll = new QPushButton("Zero XY");
    zeroAll->setFocusPolicy(Qt::NoFocus);
    connect(zeroAll, &QPushButton::clicked, this,
            [this] { mc_->zeroWork("XY"); });
    bottom->addWidget(feedLabel_);
    bottom->addStretch(1);
    bottom->addWidget(zeroAll);
    v->addLayout(bottom);
    return box;
}

QWidget* MainWindow::buildJogBox() {
    auto* box = new QGroupBox("Jog");
    auto* v = new QVBoxLayout(box);

    auto* pad = new QGridLayout;
    pad->setSpacing(4);
    auto jbtn = [&](const QString& t, const QString& axes, int r, int c,
                    const char* col = nullptr) {
        auto* b = new QPushButton(t);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumSize(42, 30);
        if (col) b->setStyleSheet(QString("color:%1;font-weight:bold").arg(col));
        connect(b, &QPushButton::clicked, this,
                [this, axes] { jogAxis(axes); });
        pad->addWidget(b, r, c);
    };
    jbtn("Y+", "Y%1", 0, 1, "#7bc47b");
    jbtn("X-", "X-%1", 1, 0, "#e06c6c");
    jbtn("X+", "X%1", 1, 2, "#e06c6c");
    jbtn("Y-", "Y-%1", 2, 1, "#7bc47b");
    jbtn("Z+", "Z%1", 0, 3, "#6ca6e0");
    jbtn("Z-", "Z-%1", 2, 3, "#6ca6e0");
    // centre: go to work XY zero
    auto* goZero = new QPushButton("⌂");
    goZero->setToolTip("Rapid to work X0 Y0");
    goZero->setFocusPolicy(Qt::NoFocus);
    goZero->setMinimumSize(44, 34);
    goZero->setMinimumSize(42, 30);
    connect(goZero, &QPushButton::clicked, this, [this] {
        mc_->sendCommand("G90");
        mc_->sendCommand("G0 X0 Y0");
    });
    pad->addWidget(goZero, 1, 1);
    v->addLayout(pad);

    // step selector as buttons
    auto* stepRow = new QHBoxLayout;
    stepRow->addWidget(new QLabel("Step"));
    for (double s : {0.01, 0.1, 1.0, 10.0}) {
        auto* b = new QPushButton(QString::number(s));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setProperty("step", s);
        connect(b, &QPushButton::clicked, this, [this, s] { setStep(s); });
        stepBtns_ << b;
        stepRow->addWidget(b);
    }
    v->addLayout(stepRow);

    auto* feedRow = new QHBoxLayout;
    feedRow->addWidget(new QLabel("Feed"));
    jogFeedSpin_ = new QDoubleSpinBox;
    jogFeedSpin_->setRange(1, 5000);
    jogFeedSpin_->setValue(500);
    jogFeedSpin_->setSuffix(" mm/min");
    feedRow->addWidget(jogFeedSpin_, 1);
    v->addLayout(feedRow);

    auto* hint = new QLabel("Arrows · PgUp/PgDn · hold to jog");
    hint->setStyleSheet("color:#70747a;font-size:10px");
    v->addWidget(hint);

    setStep(1.0);
    return box;
}

QWidget* MainWindow::buildJobBox() {
    auto* box = new QGroupBox("Job");
    auto* g = new QGridLayout(box);
    runBtn_ = new QPushButton("▶ Run");
    holdBtn_ = new QPushButton("Hold");
    stopBtn_ = new QPushButton("■ Stop");
    runBtn_->setStyleSheet(
        "background:#2e7d32;font-weight:bold;border-color:#2e7d32");
    stopBtn_->setStyleSheet(
        "background:#c62828;font-weight:bold;border-color:#c62828");
    for (auto* b : {runBtn_, holdBtn_, stopBtn_}) b->setMinimumHeight(34);
    holdBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    jobBar_ = new QProgressBar;
    g->addWidget(runBtn_, 0, 0);
    g->addWidget(holdBtn_, 0, 1);
    g->addWidget(stopBtn_, 0, 2);
    g->addWidget(jobBar_, 1, 0, 1, 3);
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
    return box;
}

void MainWindow::setStep(double mm) {
    curStep_ = mm;
    for (auto* b : stepBtns_)
        b->setChecked(qFuzzyCompare(b->property("step").toDouble(), mm));
}

double MainWindow::jogFeed() const { return jogFeedSpin_->value(); }

void MainWindow::jogAxis(const QString& axesTemplate) {
    if (!mc_->isConnected()) return;
    jogging_ = true;
    mc_->jog(axesTemplate.arg(curStep_), jogFeed());
}

void MainWindow::keyReleaseEvent(QKeyEvent* e) {
    // stop continuous (held-key) jogging cleanly on release
    if (!e->isAutoRepeat() &&
        (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
         e->key() == Qt::Key_Up || e->key() == Qt::Key_Down ||
         e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
        // only a held (auto-repeated) jog gets cancelled; a single tap
        // completes its discrete step
        if (jogHeld_) mc_->jogCancel();
        jogHeld_ = false;
        jogging_ = false;
        e->accept();
        return;
    }
    QMainWindow::keyReleaseEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (const QUrl& url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "gbr" || ext == "gtl" || ext == "gbl" || ext == "pho")
            cam_->loadGerber(path);
        else if (ext == "drl" || ext == "xln")
            cam_->loadDrill(path);
        else if (ext == "stl") {
            if (view3d_->loadStl(path)) viewStack_->setCurrentIndex(1);
        } else  // .nc/.gcode/.ngc/.tap/.txt and anything else: treat as g-code
            openPath(path);
    }
    e->acceptProposedAction();
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (mc_->isConnected() && !cmdEdit_->hasFocus()) {
        QString ax;
        switch (e->key()) {
            case Qt::Key_Left: ax = "X-%1"; break;
            case Qt::Key_Right: ax = "X%1"; break;
            case Qt::Key_Up: ax = "Y%1"; break;
            case Qt::Key_Down: ax = "Y-%1"; break;
            case Qt::Key_PageUp: ax = "Z%1"; break;
            case Qt::Key_PageDown: ax = "Z-%1"; break;
            default: break;
        }
        if (!ax.isEmpty()) {
            if (e->isAutoRepeat()) jogHeld_ = true;  // continuous hold
            jogAxis(ax);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {  // panic stop
            mc_->feedHold();
            e->accept();
            return;
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

void MainWindow::showProgram(const scnc::Program& p) {
    view_->setProgram(p);
    view3d_->setProgram(p);
}

void MainWindow::loadProgramText(const QString& text, const QString& title) {
    programText_ = text;
    program_ = parseGcode(text.toStdString());
    showProgram(program_);
    setWindowTitle("SharkCNC - " + title);
    statusBar()->showMessage(
        QString("%1 lines, %2 motion segments")
            .arg(programText_.count('\n'))
            .arg(program_.segments.size()));
}

void MainWindow::loadStlPath(const QString& p) { view3d_->loadStl(p); }

void MainWindow::forceView3D() { viewStack_->setCurrentIndex(1); view3d_->viewIso(); }

void MainWindow::openStl() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open STL model", s.value("dir/stl").toString(),
        "STL mesh (*.stl);;All files (*)");
    if (fn.isEmpty()) return;
    s.setValue("dir/stl", QFileInfo(fn).absolutePath());
    if (view3d_->loadStl(fn)) {
        viewStack_->setCurrentIndex(1);  // jump to 3D
        statusBar()->showMessage("Loaded " + QFileInfo(fn).fileName() +
                                 " - Tab toggles 2D/3D", 6000);
    } else {
        statusBar()->showMessage("Could not read STL", 5000);
    }
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
    cam_->loadGerber(fn);  // opens in the integrated CAM panel
}

void MainWindow::openDrill() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open Excellon drill", s.value("dir/gerber").toString(),
        "Excellon (*.drl *.xln *.txt);;All files (*)");
    if (fn.isEmpty()) return;
    cam_->loadDrill(fn);
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
