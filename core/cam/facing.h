// Surface facing / spoilboard flattening: raster or spiral clearing of a
// rectangular area over one or more depth passes.
#pragma once
#include <string>

#include <clipper2/clipper.h>

namespace scnc {

struct FacingOptions {
    double x0 = 0, y0 = 0;      // work-coord corner of the area (mm)
    double width = 50, height = 50;
    double toolDiameter = 6.0;
    double stepover = 0.4;      // fraction of tool diameter between passes
    double totalDepth = 0.2;    // mm to remove (positive)
    double depthPerPass = 0.2;  // mm per Z level (positive)
    double feed = 800;
    double plunge = 300;
    double travelZ = 3.0;
    int spindleRpm = 12000;
    bool spiral = false;        // false = raster zig-zag, true = inward spiral
    bool climb = true;          // affects raster direction bias
    double cleanEdge = 0.0;     // extra overtravel past edges (mm)
};

struct FacingResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    Clipper2Lib::PathsD toolpaths;
    double lengthMm = 0;
    int passes = 0;             // depth levels
};

FacingResult facingRoutine(const FacingOptions& opt);

}  // namespace scnc
