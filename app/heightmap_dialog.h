// 3D height-map viewer: colored, Z-exaggerated relief of a probed surface
// (diverging colors around Z=0: blue below, gray at zero, red above).
#pragma once
#include <QDialog>

#include "heightmap/heightmap.h"

class GcodeView3D;
class QLabel;
class QSlider;

class HeightmapDialog : public QDialog {
    Q_OBJECT
public:
    explicit HeightmapDialog(QWidget* parent = nullptr);
    // Preloaded map (e.g. straight from a probing run).
    void setMap(const scnc::HeightMap& m);
    bool loadFile(const QString& path);

signals:
    // a map was loaded from disk by the user (not preloaded via setMap)
    void mapLoaded(const scnc::HeightMap& m);
    // "Apply to loaded G-code" pressed
    void applyRequested(const scnc::HeightMap& m);

private:
    void refresh();

    scnc::HeightMap map_;
    GcodeView3D* view_ = nullptr;
    QSlider* exSlider_ = nullptr;
    QLabel *exLabel_ = nullptr, *stats_ = nullptr;
    QLabel *minChip_ = nullptr, *midChip_ = nullptr, *maxChip_ = nullptr;
    class QPushButton* applyBtn_ = nullptr;
};
