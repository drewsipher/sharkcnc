#include "cam_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "cam/excellon.h"
#include "cam/gerber.h"
#include "cam/isolation.h"

using namespace scnc;

CamDialog::CamDialog(Mode mode, const QString& inputPath, QWidget* parent)
    : QDialog(parent), mode_(mode) {
    setWindowTitle(mode == Mode::Isolation ? "Isolation routing"
                                           : "Drill G-code");
    QFile f(inputPath);
    f.open(QIODevice::ReadOnly);
    inputText_ = QString::fromUtf8(f.readAll());

    auto* form = new QFormLayout;
    auto mkD = [&](double v, double lo, double hi, double step) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(3);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    tool_ = mkD(0.2, 0.01, 5, 0.05);
    passes_ = new QSpinBox;
    passes_->setRange(1, 10);
    overlap_ = mkD(0.5, 0.0, 0.9, 0.1);
    depth_ = mkD(mode == Mode::Isolation ? -0.06 : -1.8, -20, 0, 0.01);
    travel_ = mkD(mode == Mode::Isolation ? 1.0 : 2.0, 0.1, 30, 0.5);
    feed_ = mkD(120, 1, 2000, 10);
    plunge_ = mkD(mode == Mode::Isolation ? 60 : 90, 1, 1000, 10);
    rpm_ = new QSpinBox;
    rpm_->setRange(0, 60000);
    rpm_->setValue(10000);
    mirror_ = new QCheckBox("Mirror X (bottom layer)");
    singleTool_ = new QCheckBox("Single tool (no toolchange pauses)");
    singleTool_->setChecked(true);

    if (mode == Mode::Isolation) {
        form->addRow("Tool diameter mm", tool_);
        form->addRow("Passes", passes_);
        form->addRow("Overlap 0..1", overlap_);
        form->addRow("Cut feed mm/min", feed_);
    } else {
        form->addRow(singleTool_);
    }
    form->addRow("Cut Z mm", depth_);
    form->addRow("Travel Z mm", travel_);
    form->addRow("Plunge mm/min", plunge_);
    form->addRow("Spindle RPM", rpm_);
    form->addRow(mirror_);

    summary_ = new QLabel("...");
    summary_->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Load into sender");

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(summary_);
    lay->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto regen = [this] { regenerate(); };
    for (auto* s : {tool_, overlap_, depth_, travel_, feed_, plunge_})
        connect(s, &QDoubleSpinBox::valueChanged, this, regen);
    connect(passes_, &QSpinBox::valueChanged, this, regen);
    connect(rpm_, &QSpinBox::valueChanged, this, regen);
    connect(mirror_, &QCheckBox::toggled, this, regen);
    connect(singleTool_, &QCheckBox::toggled, this, regen);
    regenerate();
}

void CamDialog::regenerate() {
    if (mode_ == Mode::Isolation) {
        auto g = parseGerber(inputText_.toStdString());
        if (!g.ok) {
            summary_->setText("Gerber error: " +
                              QString::fromStdString(g.error));
            gcode_.clear();
            return;
        }
        IsolationOptions opt;
        opt.toolDiameter = tool_->value();
        opt.passes = passes_->value();
        opt.overlap = overlap_->value();
        opt.cutZ = depth_->value();
        opt.travelZ = travel_->value();
        opt.feed = feed_->value();
        opt.plunge = plunge_->value();
        opt.spindleRpm = rpm_->value();
        opt.mirrorX = mirror_->isChecked();
        auto iso = isolationRoute(g.layer, opt);
        if (!iso.ok) {
            summary_->setText("Isolation error: " +
                              QString::fromStdString(iso.error));
            gcode_.clear();
            return;
        }
        gcode_ = QString::fromStdString(iso.gcode);
        QString warn;
        for (auto& w : g.layer.warnings)
            warn += "\nwarning: " + QString::fromStdString(w);
        summary_->setText(QString("%1 toolpaths, %2 mm of cutting, board "
                                  "%3 x %4 mm%5")
                              .arg(iso.toolpaths.size())
                              .arg(iso.lengthMm, 0, 'f', 0)
                              .arg(g.layer.maxX - g.layer.minX, 0, 'f', 1)
                              .arg(g.layer.maxY - g.layer.minY, 0, 'f', 1)
                              .arg(warn));
    } else {
        auto d = parseExcellon(inputText_.toStdString());
        DrillOptions opt;
        opt.cutZ = depth_->value();
        opt.travelZ = travel_->value();
        opt.plunge = plunge_->value();
        opt.spindleRpm = rpm_->value();
        opt.singleTool = singleTool_->isChecked();
        opt.mirrorX = mirror_->isChecked();
        auto out = drillGcode(d, opt);
        if (!out.ok) {
            summary_->setText("Drill error: " +
                              QString::fromStdString(out.error));
            gcode_.clear();
            return;
        }
        gcode_ = QString::fromStdString(out.gcode);
        QString tools;
        for (auto& t : d.tools)
            tools += QString("\n  T%1 %2 mm x%3")
                         .arg(t.number)
                         .arg(t.diameter, 0, 'f', 2)
                         .arg(t.count);
        summary_->setText(
            QString("%1 holes%2").arg(out.holes).arg(tools));
    }
}
