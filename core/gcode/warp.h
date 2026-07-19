// Height-map warping: rewrite a G-code program so every cutting move follows
// the probed surface. Long moves are split, arcs are tessellated to lines.
#pragma once
#include <string>

#include "../heightmap/heightmap.h"
#include "parser.h"

namespace scnc {

struct WarpOptions {
    double maxSegment = 2.0;   // mm: split moves so Z tracks the surface
    double chordTol = 0.01;    // mm: arc tessellation tolerance
    double zSafeMin = 1.0;     // moves at/above this Z are treated as travel
                               // and get the local offset applied unsplit
};

struct WarpResult {
    bool ok = false;
    std::string error;
    std::string gcode;
};

// Applies hm to every motion line of the program. Non-motion lines pass
// through verbatim. Requires absolute (G90) millimetre programs.
WarpResult warpGcode(const Program& prog, const HeightMap& hm,
                     const WarpOptions& opt = {});

}  // namespace scnc
