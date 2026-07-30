#include "stock_panel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "machine_client.h"

namespace {
void pushLine(std::vector<float>& v, double x1, double y1, double z1,
              double x2, double y2, double z2) {
    v.insert(v.end(), {float(x1), float(y1), float(z1), float(x2), float(y2),
                       float(z2)});
}
}  // namespace

StockPanel::StockPanel(MachineClient* mc, QWidget* parent)
    : QWidget(parent), mc_(mc) {
    auto* lay = new QVBoxLayout(this);
    auto mkD = [](double v, double lo, double hi, int dec = 2) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setValue(v);
        return s;
    };

    auto* est = new QGroupBox("Estimated stock (work coordinates)");
    auto* ef = new QFormLayout(est);
    cx_ = mkD(0, -1000, 1000);
    cy_ = mkD(0, -1000, 1000);
    sx_ = mkD(50, -1000, 1000);
    sy_ = mkD(-80, -1000, 1000);
    topZ_ = mkD(0, -100, 100);
    thick_ = mkD(10, 0.1, 200);
    ef->addRow("Corner X mm", cx_);
    ef->addRow("Corner Y mm", cy_);
    ef->addRow("Size X mm (+/-)", sx_);
    ef->addRow("Size Y mm (+/-)", sy_);
    ef->addRow("Top Z mm", topZ_);
    ef->addRow("Thickness mm", thick_);
    lay->addWidget(est);

    auto* pr = new QGroupBox("Probe the real top");
    auto* pf = new QFormLayout(pr);
    nx_ = new QSpinBox;
    nx_->setRange(2, 6);
    nx_->setValue(3);
    ny_ = new QSpinBox;
    ny_->setRange(2, 6);
    ny_->setValue(3);
    margin_ = mkD(5, 0, 50);
    clearZ_ = mkD(5, 1, 50);
    puck_ = new QCheckBox(
        "Non-conductive stock: pause each point to place the puck\n"
        "(puck thickness from Probe settings is subtracted)");
    puck_->setChecked(true);
    pf->addRow("Points X", nx_);
    pf->addRow("Points Y", ny_);
    pf->addRow("Edge margin mm", margin_);
    pf->addRow("Clearance above top mm", clearZ_);
    pf->addRow(puck_);
    lay->addWidget(pr);

    probeBtn_ = new QPushButton("Probe stock top");
    showBtn_ = new QPushButton("Show in 3D");
    showBtn_->setEnabled(false);
    info_ = new QLabel("Zero your work coordinates first; the grid is "
                       "computed from the estimated stock above.");
    info_->setWordWrap(true);
    stats_ = new QLabel;
    stats_->setWordWrap(true);
    lay->addWidget(info_);
    lay->addWidget(probeBtn_);
    lay->addWidget(showBtn_);
    lay->addWidget(stats_);
    lay->addStretch(1);

    connect(probeBtn_, &QPushButton::clicked, this, &StockPanel::startProbing);
    connect(showBtn_, &QPushButton::clicked, this, &StockPanel::showOverlay);
    connect(mc_, &MachineClient::probeFinished, this, &StockPanel::onProbe);

    // dev/demo hook: synthesize a measured surface so the 3D overlay can
    // be exercised without a machine (SHARKCNC_STOCK_DEMO=1)
    if (qEnvironmentVariableIsSet("SHARKCNC_STOCK_DEMO")) {
        gridNx_ = 3;
        gridNy_ = 3;
        pts_.clear();
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                int ii = (j % 2) ? 2 - i : i;   // stored serpentine,
                Pt p;                            // like a real run
                p.x = cx_->value() + 5 + ii * 20;
                p.y = cy_->value() + (sy_->value() < 0 ? -5.0 - j * 35
                                                       : 5.0 + j * 35);
                p.z = topZ_->value() + 0.4 - 0.25 * ii - 0.15 * j;
                p.done = true;
                pts_.push_back(p);
            }
        haveData_ = true;
        showBtn_->setEnabled(true);
        updateStats();
        QTimer::singleShot(200, this, &StockPanel::showOverlay);
    }
}

void StockPanel::startProbing() {
    if (!mc_->isConnected()) {
        QMessageBox::warning(this, "Stock probing",
                             "Connect to the machine first.");
        return;
    }
    QSettings s;
    puckZ_ = puck_->isChecked() ? s.value("probe/puckZ", 0.0).toDouble() : 0.0;
    if (puck_->isChecked() && puckZ_ <= 0) {
        QMessageBox::warning(
            this, "Stock probing",
            "Puck mode is on but Probe settings has no puck thickness -\n"
            "set it in Probe > Probe settings first.");
        return;
    }
    gridNx_ = nx_->value();
    gridNy_ = ny_->value();
    const double m = margin_->value();
    const double x0 = cx_->value() + (sx_->value() >= 0 ? m : -m);
    const double y0 = cy_->value() + (sy_->value() >= 0 ? m : -m);
    const double spanX = sx_->value() - (sx_->value() >= 0 ? 2 * m : -2 * m);
    const double spanY = sy_->value() - (sy_->value() >= 0 ? 2 * m : -2 * m);
    pts_.clear();
    for (int j = 0; j < gridNy_; ++j) {
        for (int i = 0; i < gridNx_; ++i) {
            int ii = (j % 2) ? gridNx_ - 1 - i : i;   // serpentine
            Pt p;
            p.x = x0 + spanX * ii / (gridNx_ - 1);
            p.y = y0 + spanY * j / (gridNy_ - 1);
            pts_.push_back(p);
        }
    }
    idx_ = 0;
    running_ = true;
    haveData_ = false;
    probeBtn_->setEnabled(false);
    nextPoint();
}

