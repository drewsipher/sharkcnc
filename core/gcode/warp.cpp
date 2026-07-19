#include "warp.h"

#include <cmath>
#include <set>
#include <sstream>

namespace scnc {

WarpResult warpGcode(const Program& prog, const HeightMap& hm,
                     const WarpOptions& opt) {
    WarpResult res;
    if (!hm.valid()) {
        res.error = "height map is not valid";
        return res;
    }
    if (prog.sawRelative) {
        res.error = "program uses G91 relative moves - warp requires G90";
        return res;
    }
    if (prog.sawInches) {
        res.error = "program uses G20 inches - convert to metric first";
        return res;
    }

    // index segments by source line (a line has at most one motion)
    std::ostringstream out;
    out.setf(std::ios::fixed);
    size_t segIdx = 0;

    auto emitMove = [&](const char* g, const Vec3& p, double feed, bool wantF) {
        out << g << " X" << fmtNum(p.x) << " Y" << fmtNum(p.y) << " Z"
            << fmtNum(p.z + hm.interpolate(p.x, p.y));
        if (wantF && feed > 0) out << " F" << fmtNum(feed);
        out << "\n";
    };

    for (size_t li = 0; li < prog.lines.size(); ++li) {
        const int lineNo = static_cast<int>(li + 1);
        if (segIdx < prog.segments.size() &&
            prog.segments[segIdx].line == lineNo) {
            const Segment& s = prog.segments[segIdx++];
            const bool travel =
                s.from.z >= opt.zSafeMin && s.to.z >= opt.zSafeMin;
            switch (s.type) {
                case MotionType::Rapid:
                    // rapids: apply offset at endpoint, don't split
                    emitMove("G0", s.to, 0, false);
                    break;
                case MotionType::Feed: {
                    if (travel) {
                        emitMove("G1", s.to, s.feed, true);
                        break;
                    }
                    bool first = true;
                    for (auto& p : splitLinear(s.from, s.to, opt.maxSegment)) {
                        emitMove("G1", p, s.feed, first);
                        first = false;
                    }
                    break;
                }
                case MotionType::ArcCW:
                case MotionType::ArcCCW: {
                    bool first = true;
                    Vec3 prev = s.from;
                    for (auto& p : tessellateArc(s, opt.chordTol)) {
                        for (auto& q : splitLinear(prev, p, opt.maxSegment)) {
                            emitMove("G1", q, s.feed, first);
                            first = false;
                        }
                        prev = p;
                    }
                    break;
                }
                case MotionType::Probe:
                    // pass through untouched - probing a warped file is odd,
                    // but silently altering it would be worse
                    out << prog.lines[li] << "\n";
                    break;
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
