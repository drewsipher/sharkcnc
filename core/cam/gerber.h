// RS-274X (Gerber) parser producing unioned copper polygons, built for the
// output of real ECAD tools (KiCad first). Supports standard apertures
// (C/R/O/P with holes), aperture macros (primitives 1, 4, 5, 20, 21 with
// expressions and $ parameters), D01/D02/D03, linear and arc interpolation,
// G36/G37 regions and LPD/LPC polarity.
#pragma once
#include <string>
#include <vector>

#include <clipper2/clipper.h>

namespace scnc {

struct GerberLayer {
    Clipper2Lib::PathsD copper;   // unioned filled copper, millimetres
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    int apertures = 0, flashes = 0, strokes = 0, regions = 0;
    std::vector<std::string> warnings;
};

struct GerberResult {
    bool ok = false;
    std::string error;
    GerberLayer layer;
};

GerberResult parseGerber(const std::string& text);

}  // namespace scnc
