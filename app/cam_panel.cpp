#include "cam_panel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <QComboBox>

#include "cam/excellon.h"
#include "cam/facing.h"
#include "cam/gerber.h"
#include "cam/isolation.h"
#include "cam/outline.h"
#include "tool_library.h"

using namespace scnc;

CamPanel::CamPanel(QWidget* parent) : QWidget(parent) {
    tabs_ = new QTabWidget;
    tabs_->addTab(buildIsoTab(), "Copper (isolation)");
    tabs_->addTab(buildDrillTab(), "Drill");
    tabs_->addTab(buildFaceTab(), "Face");
    tabs_->addTab(buildOutlineTab(), "Outline");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(tabs_);
}

QWidget* CamPanel::buildIsoTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);

    auto* fileRow = new QHBoxLayout;
    gerberPath_ = new QLineEdit;
    gerberPath_->setReadOnly(true);
    gerberPath_->setPlaceholderText("no copper gerber loaded");
    auto* browse = new QPushButton("Open Gerber...");
    fileRow->addWidget(gerberPath_);
    fileRow->addWidget(browse);
    lay->addLayout(fileRow);

    auto* form = new QFormLayout;
    auto mkD = [&](double v, double lo, double hi, double step) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(3);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    // tool picker from the library (drives width for V-bits by depth)
    isoToolPick_ = new QComboBox;
    isoTool_ = mkD(0.2, 0.01, 5, 0.05);
    isoToolNote_ = new QLabel;
    isoToolNote_->setStyleSheet("color:#9aa0a6;font-size:10px");
    isoToolNote_->setWordWrap(true);
    isoPasses_ = new QSpinBox;
    isoPasses_->setRange(1, 12);
    isoOverlap_ = mkD(0.5, 0.0, 0.9, 0.1);
    isoDepth_ = mkD(-0.06, -20, 0, 0.01);
    isoTravel_ = mkD(1.0, 0.1, 30, 0.5);
    isoFeed_ = mkD(120, 1, 2000, 10);
    isoPlunge_ = mkD(60, 1, 1000, 10);
    isoRpm_ = new QSpinBox;
    isoRpm_->setRange(0, 60000);
    isoRpm_->setValue(10000);
    isoFillHoles_ = mkD(2.5, 0.0, 20, 0.5);
    isoFillHoles_->setToolTip(
        "Fill copper voids smaller than this (drill/via holes) so they "
        "aren't milled; larger voids like pour clearances are kept. 0 = "
        "isolate every hole.");
    isoMirror_ = new QCheckBox("Mirror X (bottom copper)");
    form->addRow("Tool", isoToolPick_);
    form->addRow("Width Ø mm", isoTool_);
    form->addRow("", isoToolNote_);
    form->addRow("Passes", isoPasses_);
    form->addRow("Overlap 0..1", isoOverlap_);
    form->addRow("Cut Z mm", isoDepth_);
    form->addRow("Travel Z mm", isoTravel_);
    form->addRow("Feed mm/min", isoFeed_);
    form->addRow("Plunge mm/min", isoPlunge_);
    form->addRow("Spindle RPM", isoRpm_);
    form->addRow("Fill holes < mm", isoFillHoles_);
    form->addRow(isoMirror_);
    lay->addLayout(form);

    isoSummary_ = new QLabel("Load a copper gerber to begin.");
    isoSummary_->setWordWrap(true);
    lay->addWidget(isoSummary_);

    auto* btnRow = new QHBoxLayout;
    isoSendBtn_ = new QPushButton("Load into sender");
    isoSendBtn_->setEnabled(false);
    btnRow->addStretch(1);
    btnRow->addWidget(isoSendBtn_);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    connect(browse, &QPushButton::clicked, this, &CamPanel::browseGerber);
    for (auto* s : {isoTool_, isoOverlap_, isoDepth_, isoTravel_, isoFeed_,
                    isoPlunge_, isoFillHoles_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoPasses_, &QSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoRpm_, &QSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoMirror_, &QCheckBox::toggled, this, &CamPanel::regenIso);
    // picking a tool (or changing depth) recomputes the effective width
    connect(isoToolPick_, &QComboBox::currentIndexChanged, this,
            [this] { applyPickedTool(); });
    connect(isoDepth_, &QDoubleSpinBox::valueChanged, this,
            [this] { applyPickedTool(); });
    connect(&ToolLibraryModel::instance(), &ToolLibraryModel::changed, this,
            [this] { refreshToolPicker(); });
    connect(isoSendBtn_, &QPushButton::clicked, this, [this] {
        if (!isoGcode_.isEmpty())
            emit sendToJob(isoGcode_, gerberName_ + " [isolation]");
    });
    refreshToolPicker();
    return w;
}

