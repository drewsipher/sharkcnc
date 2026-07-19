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

#include "cam/excellon.h"
#include "cam/gerber.h"
#include "cam/isolation.h"

using namespace scnc;

CamPanel::CamPanel(QWidget* parent) : QWidget(parent) {
    tabs_ = new QTabWidget;
    tabs_->addTab(buildIsoTab(), "Copper (isolation)");
    tabs_->addTab(buildDrillTab(), "Drill");
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
    isoTool_ = mkD(0.2, 0.01, 5, 0.05);
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
    isoMirror_ = new QCheckBox("Mirror X (bottom copper)");
    form->addRow("Tool Ø mm", isoTool_);
    form->addRow("Passes", isoPasses_);
    form->addRow("Overlap 0..1", isoOverlap_);
    form->addRow("Cut Z mm", isoDepth_);
    form->addRow("Travel Z mm", isoTravel_);
    form->addRow("Feed mm/min", isoFeed_);
    form->addRow("Plunge mm/min", isoPlunge_);
    form->addRow("Spindle RPM", isoRpm_);
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
                    isoPlunge_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoPasses_, &QSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoRpm_, &QSpinBox::valueChanged, this, &CamPanel::regenIso);
    connect(isoMirror_, &QCheckBox::toggled, this, &CamPanel::regenIso);
    connect(isoSendBtn_, &QPushButton::clicked, this, [this] {
        if (!isoGcode_.isEmpty())
            emit sendToJob(isoGcode_, gerberName_ + " [isolation]");
    });
    return w;
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

    // when mirrored, show copper mirrored too so the preview matches
    Clipper2Lib::PathsD shownCopper = copper_;
    if (opt.mirrorX)
        for (auto& p : shownCopper)
            for (auto& pt : p) pt.x = -pt.x;
    emit previewReady(shownCopper, isoGcode_, gerberName_ + " [isolation]");
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
