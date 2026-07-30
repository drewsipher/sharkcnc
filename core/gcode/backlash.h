// Backlash compensation: rewrite a G-code program so every direction
// reversal is preceded by a "takeup" move of the measured backlash. The
// takeup winds the leadscrew through its slack while the table stays
// put, so the real cut lands where the program intended. FluidNC (like
// grbl) deliberately has no firmware backlash support and recommends
// exactly this sender-side post-processing.
#pragma once
#include <string>

#include "parser.h"

namespace scnc {

struct BacklashOptions {
    double x = 0, y = 0, z = 0;   // measured backlash per axis, mm
    double takeupFeed = 300;      // mm/min for takeup moves during cutting
    double chordTol = 0.01;       // arc tessellation tolerance, mm
    bool enabled() const { return x > 0 || y > 0 || z > 0; }
};

struct BacklashResult {
    bool ok = false;
    std::string error;
    std::string gcode;
    int takeups = 0;              // reversal moves inserted
};

// Requires absolute (G90) millimetre programs, like warpGcode. Arcs are
// tessellated so mid-arc quadrant reversals are compensated too. Probe
// lines (G38.x) pass through verbatim but get a takeup beforehand when
// the probe direction is a reversal.
//
// Convention: coordinates are shifted by +backlash on an axis while its
// last motion was positive, 0 while negative. The initial slack state of
// the machine is unknowable, so the first move on each axis establishes
// direction without a takeup - zero the machine by approaching in the
// same direction as the first cut for best absolute accuracy.
BacklashResult applyBacklash(const Program& prog, const BacklashOptions& opt);

}  // namespace scnc
