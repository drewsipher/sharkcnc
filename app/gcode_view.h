// Fast 2D toolpath view: QPainter, pan/zoom, rapids vs cuts, live tool
// position. Handles million-line programs by caching the path geometry.
#pragma once
#include <QWidget>

#include <clipper2/clipper.h>

#include "gcode/parser.h"

class GcodeView : public QWidget {
    Q_OBJECT
public:
    explicit GcodeView(QWidget* parent = nullptr);

    void setProgram(const scnc::Program& p);
    void clearProgram();
    void setCopper(const Clipper2Lib::PathsD& copper);  // translucent overlay
    void clearCopper();
    void setToolPosition(double x, double y);
    void fit();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QPointF toScreen(double x, double y) const;
    void rebuildCache();

    scnc::Program prog_;
    QVector<QPolygonF> rapids_, cuts_, copper_;
    double scale_ = 4.0;         // px per mm
    QPointF center_{0, 0};       // mm at widget centre
    QPoint dragStart_;
    QPointF dragCenter_;
    double toolX_ = 0, toolY_ = 0;
    bool haveTool_ = false;
};
