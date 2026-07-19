// Height-map probing wizard: probes a serpentine grid over the board area,
// builds a HeightMap, saves it, and can warp the loaded program.
#pragma once
#include <QDialog>

#include "heightmap/heightmap.h"

class MachineClient;
class QDoubleSpinBox;
class QSpinBox;
class QProgressBar;
class QPushButton;
class QLabel;

class ProbeDialog : public QDialog {
    Q_OBJECT
public:
    ProbeDialog(MachineClient* mc, double minX, double minY, double maxX,
                double maxY, QWidget* parent = nullptr);

    bool hasMap() const { return mapDone_; }
    const scnc::HeightMap& map() const { return map_; }

private slots:
    void startProbing();
    void onProbe(bool ok, double x, double y, double z);

private:
    void nextPoint();

    MachineClient* mc_;
    scnc::HeightMap map_;
    std::vector<scnc::HeightMap::Point> points_;
    size_t idx_ = 0;
    bool running_ = false, mapDone_ = false;

    QDoubleSpinBox *x0_, *y0_, *w_, *h_, *spacing_, *clearZ_, *probeZ_, *feed_;
    QProgressBar* progress_;
    QPushButton *startBtn_, *saveBtn_, *applyBtn_;
    QLabel* info_;
};