void CamPanel::refreshToolPicker() {
    int keepId = isoToolPick_->currentData().toInt();
    QSignalBlocker block(isoToolPick_);
    isoToolPick_->clear();
    isoToolPick_->addItem("Manual width", -1);
    for (const auto& t : ToolLibraryModel::instance().lib().tools())
        isoToolPick_->addItem(QString::fromStdString(t.summary()), t.id);
    int idx = isoToolPick_->findData(keepId);
    isoToolPick_->setCurrentIndex(idx >= 0 ? idx : 0);
    applyPickedTool();
}

void CamPanel::applyPickedTool() {
    int id = isoToolPick_->currentData().toInt();
    const scnc::Tool* t = ToolLibraryModel::instance().lib().find(id);
    if (!t) {  // manual mode
        isoTool_->setEnabled(true);
        isoToolNote_->clear();
        regenIso();
        return;
    }
    double depth = -isoDepth_->value();  // cut depth below surface (positive)
    double w = t->widthAtDepth(depth);
    isoTool_->setEnabled(false);         // width is derived from the tool
    {
        QSignalBlocker b(isoTool_);
        isoTool_->setValue(w);
    }
    if (t->feed > 0) { QSignalBlocker b(isoFeed_); isoFeed_->setValue(t->feed); }
    if (t->plunge > 0) { QSignalBlocker b(isoPlunge_); isoPlunge_->setValue(t->plunge); }
    if (t->rpm > 0) { QSignalBlocker b(isoRpm_); isoRpm_->setValue(t->rpm); }
    if (t->type == scnc::ToolType::VBit || t->type == scnc::ToolType::Chamfer ||
        t->type == scnc::ToolType::Engraver)
        isoToolNote_->setText(
            QString("V-tool: %1 mm wide at %2 mm depth — change depth to "
                    "change isolation width")
                .arg(w, 0, 'f', 3)
                .arg(depth, 0, 'f', 2));
    else
        isoToolNote_->setText(QString("flat width %1 mm").arg(w, 0, 'f', 3));
    regenIso();
}

