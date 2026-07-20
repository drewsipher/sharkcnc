#include "gcode_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

using namespace scnc;

GcodeView::GcodeView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(300, 200);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(24, 26, 28));
    setPalette(pal);
    setMouseTracking(false);
}

void GcodeView::setProgram(const Program& p) {
    prog_ = p;
    rebuildCache();
    fit();
}

void GcodeView::clearProgram() {
    prog_ = Program{};
    rapids_.clear();
    cuts_.clear();
    copper_.clear();
    update();
}

void GcodeView::setCopper(const Clipper2Lib::PathsD& copper) {
    copper_.clear();
    for (const auto& path : copper) {
        QPolygonF poly;
        poly.reserve(static_cast<int>(path.size()));
        for (const auto& pt : path) poly << QPointF(pt.x, -pt.y);
        copper_ << poly;
    }
    update();
}

void GcodeView::clearCopper() {
    copper_.clear();
    update();
}

void GcodeView::setToolPosition(double x, double y) {
    toolX_ = x;
    toolY_ = y;
    haveTool_ = true;
    update();
}

void GcodeView::rebuildCache() {
    rapids_.clear();
    cuts_.clear();
    // merge consecutive segments of the same class into polylines
    QPolygonF cur;
    bool curIsCut = false;
    auto flush = [&] {
        if (cur.size() >= 2)
            (curIsCut ? cuts_ : rapids_).push_back(cur);
        cur.clear();
    };
    Vec3 last{1e30, 1e30, 1e30};
    for (const auto& s : prog_.segments) {
        bool isCut = s.type != MotionType::Rapid;
        bool contiguous =
            std::abs(s.from.x - last.x) < 1e-9 &&
            std::abs(s.from.y - last.y) < 1e-9 && isCut == curIsCut;
        if (!contiguous) {
            flush();
            curIsCut = isCut;
            cur << QPointF(s.from.x, -s.from.y);
        }
        if (s.type == MotionType::ArcCW || s.type == MotionType::ArcCCW) {
            for (auto& p : tessellateArc(s, 0.05))
                cur << QPointF(p.x, -p.y);
        } else {
            cur << QPointF(s.to.x, -s.to.y);
        }
        last = s.to;
    }
    flush();
}

QPointF GcodeView::toScreen(double x, double y) const {
    return {width() / 2.0 + (x - center_.x()) * scale_,
            height() / 2.0 + (y - center_.y()) * scale_};
}

void GcodeView::fit() {
    if (!prog_.hasBounds()) {
        center_ = {0, 0};
        scale_ = 4.0;
        update();
        return;
    }
    double w = prog_.max.x - prog_.min.x, h = prog_.max.y - prog_.min.y;
    center_ = QPointF((prog_.min.x + prog_.max.x) / 2,
                      -(prog_.min.y + prog_.max.y) / 2);
    double sx = width() / (w + 10.0), sy = height() / (h + 10.0);
    scale_ = std::max(0.05, std::min(sx, sy));
    update();
}

void GcodeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, scale_ > 1.0);

    // adaptive decade grid: a coarse level that's always visible plus a
    // finer level (coarse/10) that fades out as it gets dense
    {
        // smallest power of ten whose spacing is >= ~60 px on screen
        double coarse = std::pow(10.0, std::ceil(std::log10(60.0 / scale_)));
        double fine = coarse / 10.0;
        double finePx = fine * scale_;
        double fineAlpha = std::clamp((finePx - 5.0) / 15.0, 0.0, 1.0);
        QPointF o = toScreen(0, 0);
        auto drawGrid = [&](double stepMm, int alpha) {
            if (alpha <= 0) return;
            double step = stepMm * scale_;
            p.setPen(QColor(90, 96, 104, alpha));
            for (double x = std::fmod(o.x(), step); x < width(); x += step)
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
            for (double y = std::fmod(o.y(), step); y < height(); y += step)
                p.drawLine(QPointF(0, y), QPointF(width(), y));
        };
        drawGrid(fine, static_cast<int>(fineAlpha * 42));   // faint, fades
        drawGrid(coarse, 70);                               // solid
    }
    // axes
    QPointF o = toScreen(0, 0);
    p.setPen(QColor(120, 60, 60));
    p.drawLine(QPointF(0, o.y()), QPointF(width(), o.y()));
    p.setPen(QColor(60, 120, 60));
    p.drawLine(QPointF(o.x(), 0), QPointF(o.x(), height()));

    p.save();
    p.translate(width() / 2.0, height() / 2.0);
    p.scale(scale_, scale_);
    p.translate(-center_.x(), -center_.y());

    // copper first, underneath the toolpaths
    if (!copper_.isEmpty()) {
        p.setPen(QPen(QColor(184, 115, 51, 200), 0));  // copper edge
        p.setBrush(QColor(184, 115, 51, 70));           // translucent fill
        QPainterPath cp;
        cp.setFillRule(Qt::OddEvenFill);
        for (const auto& poly : copper_) cp.addPolygon(poly);
        cp.closeSubpath();
        p.drawPath(cp);
        p.setBrush(Qt::NoBrush);
    }

    QPen rapidPen(QColor(110, 110, 110, 150));
    rapidPen.setWidthF(0);
    rapidPen.setStyle(Qt::DashLine);
    p.setPen(rapidPen);
    for (const auto& poly : rapids_) p.drawPolyline(poly);

    QPen cutPen(QColor(80, 180, 255));
    cutPen.setWidthF(0);
    p.setPen(cutPen);
    for (const auto& poly : cuts_) p.drawPolyline(poly);
    p.restore();

    // work origin marker
    QPointF org = toScreen(0, 0);
    p.setPen(QPen(QColor(230, 200, 90), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(org, 5, 5);
    p.drawText(org + QPointF(8, -6), "0,0");

    if (haveTool_) {
        QPointF t = toScreen(toolX_, -toolY_);
        p.setPen(QPen(QColor(255, 80, 80), 2));
        p.drawLine(t + QPointF(-8, 0), t + QPointF(8, 0));
        p.drawLine(t + QPointF(0, -8), t + QPointF(0, 8));
        p.drawEllipse(t, 4, 4);
    }

    // scale bar (round to a nice 1/2/5 x 10^n length)
    double targetPx = 80;
    double mm = targetPx / scale_;
    double mag = std::pow(10, std::floor(std::log10(mm)));
    double norm = mm / mag;
    double nice = norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10;
    double barMm = nice * mag;
    double barPx = barMm * scale_;
    double bx = width() - barPx - 14, by = height() - 16;
    p.setPen(QPen(QColor(170, 174, 178), 2));
    p.drawLine(QPointF(bx, by), QPointF(bx + barPx, by));
    p.drawLine(QPointF(bx, by - 4), QPointF(bx, by + 4));
    p.drawLine(QPointF(bx + barPx, by - 4), QPointF(bx + barPx, by + 4));
    p.drawText(QRectF(bx, by - 20, barPx, 16), Qt::AlignCenter,
               QString("%1 mm").arg(barMm, 0, 'g', 3));

    p.setPen(QColor(120, 124, 128));
    p.drawText(8, height() - 8,
               QString("%1 segments   ·   double-click or F to fit")
                   .arg(prog_.segments.size()));
}

void GcodeView::mouseDoubleClickEvent(QMouseEvent*) { fit(); }

void GcodeView::zoom(double factor) {
    scale_ = std::clamp(scale_ * factor, 0.02, 500.0);
    update();
}

void GcodeView::wheelEvent(QWheelEvent* e) {
    double f = e->angleDelta().y() > 0 ? 1.2 : 1 / 1.2;
    // zoom about cursor
    QPointF before{
        center_.x() + (e->position().x() - width() / 2.0) / scale_,
        center_.y() + (e->position().y() - height() / 2.0) / scale_};
    scale_ = std::clamp(scale_ * f, 0.02, 500.0);
    QPointF after{
        center_.x() + (e->position().x() - width() / 2.0) / scale_,
        center_.y() + (e->position().y() - height() / 2.0) / scale_};
    center_ += before - after;
    update();
}

void GcodeView::mousePressEvent(QMouseEvent* e) {
    dragStart_ = e->pos();
    dragCenter_ = center_;
}

void GcodeView::mouseMoveEvent(QMouseEvent* e) {
    QPoint d = e->pos() - dragStart_;
    center_ = dragCenter_ - QPointF(d.x() / scale_, d.y() / scale_);
    update();
}

void GcodeView::resizeEvent(QResizeEvent*) { update(); }
