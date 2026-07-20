// Isolation routing + drill G-code generation from parsed layers.
#pragma once
#include <string>
#include <vector>

#include "excellon.h"
#include "gerber.h"

namespace scnc {

struct IsolationOptions {
    double toolDiameter = 0.2;   // mm (V-bit effective width at cut depth)
    int passes = 1;              // concentric isolation passes
    double overlap = 0.5;        // fraction of tool dia between passes
    double cutZ = -0.06;         // mm
    double travelZ = 1.0;        // mm
    double feed = 120;           // mm/min cutting
    double plunge = 60;          // mm/min plunging
    int spindleRpm = 10000;      // S word (0 = omit)
    bool mirrorX = false;        // for bottom layers: x -> -x
    // Fill interior copper voids smaller than this (max bbox dimension, mm)
    // so drill/via holes aren't isolated; larger voids (pour clearances)
    // are kept. 0 disables hole filling.
    double fillHolesBelow = 2.5;
};

struct IsolationResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    Clipper2Lib::PathsD toolpaths;     // for preview
    Clipper2Lib::PathsD cleanedCopper; // copper after merge + hole fill
    double lengthMm = 0;               // total cutting length
};

IsolationResult isolationRoute(const GerberLayer& layer,
                               const IsolationOptions& opt);

struct DrillOptions {
    double cutZ = -1.8;
    double travelZ = 2.0;
    double plunge = 90;          // mm/min
    int spindleRpm = 10000;
    bool singleTool = true;      // one continuous job, no toolchange pauses
    bool mirrorX = false;
};

struct DrillResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    int holes = 0;
};

DrillResult drillGcode(const DrillFile& drills, const DrillOptions& opt);

}  // namespace scnc