QWidget* CamPanel::buildDrillTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);

    auto* fileRow = new QHBoxLayout;
    drillPath_ = new QLineEdit;
    drillPath_->setReadOnly(true);
    drillPath_->setPlaceholderText("no drill file loaded");
    auto* browse = new QPushButton("Open Excellon...");
    fileRow->addWidget(drillPath_);
    fileRow->addWidget(browse);
    lay->addLayout(fileRow);

    auto* form = new QFormLayout;
    auto mkD = [&](double v, double lo, double hi, double step) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(3);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    drillDepth_ = mkD(-1.8, -20, 0, 0.1);
    drillTravel_ = mkD(2.0, 0.1, 30, 0.5);
    drillPlunge_ = mkD(90, 1, 1000, 10);
    drillRpm_ = new QSpinBox;
    drillRpm_->setRange(0, 60000);
    drillRpm_->setValue(10000);
    drillSingle_ = new QCheckBox("Single tool (no toolchange pauses)");
    drillSingle_->setChecked(true);
    drillMirror_ = new QCheckBox("Mirror X (bottom side)");
    form->addRow(drillSingle_);
    form->addRow("Cut Z mm", drillDepth_);
    form->addRow("Travel Z mm", drillTravel_);
    form->addRow("Plunge mm/min", drillPlunge_);
    form->addRow("Spindle RPM", drillRpm_);
    form->addRow(drillMirror_);
    lay->addLayout(form);

    drillSummary_ = new QLabel("Load an Excellon drill file to begin.");
    drillSummary_->setWordWrap(true);
    lay->addWidget(drillSummary_);

    auto* btnRow = new QHBoxLayout;
    drillSendBtn_ = new QPushButton("Load into sender");
    drillSendBtn_->setEnabled(false);
    btnRow->addStretch(1);
    btnRow->addWidget(drillSendBtn_);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    connect(browse, &QPushButton::clicked, this, &CamPanel::browseDrill);
    for (auto* s : {drillDepth_, drillTravel_, drillPlunge_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CamPanel::regenDrill);
    connect(drillRpm_, &QSpinBox::valueChanged, this, &CamPanel::regenDrill);
    connect(drillSingle_, &QCheckBox::toggled, this, &CamPanel::regenDrill);
    connect(drillMirror_, &QCheckBox::toggled, this, &CamPanel::regenDrill);
    connect(drillSendBtn_, &QPushButton::clicked, this, [this] {
        if (!drillGcode_.isEmpty())
            emit sendToJob(drillGcode_, drillName_ + " [drill]");
    });
    return w;
}

QWidget* CamPanel::buildFaceTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    auto* form = new QFormLayout;
    auto mkD = [&](double v, double lo, double hi, double step) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(2);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    faceX_ = mkD(0, -1000, 1000, 1);
    faceY_ = mkD(0, -1000, 1000, 1);
    faceW_ = mkD(50, 1, 1000, 1);
    faceH_ = mkD(50, 1, 1000, 1);
    faceTool_ = mkD(6, 0.5, 50, 0.5);
    faceStepover_ = mkD(0.4, 0.05, 0.95, 0.05);
    faceDepth_ = mkD(0.2, 0.01, 20, 0.05);
    facePass_ = mkD(0.2, 0.01, 10, 0.05);
    faceFeed_ = mkD(800, 1, 5000, 50);
    facePlunge_ = mkD(300, 1, 2000, 50);
    faceTravel_ = mkD(3, 0.1, 30, 0.5);
    faceRpm_ = new QSpinBox;
    faceRpm_->setRange(0, 60000);
    faceRpm_->setValue(12000);
    faceSpiral_ = new QCheckBox("Spiral (else raster zig-zag)");

    auto* xyRow = new QHBoxLayout;
    xyRow->addWidget(new QLabel("X0"));
    xyRow->addWidget(faceX_);
    xyRow->addWidget(new QLabel("Y0"));
    xyRow->addWidget(faceY_);
    auto* whRow = new QHBoxLayout;
    whRow->addWidget(new QLabel("W"));
    whRow->addWidget(faceW_);
    whRow->addWidget(new QLabel("H"));
    whRow->addWidget(faceH_);
    form->addRow("Origin", xyRow);
    form->addRow("Size mm", whRow);
    form->addRow("Tool Ø mm", faceTool_);
    form->addRow("Stepover 0..1", faceStepover_);
    form->addRow("Total depth mm", faceDepth_);
    form->addRow("Per pass mm", facePass_);
    form->addRow("Feed mm/min", faceFeed_);
    form->addRow("Plunge mm/min", facePlunge_);
    form->addRow("Travel Z mm", faceTravel_);
    form->addRow("Spindle RPM", faceRpm_);
    form->addRow(faceSpiral_);
    lay->addLayout(form);

    faceSummary_ = new QLabel("Set an area to flatten.");
    faceSummary_->setWordWrap(true);
    lay->addWidget(faceSummary_);
    auto* btnRow = new QHBoxLayout;
    faceSendBtn_ = new QPushButton("Load into sender");
    btnRow->addStretch(1);
    btnRow->addWidget(faceSendBtn_);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    for (auto* s : {faceX_, faceY_, faceW_, faceH_, faceTool_, faceStepover_,
                    faceDepth_, facePass_, faceFeed_, facePlunge_, faceTravel_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CamPanel::regenFace);
    connect(faceRpm_, &QSpinBox::valueChanged, this, &CamPanel::regenFace);
    connect(faceSpiral_, &QCheckBox::toggled, this, &CamPanel::regenFace);
    connect(faceSendBtn_, &QPushButton::clicked, this, [this] {
        if (!faceGcode_.isEmpty()) emit sendToJob(faceGcode_, "facing");
    });
    regenFace();
    return w;
}

