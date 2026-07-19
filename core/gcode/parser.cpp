#include "parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace scnc {
namespace {

struct Word {
    char letter;
    double value;
};

// Strip ;comments and (comments), uppercase, drop spaces.
std::string cleanLine(std::string_view in) {
    std::string out;
    bool paren = false;
    for (char c : in) {
        if (c == '(') { paren = true; continue; }
        if (c == ')') { paren = false; continue; }
        if (paren) continue;
        if (c == ';') break;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::vector<Word> words(const std::string& line) {
    std::vector<Word> out;
    size_t i = 0;
    while (i < line.size()) {
        char L = line[i];
        if (L < 'A' || L > 'Z') { ++i; continue; }
        size_t j = i + 1, start = j;
        while (j < line.size() &&
               (std::isdigit(static_cast<unsigned char>(line[j])) ||
                line[j] == '.' || line[j] == '-' || line[j] == '+'))
            ++j;
        if (j > start) {
            double v = 0;
            auto res = std::from_chars(line.data() + start, line.data() + j, v);
            if (res.ec == std::errc()) out.push_back({L, v});
        }
        i = j > start ? j : i + 1;
    }
    return out;
}

}  // namespace

Program parseGcode(std::string_view text) {
    Program prog;

    // split lines, keep verbatim copies
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos)
                                    ? text.substr(pos)
                                    : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        prog.lines.emplace_back(line);
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    if (!prog.lines.empty() && prog.lines.back().empty()) prog.lines.pop_back();

    Vec3 cur{0, 0, 0};
    double unit = 1.0;        // mm; 25.4 when G20
    bool relative = false;
    int motion = 0;           // current modal motion: 0,1,2,3, 38(probe)
    double feed = 0;

    auto grow = [&](const Vec3& p) {
        prog.min.x = std::min(prog.min.x, p.x);
        prog.min.y = std::min(prog.min.y, p.y);
        prog.min.z = std::min(prog.min.z, p.z);
        prog.max.x = std::max(prog.max.x, p.x);
        prog.max.y = std::max(prog.max.y, p.y);
        prog.max.z = std::max(prog.max.z, p.z);
    };

    for (size_t li = 0; li < prog.lines.size(); ++li) {
        std::string cl = cleanLine(prog.lines[li]);
        if (cl.empty()) continue;
        // Skip grbl system lines and % program markers
        if (cl[0] == '$' || cl[0] == '%' || cl[0] == '[' || cl[0] == '<')
            continue;

        auto ws = words(cl);
        bool sawCoord = false, sawIJ = false, sawR = false;
        Vec3 target = cur;
        double I = 0, J = 0, R = 0;
        int lineMotion = -1;  // motion G on this line, else modal

        for (auto& w : ws) {
            switch (w.letter) {
                case 'G': {
                    int g = static_cast<int>(std::lround(w.value * 10));
                    switch (g) {
                        case 0: lineMotion = 0; break;
                        case 10: lineMotion = 1; break;
                        case 20: lineMotion = 2; break;
                        case 30: lineMotion = 3; break;
                        case 382: case 383: case 384: case 385:
                            lineMotion = 38; break;
                        case 200: unit = 25.4; prog.sawInches = true; break;
                        case 210: unit = 1.0; break;
                        case 900: relative = false; break;
                        case 910: relative = true; prog.sawRelative = true; break;
                        case 800: lineMotion = -2; break;  // cancel modal
                        default: break;  // planes, offsets etc: ignored
                    }
                    break;
                }
                case 'X': target.x = relative ? cur.x + w.value * unit : w.value * unit; sawCoord = true; break;
                case 'Y': target.y = relative ? cur.y + w.value * unit : w.value * unit; sawCoord = true; break;
                case 'Z': target.z = relative ? cur.z + w.value * unit : w.value * unit; sawCoord = true; break;
                case 'I': I = w.value * unit; sawIJ = true; break;
                case 'J': J = w.value * unit; sawIJ = true; break;
                case 'R': R = w.value * unit; sawR = true; break;
                case 'F': feed = w.value * unit; break;
                default: break;
            }
        }

        if (lineMotion == -2) { motion = -1; continue; }
        if (lineMotion >= 0) motion = lineMotion;
        if (!sawCoord || motion < 0) continue;

        Segment s;
        s.from = cur;
        s.to = target;
        s.feed = feed;
        s.line = static_cast<int>(li + 1);
        s.i = I; s.j = J; s.hasIJ = sawIJ;
        s.r = R; s.hasR = sawR;
        switch (motion) {
            case 0: s.type = MotionType::Rapid; break;
            case 1: s.type = MotionType::Feed; break;
            case 2: s.type = MotionType::ArcCW; break;
            case 3: s.type = MotionType::ArcCCW; break;
            case 38: s.type = MotionType::Probe; break;
            default: continue;
        }
        grow(s.from);
        grow(s.to);
        if (s.type == MotionType::ArcCW || s.type == MotionType::ArcCCW)
            for (auto& p : tessellateArc(s, 0.05)) grow(p);
        prog.segments.push_back(s);
        cur = target;
    }
    return prog;
}

std::vector<Vec3> tessellateArc(const Segment& s, double chordTol) {
    std::vector<Vec3> out;
    double cx, cy;
    if (s.hasIJ) {
        cx = s.from.x + s.i;
        cy = s.from.y + s.j;
    } else if (s.hasR) {
        // Solve center from radius (shorter arc solution, grbl semantics-ish)
        double dx = s.to.x - s.from.x, dy = s.to.y - s.from.y;
        double d = std::hypot(dx, dy);
        double r = std::abs(s.r);
        double h2 = r * r - d * d / 4.0;
        double h = h2 > 0 ? std::sqrt(h2) : 0;
        // perpendicular direction; sign by arc direction and R sign
        double sx = -dy / d, sy = dx / d;
        double sign = (s.type == MotionType::ArcCW) ? -1.0 : 1.0;
        if (s.r < 0) sign = -sign;
        cx = s.from.x + dx / 2 + sign * h * sx;
        cy = s.from.y + dy / 2 + sign * h * sy;
    } else {
        out.push_back(s.to);
        return out;
    }

    double r = std::hypot(s.from.x - cx, s.from.y - cy);
    if (r < 1e-9) { out.push_back(s.to); return out; }
    double a0 = std::atan2(s.from.y - cy, s.from.x - cx);
    double a1 = std::atan2(s.to.y - cy, s.to.x - cx);
    double sweep = a1 - a0;
    const double TAU = 2 * M_PI;
    if (s.type == MotionType::ArcCW) {           // clockwise: sweep negative
        while (sweep >= -1e-12) sweep -= TAU;
    } else {
        while (sweep <= 1e-12) sweep += TAU;
    }
    // full-circle: identical endpoints
    if (std::abs(std::abs(sweep) - TAU) < 1e-9 &&
        std::hypot(s.to.x - s.from.x, s.to.y - s.from.y) > 1e-9) {
        // endpoints differ: keep computed sweep
    }
    double maxStep = 2 * std::acos(std::clamp(1.0 - chordTol / r, -1.0, 1.0));
    if (maxStep < 1e-3) maxStep = 1e-3;
    int n = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / maxStep)));
    for (int k = 1; k <= n; ++k) {
        double a = a0 + sweep * k / n;
        double t = static_cast<double>(k) / n;
        Vec3 p{cx + r * std::cos(a), cy + r * std::sin(a),
               s.from.z + (s.to.z - s.from.z) * t};
        out.push_back(p);
    }
    out.back() = s.to;  // exact endpoint
    return out;
}

std::vector<Vec3> splitLinear(const Vec3& from, const Vec3& to, double maxLen) {
    std::vector<Vec3> out;
    double len = std::hypot(to.x - from.x, to.y - from.y);
    int n = std::max(1, static_cast<int>(std::ceil(len / maxLen)));
    for (int k = 1; k <= n; ++k) {
        double t = static_cast<double>(k) / n;
        out.push_back({from.x + (to.x - from.x) * t,
                       from.y + (to.y - from.y) * t,
                       from.z + (to.z - from.z) * t});
    }
    out.back() = to;
    return out;
}

std::string fmtNum(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

}  // namespace scnc
