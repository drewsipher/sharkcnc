#include "gcode_out.h"

#include "../gcode/parser.h"

namespace scnc {

void GcodeOut::header(int spindleRpm) {
    raw("G21");
    raw("G90");
    raw("G94");
    if (spindleRpm > 0)
        raw("M3 S" + std::to_string(spindleRpm));
    else
        raw("M3");
}

void GcodeOut::footer() {
    raw("M5");
    raw("M2");
}

void GcodeOut::rapidZ(double z) { os_ << "G0 Z" << fmtNum(z) << "\n"; }
void GcodeOut::rapidXY(double x, double y) {
    os_ << "G0 X" << fmtNum(x) << " Y" << fmtNum(y) << "\n";
}
void GcodeOut::feedZ(double z, double f) {
    os_ << "G1 Z" << fmtNum(z) << " F" << fmtNum(f) << "\n";
}
void GcodeOut::feedXY(double x, double y, double f) {
    os_ << "G1 X" << fmtNum(x) << " Y" << fmtNum(y) << " F" << fmtNum(f)
        << "\n";
}

}  // namespace scnc