QWidget* CamPanel::buildOutlineTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    auto* form = new QFormLayout;
    auto mkD = [&](double v, double lo, double hi, double step) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(2);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    outX_ = mkD(0, -1000, 1000, 1);
    outY_ = mkD(0, -1000, 1000, 1);
    outW_ = mkD(50, 1, 1000, 1);
    outH_ = mkD(50, 1, 1000, 1);
    outTool_ = mkD(1.0, 0.1, 10, 0.1);
    outDepth_ = mkD(-1.8, -30, 0, 0.1);
    outPass_ = mkD(0.4, 0.05, 10, 0.05);
    outFeed_ = mkD(300, 1, 3000, 10);
    outPlunge_ = mkD(120, 1, 1000, 10);
    outTabW_ = mkD(3.0, 0.5, 20, 0.5);
    outTabH_ = mkD(0.5, 0.1, 5, 0.1);
    outRpm_ = new QSpinBox;
    outRpm_->setRange(0, 60000);
    outRpm_->setValue(12000);
    outTabs_ = new QSpinBox;
    outTabs_->setRange(0, 24);
    outTabs_->setValue(4);
    outInside_ = new QCheckBox("Cut inside the line (slot / inner cut-out)");

    auto* xyRow = new QHBoxLayout;
    xyRow->addWidget(new QLabel("X0"));
    xyRow->addWidget(outX_);
    xyRow->addWidget(new QLabel("Y0"));
    xyRow->addWidget(outY_);
    auto* whRow = new QHBoxLayout;
    whRow->addWidget(new QLabel("W"));
    whRow->addWidget(outW_);
    whRow->addWidget(new QLabel("H"));
    whRow->addWidget(outH_);
    form->addRow("Origin", xyRow);
    form->addRow("Size mm", whRow);
    form->addRow("Tool Ø mm", outTool_);
    form->addRow("Cut depth mm", outDepth_);
    form->addRow("Per pass mm", outPass_);
    form->addRow("Feed mm/min", outFeed_);
    form->addRow("Plunge mm/min", outPlunge_);
    form->addRow("Tabs", outTabs_);
    form->addRow("Tab width mm", outTabW_);
    form->addRow("Tab height mm", outTabH_);
    form->addRow("Spindle RPM", outRpm_);
    form->addRow(outInside_);
    lay->addLayout(form);

    outSummary_ = new QLabel("Set the board size to cut out.");
    outSummary_->setWordWrap(true);
    lay->addWidget(outSummary_);
    auto* btnRow = new QHBoxLayout;
    outSendBtn_ = new QPushButton("Load into sender");
    btnRow->addStretch(1);
    btnRow->addWidget(outSendBtn_);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    for (auto* s : {outX_, outY_, outW_, outH_, outTool_, outDepth_, outPass_,
                    outFeed_, outPlunge_, outTabW_, outTabH_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CamPanel::regenOutline);
    connect(outRpm_, &QSpinBox::valueChanged, this, &CamPanel::regenOutline);
    connect(outTabs_, &QSpinBox::valueChanged, this, &CamPanel::regenOutline);
    connect(outInside_, &QCheckBox::toggled, this, &CamPanel::regenOutline);
    connect(outSendBtn_, &QPushButton::clicked, this, [this] {
        if (!outGcode_.isEmpty()) emit sendToJob(outGcode_, "outline");
    });
    regenOutline();
    return w;
}