void StockPanel::nextPoint() {
    if (idx_ >= pts_.size()) {
        finishRun(true);
        return;
    }
    const Pt& p = pts_[idx_];
    QSettings s;
    const double safe = topZ_->value() + puckZ_ + clearZ_->value();
    mc_->sendCommand("G90");
    mc_->sendCommand(QString("G0 Z%1").arg(safe));
    mc_->sendCommand(QString("G0 X%1 Y%2").arg(p.x).arg(p.y));
    if (puck_->isChecked()) {
        info_->setText(QString("Point %1/%2 at X%3 Y%4")
                           .arg(idx_ + 1)
                           .arg(pts_.size())
                           .arg(p.x, 0, 'f', 1)
                           .arg(p.y, 0, 'f', 1));
        auto btn = QMessageBox::information(
            this, "Place the puck",
            QString("Point %1 of %2\n\nPlace the puck on the stock under "
                    "the probe, then OK to probe.")
                .arg(idx_ + 1)
                .arg(pts_.size()),
            QMessageBox::Ok | QMessageBox::Cancel);
        if (btn != QMessageBox::Ok) {
            finishRun(false);
            return;
        }
    }
    const double target =
        topZ_->value() + puckZ_ - s.value("probe/travel", 25.0).toDouble();
    mc_->probe(QString("G38.2 Z%1 F%2")
                   .arg(target)
                   .arg(s.value("probe/feed", 40.0).toDouble()));
}

void StockPanel::onProbe(bool ok, double, double, double z) {
    if (!running_) return;
    if (!ok) {
        info_->setText("Probe failed - check wiring / travel range.");
        finishRun(false);
        return;
    }
    pts_[idx_].z = z - puckZ_;   // surface under the puck
    pts_[idx_].done = true;
    ++idx_;
    mc_->sendCommand(QString("G0 Z%1")
                         .arg(topZ_->value() + puckZ_ + clearZ_->value()));
    nextPoint();
}

void StockPanel::finishRun(bool completed) {
    running_ = false;
    probeBtn_->setEnabled(true);
    mc_->sendCommand(QString("G0 Z%1")
                         .arg(topZ_->value() + puckZ_ + clearZ_->value()));
    if (completed) {
        haveData_ = true;
        showBtn_->setEnabled(true);
        info_->setText("Stock top measured.");
        updateStats();
        showOverlay();
    } else if (!haveData_) {
        info_->setText("Probing aborted.");
    }
}

void StockPanel::updateStats() {
    double zmin = 1e30, zmax = -1e30, sum = 0;
    for (auto& p : pts_) {
        zmin = std::min(zmin, p.z);
        zmax = std::max(zmax, p.z);
        sum += p.z;
    }
    const double mean = sum / pts_.size();
    stats_->setText(
        QString("Measured top: mean %1, min %2, max %3, tilt %4 mm\n"
                "vs estimate top %5: mean off by %6 mm")
            .arg(mean, 0, 'f', 3)
            .arg(zmin, 0, 'f', 3)
            .arg(zmax, 0, 'f', 3)
            .arg(zmax - zmin, 0, 'f', 3)
            .arg(topZ_->value(), 0, 'f', 3)
            .arg(mean - topZ_->value(), 0, 'f', 3));
}

std::vector<float> StockPanel::estimateLines() const {
    std::vector<float> v;
    const double x0 = cx_->value(), y0 = cy_->value();
    const double x1 = x0 + sx_->value(), y1 = y0 + sy_->value();
    const double zt = topZ_->value(), zb = zt - thick_->value();
    for (double z : {zt, zb}) {
        pushLine(v, x0, y0, z, x1, y0, z);
        pushLine(v, x1, y0, z, x1, y1, z);
        pushLine(v, x1, y1, z, x0, y1, z);
        pushLine(v, x0, y1, z, x0, y0, z);
    }
    pushLine(v, x0, y0, zt, x0, y0, zb);
    pushLine(v, x1, y0, zt, x1, y0, zb);
    pushLine(v, x1, y1, zt, x1, y1, zb);
    pushLine(v, x0, y1, zt, x0, y1, zb);
    return v;
}

std::vector<float> StockPanel::measuredLines() const {
    // serpentine-stored points -> row-major index
    auto at = [&](int i, int j) -> const Pt& {
        int ii = (j % 2) ? gridNx_ - 1 - i : i;
        return pts_[j * gridNx_ + ii];
    };
    std::vector<float> v;
    for (int j = 0; j < gridNy_; ++j)
        for (int i = 0; i + 1 < gridNx_; ++i) {
            const Pt &a = at(i, j), &b = at(i + 1, j);
            pushLine(v, a.x, a.y, a.z, b.x, b.y, b.z);
        }
    for (int i = 0; i < gridNx_; ++i)
        for (int j = 0; j + 1 < gridNy_; ++j) {
            const Pt &a = at(i, j), &b = at(i, j + 1);
            pushLine(v, a.x, a.y, a.z, b.x, b.y, b.z);
        }
    return v;
}

void StockPanel::showOverlay() {
    if (!haveData_ && pts_.empty()) return;
    emit overlayReady(estimateLines(),
                      haveData_ ? measuredLines() : std::vector<float>{});
}
