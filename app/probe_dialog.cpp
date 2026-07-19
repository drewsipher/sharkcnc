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
    x0_ = mk(minX, -1000, 1000);
    y0_ = mk(minY, -1000, 1000);
    w_ = mk(std::max(1.0, maxX - minX), 1, 1000);
    h_ = mk(std::max(1.0, maxY - minY), 1, 1000);
    spacing_ = mk(7.5, 1, 100);
    clearZ_ = mk(2.0, 0.1, 50, 0.5);
    probeZ_ = mk(-2.0, -20, 0, 0.5);
    feed_ = mk(40, 1, 500, 10);
    form->addRow("Origin X (work)", x0_);
    form->addRow("Origin Y (work)", y0_);
    form->addRow("Width mm", w_);
    form->addRow("Height mm", h_);
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
    applyBtn_ = new QPushButton("Apply to loaded G-code");
    saveBtn_->setEnabled(false);
    applyBtn_->setEnabled(false);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(info_);
    lay->addWidget(progress_);
    lay->addWidget(startBtn_);
    lay->addWidget(saveBtn_);
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
    connect(applyBtn_, &QPushButton::clicked, this, &QDialog::accept);
}

void ProbeDialog::startProbing() {
    if (!mc_->isConnected()) {
        QMessageBox::warning(this, "Not connected",
                             "Connect to the machine first.");
        return;
    }
    int nx = std::max(2, static_cast<int>(std::ceil(w_->value() /
                                                    spacing_->value())) + 1);
    int ny = std::max(2, static_cast<int>(std::ceil(h_->value() /
                                                    spacing_->value())) + 1);
    map_ = HeightMap(x0_->value(), y0_->value(),
                     w_->value() / (nx - 1), h_->value() / (ny - 1), nx, ny);
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