void CamPanel::regenOutline() {
    scnc::OutlineOptions o;
    o.toolDiameter = outTool_->value();
    o.cutZ = outDepth_->value();
    o.depthPerPass = outPass_->value();
    o.feed = outFeed_->value();
    o.plunge = outPlunge_->value();
    o.spindleRpm = outRpm_->value();
    o.tabs = outTabs_->value();
    o.tabWidth = outTabW_->value();
    o.tabHeight = outTabH_->value();
    o.outside = !outInside_->isChecked();
    auto rect = scnc::rectBoundary(outX_->value(), outY_->value(),
                                   outW_->value(), outH_->value());
    auto r = scnc::outlineRoutine(rect, o);
    if (!r.ok) {
        outSummary_->setText("Error: " + QString::fromStdString(r.error));
        outGcode_.clear();
        outSendBtn_->setEnabled(false);
        return;
    }
    outGcode_ = QString::fromStdString(r.gcode);
    outSendBtn_->setEnabled(true);
    outSummary_->setText(QString("%1 pass(es), %2 tabs")
                             .arg(r.passes)
                             .arg(o.tabs));
    emit previewReady(Clipper2Lib::PathsD{}, outGcode_, "outline");
}

void CamPanel::setFacingArea(double x0, double y0, double ww, double hh) {
    faceX_->setValue(x0);
    faceY_->setValue(y0);
    faceW_->setValue(ww);
    faceH_->setValue(hh);
    tabs_->setCurrentIndex(2);
}

void CamPanel::regenFace() {
    scnc::FacingOptions o;
    o.x0 = faceX_->value();
    o.y0 = faceY_->value();
    o.width = faceW_->value();
    o.height = faceH_->value();
    o.toolDiameter = faceTool_->value();
    o.stepover = faceStepover_->value();
    o.totalDepth = faceDepth_->value();
    o.depthPerPass = facePass_->value();
    o.feed = faceFeed_->value();
    o.plunge = facePlunge_->value();
    o.travelZ = faceTravel_->value();
    o.spindleRpm = faceRpm_->value();
    o.spiral = faceSpiral_->isChecked();
    auto r = scnc::facingRoutine(o);
    if (!r.ok) {
        faceSummary_->setText("Error: " + QString::fromStdString(r.error));
        faceGcode_.clear();
        faceSendBtn_->setEnabled(false);
        return;
    }
    faceGcode_ = QString::fromStdString(r.gcode);
    faceSendBtn_->setEnabled(true);
    faceSummary_->setText(QString("%1 pass(es), %2 mm of cutting")
                              .arg(r.passes)
                              .arg(r.lengthMm, 0, 'f', 0));
    emit previewReady(Clipper2Lib::PathsD{}, faceGcode_, "facing");
}

void CamPanel::browseGerber() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open copper Gerber", s.value("dir/gerber").toString(),
        "Gerber (*.gbr *.gtl *.gbl *.g *.pho);;All files (*)");
    if (!fn.isEmpty()) loadGerber(fn);
}

void CamPanel::browseDrill() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(
        this, "Open Excellon drill", s.value("dir/gerber").toString(),
        "Excellon (*.drl *.xln *.txt);;All files (*)");
    if (!fn.isEmpty()) loadDrill(fn);
}

void CamPanel::loadGerber(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    gerberText_ = QString::fromUtf8(f.readAll());
    gerberName_ = QFileInfo(path).fileName();
    gerberPath_->setText(gerberName_);
    QSettings().setValue("dir/gerber", QFileInfo(path).absolutePath());
    tabs_->setCurrentIndex(0);
    regenIso();
}

