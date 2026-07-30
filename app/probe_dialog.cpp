#include "probe_dialog.h"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <fstream>

#include "gcode/parser.h"
#include "gcode_view3d.h"
#include "heightmap_dialog.h"
#include "machine_client.h"

using namespace scnc;

ProbeDialog::ProbeDialog(MachineClient* mc, double minX, double minY,
                         double maxX, double maxY, QWidget* parent)
    : QDialog(parent), mc_(mc) {
    setWindowTitle("Height map probing");
    auto* form = new QFormLayout;
    auto mk = [&](double v, double lo, double hi, double step = 1.0) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(2);
        s->setSingleStep(step);
        s->setValue(v);
        return s;
    };
    // Corner-to-corner: any origin convention works (e.g. 0,0 at the
    // back-left corner with the board extending in -Y). The grid covers
    // the rectangle between the two corners, in any quadrant, either
    // corner first.
    x0_ = mk(minX, -1000, 1000);
    y0_ = mk(minY, -1000, 1000);
    x1_ = mk(std::max(minX + 1.0, maxX), -1000, 1000);
    y1_ = mk(std::max(minY + 1.0, maxY), -1000, 1000);
    spacing_ = mk(7.5, 1, 100);
    clearZ_ = mk(2.0, 0.1, 50, 0.5);
    probeZ_ = mk(-2.0, -20, 0, 0.5);
    feed_ = mk(40, 1, 500, 10);
    form->addRow("Corner 1 X (work)", x0_);
    form->addRow("Corner 1 Y (work)", y0_);
    form->addRow("Corner 2 X (work)", x1_);
    form->addRow("Corner 2 Y (work)", y1_);
    form->addRow("Grid spacing mm", spacing_);
    form->addRow("Clearance Z", clearZ_);
    form->addRow("Probe target Z", probeZ_);
    form->addRow("Probe feed mm/min", feed_);

    progress_ = new QProgressBar;
    info_ = new QLabel(
        "Zero X/Y at board origin and Z at board surface first.\n"
        "Probing uses work coordinates.");
    info_->setWordWrap(true);
    startBtn_ = new QPushButton("Start probing");
    saveBtn_ = new QPushButton("Save map...");
    viewBtn_ = new QPushButton("View in 3D...");
    applyBtn_ = new QPushButton("Apply to loaded G-code");
    saveBtn_->setEnabled(false);
    viewBtn_->setEnabled(false);
    applyBtn_->setEnabled(false);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(info_);
    lay->addWidget(progress_);
    lay->addWidget(startBtn_);
    lay->addWidget(saveBtn_);
    lay->addWidget(viewBtn_);
    lay->addWidget(applyBtn_);

    connect(startBtn_, &QPushButton::clicked, this,
            &ProbeDialog::startProbing);
    connect(mc_, &MachineClient::probeFinished, this, &ProbeDialog::onProbe);
    connect(saveBtn_, &QPushButton::clicked, this, [this] {
        QString fn = QFileDialog::getSaveFileName(this, "Save height map",
                                                  "board.heightmap.json",
                                                  "Height map (*.json)");
        if (fn.isEmpty()) return;
        std::ofstream f(fn.toStdString());
        f << map_.toJson();
    });
    connect(viewBtn_, &QPushButton::clicked, this, [this] {
        if (!openglAvailable()) {
            QMessageBox::warning(this, "Height map",
                                 "No OpenGL context available.");
            return;
        }
        auto* dlg = new HeightmapDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setMap(map_);
        dlg->show();
    });
    connect(applyBtn_, &QPushButton::clicked, this, &QDialog::accept);
}

void ProbeDialog::startProbing() {
    if (!mc_->isConnected()) {
        QMessageBox::warning(this, "Not connected",
                             "Connect to the machine first.");
        return;
    }
    const double gx0 = std::min(x0_->value(), x1_->value());
    const double gy0 = std::min(y0_->value(), y1_->value());
    const double gw = std::abs(x1_->value() - x0_->value());
    const double gh = std::abs(y1_->value() - y0_->value());
    if (gw < 0.5 || gh < 0.5) {
        QMessageBox::warning(this, "Probe area",
                             "The two corners span less than 0.5 mm - "
                             "check the corner coordinates.");
        return;
    }
    int nx = std::max(2, static_cast<int>(std::ceil(gw /
                                                    spacing_->value())) + 1);
    int ny = std::max(2, static_cast<int>(std::ceil(gh /
                                                    spacing_->value())) + 1);
    map_ = HeightMap(gx0, gy0, gw / (nx - 1), gh / (ny - 1), nx, ny);
    points_ = map_.probeOrder();
    idx_ = 0;
    running_ = true;
    mapDone_ = false;
    progress_->setRange(0, static_cast<int>(points_.size()));
    progress_->setValue(0);
    startBtn_->setEnabled(false);
    nextPoint();
}

void ProbeDialog::nextPoint() {
    if (idx_ >= points_.size()) {
        running_ = false;
        mapDone_ = true;
        saveBtn_->setEnabled(true);
        viewBtn_->setEnabled(true);
        applyBtn_->setEnabled(true);
        startBtn_->setEnabled(true);
        info_->setText("Probing complete.");
        mc_->sendCommand(
            QString("G0 Z%1").arg(QString::fromStdString(
                fmtNum(clearZ_->value()))));
        return;
    }
    const auto& pt = points_[idx_];
    mc_->sendCommand(QString("G90"));
    mc_->sendCommand(QString("G0 Z%1").arg(clearZ_->value()));
    mc_->sendCommand(
        QString("G0 X%1 Y%2").arg(pt.x).arg(pt.y));
    mc_->probe(QString("G38.2 Z%1 F%2")
                   .arg(probeZ_->value())
                   .arg(feed_->value()));
}

void ProbeDialog::onProbe(bool ok, double, double, double z) {
    if (!running_) return;
    if (!ok) {
        running_ = false;
        startBtn_->setEnabled(true);
        info_->setText("Probe failed - check probe wiring and Z range.");
        return;
    }
    const auto& pt = points_[idx_];
    map_.set(pt.ix, pt.iy, z);
    ++idx_;
    progress_->setValue(static_cast<int>(idx_));
    nextPoint();
}
