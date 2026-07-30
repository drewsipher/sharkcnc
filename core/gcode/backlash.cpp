#include "backlash.h"

#include <cmath>
#include <sstream>

namespace scnc {

namespace {

int dirOf(double delta) {
    if (delta > 1e-9) return 1;
    if (delta < -1e-9) return -1;
    return 0;
}

}  // namespace

BacklashResult applyBacklash(const Program& prog, const BacklashOptions& opt) {
    BacklashResult res;
    if (!opt.enabled()) {
        res.error = "all backlash values are zero";
        return res;
    }
    if (prog.sawRelative) {
        res.error = "program uses G91 relative moves - comp requires G90";
        return res;
    }
    if (prog.sawInches) {
        res.error = "program uses G20 inches - convert to metric first";
        return res;
    }

    std::ostringstream out;
    out.setf(std::ios::fixed);

    const double bl[3] = {opt.x, opt.y, opt.z};
    int lastDir[3] = {0, 0, 0};   // per-axis last motion direction
    double off[3] = {0, 0, 0};    // current coordinate shift
    // real position of the previous segment end (program coords)
    double cur[3] = {0, 0, 0};
    bool haveCur = false;

    // Track reversals for a move from cur to 'to'; returns true when a
    // takeup is needed and updates off/lastDir. The takeup target is the
    // CURRENT position under the NEW offsets (pure slack winding).
    auto reversalCheck = [&](const double to[3]) {
        bool need = false;
        for (int a = 0; a < 3; ++a) {
            int d = dirOf(to[a] - cur[a]);
            if (d == 0) continue;
            if (lastDir[a] != 0 && d != lastDir[a] && bl[a] > 0) {
                off[a] += d * bl[a];
                need = true;
            }
            lastDir[a] = d;
        }
        return need;
    };

    auto emitTakeup = [&](bool rapid) {
        out << (rapid ? "G0" : "G1") << " X" << fmtNum(cur[0] + off[0])
            << " Y" << fmtNum(cur[1] + off[1]) << " Z"
            << fmtNum(cur[2] + off[2]);
        if (!rapid) out << " F" << fmtNum(opt.takeupFeed);
        out << " ; backlash takeup\n";
        ++res.takeups;
    };

    auto emitMove = [&](const char* g, const double to[3], double feed) {
        out << g << " X" << fmtNum(to[0] + off[0]) << " Y"
            << fmtNum(to[1] + off[1]) << " Z" << fmtNum(to[2] + off[2]);
        if (feed > 0) out << " F" << fmtNum(feed);
        out << "\n";
        cur[0] = to[0]; cur[1] = to[1]; cur[2] = to[2];
    };

    auto handleLinear = [&](const Segment& s, bool rapid) {
        if (!haveCur) {
            cur[0] = s.from.x; cur[1] = s.from.y; cur[2] = s.from.z;
            haveCur = true;
        }
        double to[3] = {s.to.x, s.to.y, s.to.z};
        if (reversalCheck(to)) emitTakeup(rapid);
        emitMove(rapid ? "G0" : "G1", to, rapid ? 0 : s.feed);
    };

    size_t segIdx = 0;
    for (size_t li = 0; li < prog.lines.size(); ++li) {
        const int lineNo = static_cast<int>(li + 1);
        if (segIdx < prog.segments.size() &&
            prog.segments[segIdx].line == lineNo) {
            const Segment& s = prog.segments[segIdx++];
            switch (s.type) {
                case MotionType::Rapid:
                    handleLinear(s, true);
                    break;
                case MotionType::Feed:
                    handleLinear(s, false);
                    break;
                case MotionType::ArcCW:
                case MotionType::ArcCCW: {
                    if (!haveCur) {
                        cur[0] = s.from.x; cur[1] = s.from.y;
                        cur[2] = s.from.z;
                        haveCur = true;
                    }
                    // tessellate so quadrant reversals inside the arc
                    // get their takeups too; restate F after any takeup
                    // (the takeup changed the modal feed)
                    bool wantF = true;
                    for (auto& p : tessellateArc(s, opt.chordTol)) {
                        double to[3] = {p.x, p.y, p.z};
                        if (reversalCheck(to)) { emitTakeup(false); wantF = true; }
                        emitMove("G1", to, wantF ? s.feed : 0);
                        wantF = false;
                    }
                    break;
                }
                case MotionType::Probe: {
                    if (!haveCur) {
                        cur[0] = s.from.x; cur[1] = s.from.y;
                        cur[2] = s.from.z;
                        haveCur = true;
                    }
                    // wind the slack before probing (a probe is usually a
                    // Z reversal after a retract), then pass the original
                    // probe line through untouched: comp on the probe
                    // itself would falsify the reported trigger position.
                    double to[3] = {s.to.x, s.to.y, s.to.z};
                    if (reversalCheck(to)) emitTakeup(false);
                    out << prog.lines[li] << "\n";
                    cur[0] = s.to.x; cur[1] = s.to.y; cur[2] = s.to.z;
                    break;
                }
            }
        } else {
            out << prog.lines[li] << "\n";
        }
    }
    res.ok = true;
    res.gcode = out.str();
    return res;
}

}  // namespace scnc
