#include "outline.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gcode_out.h"
#include "../gcode/parser.h"

namespace scnc {

using namespace Clipper2Lib;

namespace {

// Resample a closed polygon at ~step spacing, returning points with the
// cumulative arclength at each (used for tab placement). Also returns the
// total perimeter.
struct SampledLoop {
    std::vector<PointD> pts;
    std::vector<double> arc;   // arclength at each point
    double perimeter = 0;
};

SampledLoop sampleClosed(const PathD& poly, double step) {
    SampledLoop out;
    if (poly.size() < 2) return out;
    size_t n = poly.size();
    double acc = 0;
    for (size_t i = 0; i < n; ++i) {
        PointD a = poly[i], b = poly[(i + 1) % n];
        double segLen = std::hypot(b.x - a.x, b.y - a.y);
        int steps = std::max(1, static_cast<int>(std::ceil(segLen / step)));
        for (int k = 0; k < steps; ++k) {
            double t = double(k) / steps;
            out.pts.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
            out.arc.push_back(acc + segLen * t);
        }
        acc += segLen;
    }
    out.perimeter = acc;
    return out;
}

}  // namespace

PathsD rectBoundary(double x0, double y0, double w, double h) {
    return PathsD{PathD{{x0, y0}, {x0 + w, y0}, {x0 + w, y0 + h},
                        {x0, y0 + h}}};
}

OutlineResult outlineRoutine(const PathsD& boundary,
                             const OutlineOptions& opt) {
    OutlineResult res;
    if (boundary.empty()) {
        res.error = "no boundary";
        return res;
    }
    if (opt.toolDiameter <= 0 || opt.depthPerPass <= 0) {
        res.error = "bad tool/pass parameters";
        return res;
    }

    double off = opt.toolDiameter / 2 * (opt.outside ? 1.0 : -1.0);
    PathsD cut = InflatePaths(boundary, off, JoinType::Round,
                              EndType::Polygon, 2.0, 4);
    cut = SimplifyPaths(cut, 0.002);
    if (cut.empty()) {
        res.error = "offset produced no path (tool too big for an inside cut?)";
        return res;
    }

    int passes = std::max(
        1, static_cast<int>(std::ceil(std::abs(opt.cutZ) / opt.depthPerPass)));
    res.passes = passes;
    double tabTopZ = opt.cutZ + opt.tabHeight;  // Z the tool lifts to over tabs

    GcodeOut g;
    g.comment("sharkcnc board outline");
    g.comment("tool " + fmtNum(opt.toolDiameter) + "mm, " +
              std::to_string(passes) + " pass(es) to " + fmtNum(opt.cutZ) +
              ", " + std::to_string(opt.tabs) + " tabs");
    g.header(opt.spindleRpm);
    g.rapidZ(opt.travelZ);

    for (const PathD& loop : cut) {
        SampledLoop s = sampleClosed(loop, opt.sample);
        if (s.pts.size() < 3) continue;
        res.toolpaths.push_back(loop);

        // tab centre arclengths
        std::vector<double> tabCentres;
        for (int t = 0; t < opt.tabs; ++t)
            tabCentres.push_back(s.perimeter * t / std::max(1, opt.tabs));
        auto overTab = [&](double a) {
            for (double c : tabCentres) {
                double d = std::abs(a - c);
                d = std::min(d, s.perimeter - d);  // wrap
                if (d <= opt.tabWidth / 2) return true;
            }
            return false;
        };

        for (int p = 0; p < passes; ++p) {
            double z = -std::min(std::abs(opt.cutZ), (p + 1) * opt.depthPerPass);
            bool tabsThisPass = opt.tabs > 0 && z < tabTopZ - 1e-9;
            g.rapidXY(s.pts[0].x, s.pts[0].y);
            double z0 = (tabsThisPass && overTab(s.arc[0])) ? tabTopZ : z;
            g.feedZ(z0, opt.plunge);
            for (size_t i = 1; i <= s.pts.size(); ++i) {
                const PointD& pt = s.pts[i % s.pts.size()];
                double a = i < s.arc.size() ? s.arc[i] : s.perimeter;
                double zz = (tabsThisPass && overTab(a)) ? tabTopZ : z;
                g.raw("G1 X" + fmtNum(pt.x) + " Y" + fmtNum(pt.y) + " Z" +
                      fmtNum(zz) + " F" + fmtNum(opt.feed));
            }
            res.lengthMm += s.perimeter;
            g.rapidZ(opt.travelZ);
        }
    }
    g.footer();
    res.ok = true;
    res.gcode = g.str();
    return res;
}

}  // namespace scnc
