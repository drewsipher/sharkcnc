// Board outline / cut-out with holding tabs. Cuts around a boundary polygon
// (offset by the tool radius) over multiple depth passes, lifting over
// evenly-spaced tabs so the part stays attached until it's freed by hand.
#pragma once
#include <string>

#include <clipper2/clipper.h>

namespace scnc {

struct OutlineOptions {
    double toolDiameter = 1.0;
    double cutZ = -1.8;         // final depth (through board + a little)
    double depthPerPass = 0.4;  // mm per pass (positive)
    double travelZ = 3.0;
    double feed = 300;
    double plunge = 120;
    int spindleRpm = 12000;
    bool outside = true;        // cut outside the boundary (keep the part)
    int tabs = 4;               // holding tabs, evenly spaced (0 = none)
    double tabWidth = 3.0;      // mm along the perimeter
    double tabHeight = 0.5;     // mm of material left under each tab
    double sample = 0.5;        // mm path sampling step
};

struct OutlineResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    Clipper2Lib::PathsD toolpaths;
    double lengthMm = 0;
    int passes = 0;
};

// A rectangular board boundary helper (work coords).
Clipper2Lib::PathsD rectBoundary(double x0, double y0, double w, double h);

OutlineResult outlineRoutine(const Clipper2Lib::PathsD& boundary,
                             const OutlineOptions& opt);

}  // namespace scnc
