#include "facing.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gcode_out.h"
#include "../gcode/parser.h"

namespace scnc {

using namespace Clipper2Lib;

namespace {

double polylineLength(const PathD& p) {
    double len = 0;
    for (size_t i = 1; i < p.size(); ++i)
        len += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return len;
}

// One layer of coverage at Z=0 (caller offsets Z per pass). Points are the
// XY path the tool centre follows to clear the rectangle.
PathD rasterLayer(const FacingOptions& o) {
    PathD path;
    double r = o.toolDiameter / 2;
    double step = std::max(0.1, o.toolDiameter * std::clamp(o.stepover, 0.05, 0.95));
    double xLo = o.x0 + r - o.cleanEdge;
    double xHi = o.x0 + o.width - r + o.cleanEdge;
    double yLo = o.y0 + r;
    double yHi = o.y0 + o.height - r;
    if (xHi < xLo) { double m = (xLo + xHi) / 2; xLo = xHi = m; }
    if (yHi < yLo) { double m = (yLo + yHi) / 2; yLo = yHi = m; }

    int rows = std::max(1, static_cast<int>(std::ceil((yHi - yLo) / step)));
    bool dir = o.climb;  // start direction
    for (int i = 0; i <= rows; ++i) {
        double y = yLo + std::min(yHi - yLo, i * step);
        if (dir) {
            path.push_back({xLo, y});
            path.push_back({xHi, y});
        } else {
            path.push_back({xHi, y});
            path.push_back({xLo, y});
        }
        dir = !dir;
        if (y >= yHi) break;
    }
    return path;
}

// Inward rectangular spiral of the centre path.
PathD spiralLayer(const FacingOptions& o) {
    PathD path;
    double r = o.toolDiameter / 2;
    double step = std::max(0.1, o.toolDiameter * std::clamp(o.stepover, 0.05, 0.95));
    double xa = o.x0 + r, xb = o.x0 + o.width - r;
    double ya = o.y0 + r, yb = o.y0 + o.height - r;
    while (xa <= xb && ya <= yb) {
        path.push_back({xa, ya});
        path.push_back({xb, ya});
        path.push_back({xb, yb});
        path.push_back({xa, yb});
        path.push_back({xa, ya + step});  // step in for next ring
        xa += step; ya += step;
        xb -= step; yb -= step;
    }
    return path;
}

}  // namespace

FacingResult facingRoutine(const FacingOptions& o) {
    FacingResult res;
    if (o.width <= 0 || o.height <= 0) {
        res.error = "area must be positive";
        return res;
    }
    if (o.toolDiameter <= 0) {
        res.error = "tool diameter must be > 0";
        return res;
    }
    if (o.depthPerPass <= 0) {
        res.error = "depth per pass must be > 0";
        return res;
    }

    PathD layer = o.spiral ? spiralLayer(o) : rasterLayer(o);
    if (layer.size() < 2) {
        res.error = "area smaller than the tool";
        return res;
    }

    int passes = std::max(1, static_cast<int>(std::ceil(o.totalDepth /
                                                        o.depthPerPass)));
    res.passes = passes;

    GcodeOut g;
    g.comment("sharkcnc surface facing");
    g.comment("area " + fmtNum(o.width) + "x" + fmtNum(o.height) + "mm, tool " +
              fmtNum(o.toolDiameter) + "mm, " + std::to_string(passes) +
              " pass(es) to " + fmtNum(-o.totalDepth));
    g.header(o.spindleRpm);
    g.rapidZ(o.travelZ);

    for (int p = 0; p < passes; ++p) {
        double z = -std::min(o.totalDepth, (p + 1) * o.depthPerPass);
        g.rapidXY(layer.front().x, layer.front().y);
        g.feedZ(z, o.plunge);
        for (size_t i = 1; i < layer.size(); ++i)
            g.feedXY(layer[i].x, layer[i].y, o.feed);
        g.rapidZ(o.travelZ);
        res.lengthMm += polylineLength(layer);
    }
    g.footer();

    res.ok = true;
    res.gcode = g.str();
    res.toolpaths = PathsD{layer};
    return res;
}

}  // namespace scnc
