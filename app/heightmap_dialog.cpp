#include "heightmap_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>
#include <fstream>
#include <sstream>

#include "gcode_view3d.h"

using namespace scnc;

namespace {
QLabel* chip(const QString& css) {
    auto* l = new QLabel;
    l->setFixedSize(14, 14);
    l->setStyleSheet("background:" + css + ";border-radius:3px;");
    return l;
}
}  // namespace

HeightmapDialog::HeightmapDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Height map viewer");
    resize(760, 560);
    auto* lay = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    auto* loadBtn = new QPushButton("Load map...");
    top->addWidget(loadBtn);
    auto* topBtn = new QPushButton("Top");
    auto* isoBtn = new QPushButton("Iso");
    top->addWidget(topBtn);
    top->addWidget(isoBtn);
    top->addSpacing(16);
    top->addWidget(new QLabel("Z exaggeration"));
    exSlider_ = new QSlider(Qt::Horizontal);
    exSlider_->setRange(1, 100);
    exSlider_->setValue(20);
    exSlider_->setMinimumWidth(140);
    top->addWidget(exSlider_, 1);
    exLabel_ = new QLabel("20x");
    top->addWidget(exLabel_);
    lay->addLayout(top);

    view_ = new GcodeView3D(this);
    lay->addWidget(view_, 1);

    auto* leg = new QHBoxLayout;
    minChip_ = chip("#4078d9");
    midChip_ = chip("#94948f");
    maxChip_ = chip("#d9553d");
    leg->addWidget(minChip_);
    leg->addWidget(new QLabel("low"));
    leg->addSpacing(8);
    leg->addWidget(midChip_);
    leg->addWidget(new QLabel("Z=0"));
    leg->addSpacing(8);
    leg->addWidget(maxChip_);
    leg->addWidget(new QLabel("high"));
    leg->addStretch(1);
    stats_ = new QLabel("No map loaded.");
    leg->addWidget(stats_);
    lay->addLayout(leg);

    connect(loadBtn, &QPushButton::clicked, this, [this] {
        QString fn = QFileDialog::getOpenFileName(
            this, "Open height map", QString(),
            "Height map (*.json);;All files (*)");
        if (!fn.isEmpty() && !loadFile(fn))
            QMessageBox::warning(this, "Height map",
                                 "Could not parse that height map file.");
    });
    connect(topBtn, &QPushButton::clicked, view_, &GcodeView3D::viewTop);
    connect(isoBtn, &QPushButton::clicked, view_, &GcodeView3D::viewIso);
    connect(exSlider_, &QSlider::valueChanged, this, [this](int v) {
        exLabel_->setText(QString::number(v) + "x");
        refresh();
    });
}

void HeightmapDialog::setMap(const HeightMap& m) {
    map_ = m;
    refresh();
}

bool HeightmapDialog::loadFile(const QString& path) {
    std::ifstream f(path.toStdString());
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    HeightMap m;
    if (!HeightMap::fromJson(ss.str(), m) || !m.valid()) return false;
    setWindowTitle("Height map viewer - " + QFileInfo(path).fileName());
    setMap(m);
    return true;
}

void HeightmapDialog::refresh() {
    if (!map_.valid()) return;
    view_->setHeightMap(map_, exSlider_->value());
    double zmin = 1e30, zmax = -1e30;
    for (int j = 0; j < map_.ny(); ++j)
        for (int i = 0; i < map_.nx(); ++i) {
            zmin = std::min(zmin, map_.at(i, j));
            zmax = std::max(zmax, map_.at(i, j));
        }
    stats_->setText(QString("%1x%2 points   min %3   max %4   range %5 mm")
                        .arg(map_.nx())
                        .arg(map_.ny())
                        .arg(zmin, 0, 'f', 3)
                        .arg(zmax, 0, 'f', 3)
                        .arg(zmax - zmin, 0, 'f', 3));
}
