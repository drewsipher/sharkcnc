// Stock setup tab: enter the estimated stock block, probe its real top
// surface at a grid of points (with guided puck placement for
// non-conductive stock), and compare estimate vs measurement as floating
// wireframes in the 3D view.
#pragma once
#include <QWidget>
#include <vector>

class MachineClient;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

class StockPanel : public QWidget {
    Q_OBJECT
public:
    explicit StockPanel(MachineClient* mc, QWidget* parent = nullptr);

signals:
    // line-list vertex data (x,y,z triples, GL_LINES pairs)
    void overlayReady(const std::vector<float>& estimateLines,
                      const std::vector<float>& measuredLines);

private:
    struct Pt { double x, y; double z = 0; bool done = false; };

    void startProbing();
    void nextPoint();
    void onProbe(bool ok, double x, double y, double z);
    void finishRun(bool completed);
    void showOverlay();
    std::vector<float> estimateLines() const;
    std::vector<float> measuredLines() const;
    void updateStats();

    MachineClient* mc_;
    QDoubleSpinBox *cx_, *cy_, *sx_, *sy_, *topZ_, *thick_;
    QSpinBox *nx_, *ny_;
    QDoubleSpinBox *margin_, *clearZ_;
    QCheckBox* puck_;
    QLabel *stats_, *info_;
    QPushButton *probeBtn_, *showBtn_;

    std::vector<Pt> pts_;
    size_t idx_ = 0;
    int gridNx_ = 0, gridNy_ = 0;
    bool running_ = false, haveData_ = false;
    double puckZ_ = 0;
};