void CamPanel::loadDrill(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    drillText_ = QString::fromUtf8(f.readAll());
    drillName_ = QFileInfo(path).fileName();
    drillPath_->setText(drillName_);
    QSettings().setValue("dir/gerber", QFileInfo(path).absolutePath());
    tabs_->setCurrentIndex(1);
    regenDrill();
}

void CamPanel::regenIso() {
    if (gerberText_.isEmpty()) return;
    auto g = parseGerber(gerberText_.toStdString());
    if (!g.ok) {
        isoSummary_->setText("Gerber error: " +
                             QString::fromStdString(g.error));
        isoGcode_.clear();
        isoSendBtn_->setEnabled(false);
        return;
    }
    copper_ = g.layer.copper;
    IsolationOptions opt;
    opt.toolDiameter = isoTool_->value();
    opt.passes = isoPasses_->value();
    opt.overlap = isoOverlap_->value();
    opt.cutZ = isoDepth_->value();
    opt.travelZ = isoTravel_->value();
    opt.feed = isoFeed_->value();
    opt.plunge = isoPlunge_->value();
    opt.spindleRpm = isoRpm_->value();
    opt.mirrorX = isoMirror_->isChecked();
    opt.fillHolesBelow = isoFillHoles_->value();
    auto iso = isolationRoute(g.layer, opt);
    if (!iso.ok) {
        isoSummary_->setText("Isolation error: " +
                             QString::fromStdString(iso.error));
        isoGcode_.clear();
        isoSendBtn_->setEnabled(false);
        return;
    }
    isoGcode_ = QString::fromStdString(iso.gcode);
    isoSendBtn_->setEnabled(true);
    QString warn;
    for (auto& wtxt : g.layer.warnings)
        warn += "\n⚠ " + QString::fromStdString(wtxt);
    isoSummary_->setText(QString("%1 toolpaths, %2 mm cutting, board %3×%4 mm%5")
                             .arg(iso.toolpaths.size())
                             .arg(iso.lengthMm, 0, 'f', 0)
                             .arg(g.layer.maxX - g.layer.minX, 0, 'f', 1)
                             .arg(g.layer.maxY - g.layer.minY, 0, 'f', 1)
                             .arg(warn));

    // show the cleaned copper (merged, drill holes filled) so the overlay
    // matches exactly what the toolpaths isolate
    emit previewReady(iso.cleanedCopper, isoGcode_,
                      gerberName_ + " [isolation]");
}

void CamPanel::regenDrill() {
    if (drillText_.isEmpty()) return;
    auto d = parseExcellon(drillText_.toStdString());
    DrillOptions opt;
    opt.cutZ = drillDepth_->value();
    opt.travelZ = drillTravel_->value();
    opt.plunge = drillPlunge_->value();
    opt.spindleRpm = drillRpm_->value();
    opt.singleTool = drillSingle_->isChecked();
    opt.mirrorX = drillMirror_->isChecked();
    auto out = drillGcode(d, opt);
    if (!out.ok) {
        drillSummary_->setText("Drill error: " +
                               QString::fromStdString(out.error));
        drillGcode_.clear();
        drillSendBtn_->setEnabled(false);
        return;
    }
    drillGcode_ = QString::fromStdString(out.gcode);
    drillSendBtn_->setEnabled(true);
    QString tools;
    for (auto& t : d.tools)
        tools += QString("\n  T%1  %2 mm  ×%3")
                     .arg(t.number)
                     .arg(t.diameter, 0, 'f', 2)
                     .arg(t.count);
    drillSummary_->setText(QString("%1 holes%2").arg(out.holes).arg(tools));
    emit previewReady(Clipper2Lib::PathsD{}, drillGcode_,
                      drillName_ + " [drill]");
}
