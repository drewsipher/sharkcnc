#include "isolation.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "gcode_out.h"
#include "../gcode/parser.h"

namespace scnc {

using namespace Clipper2Lib;

namespace {

double pathLength(const PathD& p, bool closed) {
    double len = 0;
    for (size_t i = 1; i < p.size(); ++i)
        len += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    if (closed && p.size() > 2)
        len += std::hypot(p.front().x - p.back().x, p.front().y - p.back().y);
    return len;
}

// Greedy nearest-neighbour ordering of closed paths; each path may start
// at any vertex (we rotate it to the vertex closest to the previous end).
PathsD orderPaths(PathsD paths) {
    PathsD out;
    PointD cur{0, 0};
    std::vector<bool> used(paths.size(), false);
    for (size_t n = 0; n < paths.size(); ++n) {
        double best = 1e30;
        size_t bi = 0, bv = 0;
        for (size_t i = 0; i < paths.size(); ++i) {
            if (used[i]) continue;
            for (size_t v = 0; v < paths[i].size(); ++v) {
                double d = std::hypot(paths[i][v].x - cur.x,
                                      paths[i][v].y - cur.y);
                if (d < best) { best = d; bi = i; bv = v; }
            }
        }
        used[bi] = true;
        PathD rotated;
        rotated.reserve(paths[bi].size());
        for (size_t v = 0; v < paths[bi].size(); ++v)
            rotated.push_back(paths[bi][(bv + v) % paths[bi].size()]);
        cur = rotated.front();
        out.push_back(std::move(rotated));
    }
    return out;
}

// CCW rotation about the origin: 90 (x,y)->(-y,x), 180, 270
void rotatePt(double& x, double& y, int deg) {
    double ox = x, oy = y;
    switch (((deg % 360) + 360) % 360) {
        case 90:  x = -oy; y = ox;  break;
        case 180: x = -ox; y = -oy; break;
        case 270: x = oy;  y = -ox; break;
        default: break;
    }
}

void rotatePaths(PathsD& ps, int deg) {
    if (((deg % 360) + 360) % 360 == 0) return;
    for (auto& p : ps)
        for (auto& pt : p) rotatePt(pt.x, pt.y, deg);
}

void mirror(PathsD& ps) {
    for (auto& p : ps) {
        for (auto& pt : p) pt.x = -pt.x;
        std::reverse(p.begin(), p.end());
    }
}

}  // namespace

IsolationResult isolationRoute(const GerberLayer& layer,
                               const IsolationOptions& opt) {
    IsolationResult res;
    if (layer.copper.empty()) {
        res.error = "layer has no copper";
        return res;
    }
    if (opt.toolDiameter <= 0) {
        res.error = "tool diameter must be > 0";
        return res;
    }

    PathsD copper = layer.copper;
    rotatePaths(copper, opt.rotateDeg);
    if (opt.mirrorX) mirror(copper);

    // Rebuild the copper as a nesting tree so we can tell a *drill hole* (an
    // enclosed void with no copper island inside it) from a *pour clearance*
    // (a void that surrounds an isolated pad/trace). Both can be small, so
    // size alone can't distinguish them - topology does. Size alone also
    // can't distinguish a drill hole from a thermal-relief spoke or a text
    // knockout, so a fill additionally requires the void to be ROUND
    // (isoperimetric ratio) - drills are circles, spokes and letters aren't.
    {
        ClipperD cl;
        cl.AddSubject(copper);
        PolyTreeD tree;
        cl.Execute(ClipType::Union, FillRule::NonZero, tree);
        PathsD cleaned;
        std::function<void(const PolyPathD*)> walk = [&](const PolyPathD* node) {
            for (size_t i = 0; i < node->Count(); ++i) {
                const PolyPathD* ch = node->Child(i);
                res.trueCopper.push_back(ch->Polygon());
                bool fill = false;
                if (ch->IsHole() && ch->Count() == 0 && opt.fillHolesBelow > 0) {
                    auto b = GetBounds(PathsD{ch->Polygon()});
                    if (std::max(b.Width(), b.Height()) < opt.fillHolesBelow) {
                        const PathD& p = ch->Polygon();
                        double a = std::abs(Area(p)), per = 0;
                        for (size_t k = 0; k < p.size(); ++k) {
                            const auto& s = p[k];
                            const auto& e = p[(k + 1) % p.size()];
                            per += std::hypot(s.x - e.x, s.y - e.y);
                        }
                        double roundness =
                            per > 0 ? 4.0 * M_PI * a / (per * per) : 0;
                        fill = roundness >= opt.fillRoundness;
                    }
                }
                if (fill) res.filledVoids.push_back(ch->Polygon());
                else cleaned.push_back(ch->Polygon());
                walk(ch);  // childless holes have no subtree, so this is safe
            }
        };
        walk(&tree);
        copper.swap(cleaned);
    }
    res.cleanedCopper = copper;

    PathsD all;
    for (int pass = 0; pass < std::max(1, opt.passes); ++pass) {
        double off = opt.toolDiameter / 2 +
                     pass * opt.toolDiameter * (1.0 - opt.overlap);
        PathsD p = InflatePaths(copper, off, JoinType::Round,
                                EndType::Polygon, 2.0, 4);
        p = SimplifyPaths(p, 0.002);
        all.insert(all.end(), p.begin(), p.end());
    }
    if (all.empty()) {
        res.error = "no isolation paths generated";
        return res;
    }
    all = orderPaths(std::move(all));

    GcodeOut g;
    g.comment("sharkcnc isolation route");
    g.comment("tool " + fmtNum(opt.toolDiameter) + "mm, passes " +
              std::to_string(opt.passes) + ", cut Z " + fmtNum(opt.cutZ));
    g.header(opt.spindleRpm);
    g.rapidZ(opt.travelZ);
    for (const auto& path : all) {
        if (path.size() < 3) continue;
        g.rapidXY(path[0].x, path[0].y);
        g.feedZ(opt.cutZ, opt.plunge);
        for (size_t i = 1; i < path.size(); ++i)
            g.feedXY(path[i].x, path[i].y, opt.feed);
        g.feedXY(path[0].x, path[0].y, opt.feed);  // close the loop
        g.rapidZ(opt.travelZ);
        res.lengthMm += pathLength(path, true);
    }
    g.footer();

    res.ok = true;
    res.gcode = g.str();
    res.toolpaths = std::move(all);
    return res;
}

DrillResult drillGcode(const DrillFile& drills, const DrillOptions& opt) {
    DrillResult res;
    if (drills.hits.empty()) {
        res.error = "no holes in drill file";
        return res;
    }

    GcodeOut g;
    g.comment("sharkcnc drill");
    g.header(opt.spindleRpm);
    g.rapidZ(opt.travelZ);

    auto emitHits = [&](const std::vector<DrillHit>& hits) {
        // nearest-neighbour ordering
        std::vector<bool> used(hits.size(), false);
        double cx = 0, cy = 0;
        for (size_t n = 0; n < hits.size(); ++n) {
            double best = 1e30;
            size_t bi = 0;
            for (size_t i = 0; i < hits.size(); ++i) {
                if (used[i]) continue;
                double x = hits[i].x, y = hits[i].y;
                rotatePt(x, y, opt.rotateDeg);
                if (opt.mirrorX) x = -x;
                double d = std::hypot(x - cx, y - cy);
                if (d < best) { best = d; bi = i; }
            }
            used[bi] = true;
            double x = hits[bi].x, y = hits[bi].y;
            rotatePt(x, y, opt.rotateDeg);
            if (opt.mirrorX) x = -x;
            cx = x; cy = y;
            g.rapidXY(x, y);
            g.feedZ(opt.cutZ, opt.plunge);
            g.rapidZ(opt.travelZ);
            ++res.holes;
        }
    };

    if (opt.singleTool) {
        g.comment("single-tool mode: all " +
                  std::to_string(drills.hits.size()) +
                  " holes, chuck the smallest drill");
        emitHits(drills.hits);
    } else {
        for (const auto& t : drills.tools) {
            std::vector<DrillHit> hits;
            for (const auto& h : drills.hits)
                if (h.tool == t.number) hits.push_back(h);
            if (hits.empty()) continue;
            g.comment("tool T" + std::to_string(t.number) + " dia " +
                      fmtNum(t.diameter) + "mm (" +
                      std::to_string(hits.size()) + " holes)");
            g.rapidZ(opt.travelZ + 20);  // headroom for manual change
            g.raw("M0 ; change to " + fmtNum(t.diameter) + "mm drill");
            emitHits(hits);
        }
    }
    g.footer();
    res.ok = true;
    res.gcode = g.str();
    return res;
}

}  // namespace scnc
