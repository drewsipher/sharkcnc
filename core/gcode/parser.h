// G-code parsing for preview, height-map warping and job analysis.
// Streaming to the controller always sends the original file lines;
// this model exists so the app can see and transform what will happen.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scnc {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
};

enum class MotionType { Rapid, Feed, ArcCW, ArcCCW, Probe };

struct Segment {
    MotionType type = MotionType::Rapid;
    Vec3 from;              // mm, program coordinates
    Vec3 to;
    double i = 0, j = 0;    // arc center offsets from 'from' (mm, XY plane)
    bool hasIJ = false;
    double r = 0;           // arc radius alternative
    bool hasR = false;
    double feed = 0;        // mm/min in effect
    int line = 0;           // 1-based source line
};

struct Program {
    std::vector<Segment> segments;
    std::vector<std::string> lines;   // original file lines (verbatim)
    bool sawInches = false;           // G20 seen anywhere
    bool sawRelative = false;         // G91 seen anywhere
    Vec3 min{1e18, 1e18, 1e18}, max{-1e18, -1e18, -1e18};  // motion bounds
    bool hasBounds() const { return min.x <= max.x; }
};

// Parse a whole file. Unknown words are ignored (they stream fine); this
// only models motion. Coordinates are normalised to millimetres.
Program parseGcode(std::string_view text);

// Tessellate an arc segment into short chords (<= chordTol deviation).
// Returns intermediate points excluding 'from', including 'to'.
std::vector<Vec3> tessellateArc(const Segment& s, double chordTol = 0.01);

// Split one linear move into pieces no longer than maxLen (XY length).
std::vector<Vec3> splitLinear(const Vec3& from, const Vec3& to, double maxLen);

// Format a double the way controllers like: fixed, trimmed zeros.
std::string fmtNum(double v);

}  // namespace scnc
