#include "gerber.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>

namespace scnc {

using namespace Clipper2Lib;

namespace {

constexpr double kPi = 3.14159265358979323846;

PathD circlePath(double cx, double cy, double r, int n = 48) {
    PathD p;
    p.reserve(n);
    for (int i = 0; i < n; ++i) {
        double a = 2 * kPi * i / n;
        p.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return p;
}

PathD rectPath(double cx, double cy, double w, double h, double rotDeg = 0) {
    PathD p{{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
    double c = std::cos(rotDeg * kPi / 180), s = std::sin(rotDeg * kPi / 180);
    for (auto& pt : p) {
        double x = pt.x * c - pt.y * s, y = pt.x * s + pt.y * c;
        pt = {cx + x, cy + y};
    }
    return p;
}

// stadium (obround) via inflating the centre segment
PathsD stadium(double x1, double y1, double x2, double y2, double dia) {
    PathD seg{{x1, y1}, {x2, y2}};
    return InflatePaths(PathsD{seg}, dia / 2, JoinType::Round, EndType::Round,
                        2.0, 4);
}

// --- macro expression evaluator ----------------------------------------
struct ExprCtx {
    std::map<int, double> vars;
};

class Expr {
public:
    Expr(const std::string& s, const ExprCtx& ctx) : s_(s), ctx_(ctx) {}
    double eval() {
        double v = expr();
        return v;
    }

private:
    double expr() {
        double v = term();
        while (peek() == '+' || peek() == '-') {
            char op = get();
            double r = term();
            v = op == '+' ? v + r : v - r;
        }
        return v;
    }
    double term() {
        double v = factor();
        while (peek() == 'x' || peek() == 'X' || peek() == '/') {
            char op = get();
            double r = factor();
            v = (op == '/') ? v / r : v * r;
        }
        return v;
    }
    double factor() {
        if (peek() == '-') { get(); return -factor(); }
        if (peek() == '+') { get(); return factor(); }
        if (peek() == '(') {
            get();
            double v = expr();
            if (peek() == ')') get();
            return v;
        }
        if (peek() == '$') {
            get();
            int n = 0;
            while (std::isdigit(static_cast<unsigned char>(peek())))
                n = n * 10 + (get() - '0');
            auto it = ctx_.vars.find(n);
            return it == ctx_.vars.end() ? 0.0 : it->second;
        }
        // number
        size_t start = i_;
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')
            get();
        return std::atof(s_.substr(start, i_ - start).c_str());
    }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char get() { return i_ < s_.size() ? s_[i_++] : '\0'; }

    const std::string& s_;
    const ExprCtx& ctx_;
    size_t i_ = 0;
};

double evalExpr(const std::string& s, const ExprCtx& ctx) {
    return Expr(s, ctx).eval();
}

// --- apertures ----------------------------------------------------------
struct MacroDef {
    std::vector<std::string> blocks;  // primitive/variable statements
};

struct Aperture {
    PathsD fill;   // polygons (positive area only; holes already subtracted)
};

PathsD subtractHole(PathsD shape, double holeDia) {
    if (holeDia <= 0) return shape;
    return Difference(shape, PathsD{circlePath(0, 0, holeDia / 2)},
                      FillRule::NonZero);
}

Aperture buildStandard(char type, const std::vector<double>& p) {
    Aperture a;
    switch (type) {
        case 'C': {
            double d = p.size() > 0 ? p[0] : 0;
            a.fill = subtractHole(PathsD{circlePath(0, 0, d / 2)},
                                  p.size() > 1 ? p[1] : 0);
            break;
        }
        case 'R': {
            double w = p.size() > 0 ? p[0] : 0, h = p.size() > 1 ? p[1] : w;
            a.fill = subtractHole(PathsD{rectPath(0, 0, w, h)},
                                  p.size() > 2 ? p[2] : 0);
            break;
        }
        case 'O': {
            double w = p.size() > 0 ? p[0] : 0, h = p.size() > 1 ? p[1] : w;
            PathsD s;
            if (w >= h)
                s = stadium(-(w - h) / 2, 0, (w - h) / 2, 0, h);
            else
                s = stadium(0, -(h - w) / 2, 0, (h - w) / 2, w);
            a.fill = subtractHole(std::move(s), p.size() > 2 ? p[2] : 0);
            break;
        }
        case 'P': {
            double od = p.size() > 0 ? p[0] : 0;
            int n = p.size() > 1 ? static_cast<int>(p[1]) : 3;
            double rot = p.size() > 2 ? p[2] : 0;
            PathD poly;
            for (int i = 0; i < n; ++i) {
                double ang = rot * kPi / 180 + 2 * kPi * i / n;
                poly.push_back({od / 2 * std::cos(ang),
                                od / 2 * std::sin(ang)});
            }
            a.fill = subtractHole(PathsD{poly}, p.size() > 3 ? p[3] : 0);
            break;
        }
        default: break;
    }
    return a;
}

void rotatePaths(PathsD& ps, double deg) {
    if (deg == 0) return;
    double c = std::cos(deg * kPi / 180), s = std::sin(deg * kPi / 180);
    for (auto& path : ps)
        for (auto& pt : path) {
            double x = pt.x * c - pt.y * s, y = pt.x * s + pt.y * c;
            pt = {x, y};
        }
}

Aperture buildMacro(const MacroDef& def, const std::vector<double>& params,
                    std::vector<std::string>& warnings) {
    ExprCtx ctx;
    for (size_t i = 0; i < params.size(); ++i)
        ctx.vars[static_cast<int>(i + 1)] = params[i];

    PathsD acc;
    for (const std::string& blk : def.blocks) {
        if (blk.empty()) continue;
        if (blk[0] == '$') {  // variable assignment $n=expr
            auto eq = blk.find('=');
            if (eq == std::string::npos) continue;
            int n = std::atoi(blk.c_str() + 1);
            ctx.vars[n] = evalExpr(blk.substr(eq + 1), ctx);
            continue;
        }
        if (blk[0] == '0') continue;  // comment primitive
        // primitive: code,mod,mod,...
        std::vector<std::string> mods;
        std::stringstream ss(blk);
        std::string item;
        while (std::getline(ss, item, ',')) mods.push_back(item);
        if (mods.empty()) continue;
        int code = std::atoi(mods[0].c_str());
        auto M = [&](size_t i) {
            return i < mods.size() ? evalExpr(mods[i], ctx) : 0.0;
        };
        PathsD prim;
        bool exposure = true;
        switch (code) {
            case 1: {  // circle: exp, dia, cx, cy [, rot]
                exposure = M(1) != 0;
                double dia = M(2), cx = M(3), cy = M(4), rot = M(5);
                prim = PathsD{circlePath(cx, cy, dia / 2)};
                rotatePaths(prim, rot);
                break;
            }
            case 20: {  // vector line: exp,w,x1,y1,x2,y2,rot
                exposure = M(1) != 0;
                double w = M(2);
                PathD quad;
                double x1 = M(3), y1 = M(4), x2 = M(5), y2 = M(6);
                double dx = x2 - x1, dy = y2 - y1;
                double len = std::hypot(dx, dy);
                if (len < 1e-12) break;
                double nx = -dy / len * w / 2, ny = dx / len * w / 2;
                quad = {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny},
                        {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}};
                prim = PathsD{quad};
                rotatePaths(prim, M(7));
                break;
            }
            case 21: {  // centre line (rect): exp,w,h,cx,cy,rot
                exposure = M(1) != 0;
                prim = PathsD{rectPath(M(4), M(5), M(2), M(3))};
                rotatePaths(prim, M(6));
                break;
            }
            case 4: {  // outline: exp,n,x0,y0,...,xn,yn,rot
                exposure = M(1) != 0;
                int n = static_cast<int>(M(2));
                PathD poly;
                for (int i = 0; i <= n; ++i)
                    poly.push_back({M(3 + 2 * i), M(4 + 2 * i)});
                if (!poly.empty()) poly.pop_back();  // last repeats first
                prim = PathsD{poly};
                rotatePaths(prim, M(3 + 2 * (n + 1)));
                break;
            }
            case 5: {  // polygon: exp,n,cx,cy,dia,rot
                exposure = M(1) != 0;
                int n = static_cast<int>(M(2));
                double cx = M(3), cy = M(4), dia = M(5), rot = M(6);
                PathD poly;
                for (int i = 0; i < n; ++i) {
                    double a = rot * kPi / 180 + 2 * kPi * i / n;
                    poly.push_back({cx + dia / 2 * std::cos(a),
                                    cy + dia / 2 * std::sin(a)});
                }
                prim = PathsD{poly};
                break;
            }
            case 7: {  // thermal: cx,cy,outer,inner,gap,rot - approximate
                double cx = M(1), cy = M(2), od = M(3), id = M(4);
                prim = Difference(PathsD{circlePath(cx, cy, od / 2)},
                                  PathsD{circlePath(cx, cy, id / 2)},
                                  FillRule::NonZero);
                rotatePaths(prim, M(6));
                warnings.push_back("thermal primitive approximated (no gaps)");
                break;
            }
            default:
                warnings.push_back("unsupported macro primitive " +
                                   std::to_string(code));
                continue;
        }
        if (prim.empty()) continue;
        acc = exposure ? Union(acc, prim, FillRule::NonZero)
                       : Difference(acc, prim, FillRule::NonZero);
    }
    Aperture a;
    a.fill = std::move(acc);
    return a;
}

PathsD translated(const PathsD& ps, double x, double y) {
    PathsD out = ps;
    for (auto& p : out)
        for (auto& pt : p) pt = {pt.x + x, pt.y + y};
    return out;
}

}  // namespace

GerberResult parseGerber(const std::string& text) {
    GerberResult res;
    GerberLayer& L = res.layer;

    double unit = 1.0;          // mm multiplier (25.4 for inches)
    int intDigits = 3, decDigits = 6;
    bool leadingZeroOmit = true;  // FSLA
    std::map<int, Aperture> apertures;
    std::map<std::string, MacroDef> macros;
    int currentAp = -1;

    bool inRegion = false;
    PathD regionPath;
    PathsD regionAcc;

    bool dark = true;            // LPD
    PathsD darkAcc, result;
    auto flushPolarity = [&](bool newDark) {
        if (dark == newDark) return;
        if (!darkAcc.empty()) {
            result = dark ? Union(result, darkAcc, FillRule::NonZero)
                          : Difference(result, darkAcc, FillRule::NonZero);
            darkAcc.clear();
        }
        dark = newDark;
    };

    double cx = 0, cy = 0;       // current point (mm)
    int interp = 1;              // 1 linear, 2 cw, 3 ccw
    bool multiQuadrant = false;

    auto coordVal = [&](const std::string& digits) {
        // signed fixed-format number -> mm
        bool neg = false;
        std::string d = digits;
        if (!d.empty() && (d[0] == '-' || d[0] == '+')) {
            neg = d[0] == '-';
            d.erase(0, 1);
        }
        double v;
        if (d.find('.') != std::string::npos) {
            v = std::atof(d.c_str());
        } else {
            int total = intDigits + decDigits;
            if (leadingZeroOmit) {
                while (static_cast<int>(d.size()) < total) d.insert(0, "0");
            } else {
                while (static_cast<int>(d.size()) < total) d.push_back('0');
            }
            v = std::atof(d.c_str()) / std::pow(10.0, decDigits);
        }
        return (neg ? -v : v) * unit;
    };

    auto strokeTo = [&](double nx, double ny, double ci, double cj) {
        auto it = apertures.find(currentAp);
        if (it == apertures.end()) return;
        // aperture size estimate: use bounding box width for stroke dia
        auto rect = GetBounds(it->second.fill);
        double dia = std::max(rect.Width(), rect.Height());
        if (dia <= 0) dia = 0.01;
        if (interp == 1) {
            auto s = stadium(cx, cy, nx, ny, dia);
            darkAcc.insert(darkAcc.end(), s.begin(), s.end());
        } else {
            // arc stroke: tessellate then inflate polyline
            double ccx = cx + ci, ccy = cy + cj;
            double r = std::hypot(cx - ccx, cy - ccy);
            double a0 = std::atan2(cy - ccy, cx - ccx);
            double a1 = std::atan2(ny - ccy, nx - ccx);
            double sweep = a1 - a0;
            if (interp == 2) { while (sweep >= -1e-12) sweep -= 2 * kPi; }
            else            { while (sweep <=  1e-12) sweep += 2 * kPi; }
            if (!multiQuadrant && std::abs(sweep) > kPi / 2 + 1e-6) {
                // single-quadrant mode: pick <=90 deg solution
                // (approximation: clamp; KiCad emits G75 so this is rare)
            }
            int n = std::max(4, static_cast<int>(std::abs(sweep) / 0.1));
            PathD arc;
            arc.push_back({cx, cy});
            for (int k = 1; k <= n; ++k) {
                double a = a0 + sweep * k / n;
                arc.push_back({ccx + r * std::cos(a), ccy + r * std::sin(a)});
            }
            auto s = InflatePaths(PathsD{arc}, dia / 2, JoinType::Round,
                                  EndType::Round, 2.0, 4);
            darkAcc.insert(darkAcc.end(), s.begin(), s.end());
        }
        ++L.strokes;
    };

    // token scan: statements end with '*'; extended commands in %...%
    size_t i = 0;
    const size_t N = text.size();
    while (i < N) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (c == '%') {  // extended command block
            size_t end = text.find('%', i + 1);
            if (end == std::string::npos) break;
            std::string block = text.substr(i + 1, end - i - 1);
            i = end + 1;
            // split into *-terminated statements
            std::vector<std::string> stmts;
            {
                std::string cur;
                for (char bc : block) {
                    if (bc == '*') { stmts.push_back(cur); cur.clear(); }
                    else if (bc != '\n' && bc != '\r') cur.push_back(bc);
                }
            }
            if (stmts.empty()) continue;
            const std::string& s0 = stmts[0];
            if (s0.starts_with("FS")) {
                auto x = s0.find('X');
                if (x != std::string::npos && x + 2 < s0.size()) {
                    intDigits = s0[x + 1] - '0';
                    decDigits = s0[x + 2] - '0';
                }
                leadingZeroOmit = s0.find('L') != std::string::npos;
            } else if (s0.starts_with("MO")) {
                unit = s0.find("IN") != std::string::npos ? 25.4 : 1.0;
            } else if (s0.starts_with("LP")) {
                flushPolarity(s0.size() > 2 && s0[2] == 'D');
            } else if (s0.starts_with("AM")) {
                MacroDef def;
                std::string name = s0.substr(2);
                for (size_t k = 1; k < stmts.size(); ++k)
                    def.blocks.push_back(stmts[k]);
                macros[name] = def;
            } else if (s0.starts_with("AD")) {
                // ADD<code><type>,<params>
                size_t p = 3;
                int code = 0;
                while (p < s0.size() &&
                       std::isdigit(static_cast<unsigned char>(s0[p])))
                    code = code * 10 + (s0[p++] - '0');
                std::string typeName;
                while (p < s0.size() && s0[p] != ',') typeName.push_back(s0[p++]);
                std::vector<double> params;
                if (p < s0.size() && s0[p] == ',') {
                    ++p;
                    std::stringstream ss(s0.substr(p));
                    std::string item;
                    while (std::getline(ss, item, 'X'))
                        params.push_back(std::atof(item.c_str()) * unit);
                }
                Aperture ap;
                if (typeName.size() == 1 &&
                    std::strchr("CROP", typeName[0])) {
                    ap = buildStandard(typeName[0], params);
                } else if (macros.count(typeName)) {
                    // macro params are in units already? spec: units apply
                    ap = buildMacro(macros[typeName], params, L.warnings);
                } else {
                    L.warnings.push_back("unknown aperture type " + typeName);
                }
                apertures[code] = std::move(ap);
                ++L.apertures;
            }
            // LN, TF, TA, TO, SR(1,1): ignored
            continue;
        }

        // normal statement: read until '*'
        size_t end = text.find('*', i);
        if (end == std::string::npos) break;
        std::string st;
        for (size_t k = i; k < end; ++k)
            if (!std::isspace(static_cast<unsigned char>(text[k])))
                st.push_back(text[k]);
        i = end + 1;
        if (st.empty()) continue;

        if (st == "G36") { inRegion = true; regionPath.clear(); continue; }
        if (st == "G37") {
            inRegion = false;
            if (regionPath.size() >= 3) {
                darkAcc.push_back(regionPath);
                ++L.regions;
            }
            regionPath.clear();
            continue;
        }
        if (st == "G74") { multiQuadrant = false; continue; }
        if (st == "G75") { multiQuadrant = true; continue; }
        if (st == "M02" || st == "M00") break;
        if (st.starts_with("G04")) continue;  // comment

        // coordinate / operation statement, may start with G01/G02/G03
        size_t p = 0;
        std::optional<double> X, Y, I, J;
        int dcode = 0;
        while (p < st.size()) {
            char L2 = st[p];
            if (L2 == 'G') {
                int g = std::atoi(st.c_str() + p + 1);
                if (g == 1) interp = 1;
                else if (g == 2) interp = 2;
                else if (g == 3) interp = 3;
                p += 2;
                while (p < st.size() &&
                       std::isdigit(static_cast<unsigned char>(st[p])))
                    ++p;
                continue;
            }
            if (L2 == 'D') {
                dcode = std::atoi(st.c_str() + p + 1);
                break;  // D is always last
            }
            if (L2 == 'X' || L2 == 'Y' || L2 == 'I' || L2 == 'J') {
                size_t q = p + 1, start = q;
                while (q < st.size() &&
                       (std::isdigit(static_cast<unsigned char>(st[q])) ||
                        st[q] == '-' || st[q] == '+' || st[q] == '.'))
                    ++q;
                double v = coordVal(st.substr(start, q - start));
                if (L2 == 'X') X = v;
                else if (L2 == 'Y') Y = v;
                else if (L2 == 'I') I = v;
                else J = v;
                p = q;
                continue;
            }
            ++p;
        }

        if (dcode >= 10) { currentAp = dcode; continue; }

        double nx = X.value_or(cx), ny = Y.value_or(cy);
        switch (dcode) {
            case 1:  // draw
                if (inRegion) {
                    if (regionPath.empty()) regionPath.push_back({cx, cy});
                    if (interp == 1) {
                        regionPath.push_back({nx, ny});
                    } else {
                        double ccx = cx + I.value_or(0), ccy = cy + J.value_or(0);
                        double r = std::hypot(cx - ccx, cy - ccy);
                        double a0 = std::atan2(cy - ccy, cx - ccx);
                        double a1 = std::atan2(ny - ccy, nx - ccx);
                        double sweep = a1 - a0;
                        if (interp == 2) { while (sweep >= -1e-12) sweep -= 2 * kPi; }
                        else             { while (sweep <=  1e-12) sweep += 2 * kPi; }
                        int n = std::max(4, static_cast<int>(std::abs(sweep) / 0.1));
                        for (int k = 1; k <= n; ++k) {
                            double a = a0 + sweep * k / n;
                            regionPath.push_back(
                                {ccx + r * std::cos(a), ccy + r * std::sin(a)});
                        }
                    }
                } else {
                    strokeTo(nx, ny, I.value_or(0), J.value_or(0));
                }
                cx = nx; cy = ny;
                break;
            case 2:  // move
                if (inRegion && regionPath.size() >= 3) {
                    darkAcc.push_back(regionPath);
                    ++L.regions;
                    regionPath.clear();
                }
                cx = nx; cy = ny;
                break;
            case 3: {  // flash
                auto it = apertures.find(currentAp);
                if (it != apertures.end()) {
                    auto f = translated(it->second.fill, nx, ny);
                    darkAcc.insert(darkAcc.end(), f.begin(), f.end());
                    ++L.flashes;
                }
                cx = nx; cy = ny;
                break;
            }
            default:
                cx = nx; cy = ny;
                break;
        }
    }

    // final polarity flush + union
    if (!darkAcc.empty()) {
        result = dark ? Union(result, darkAcc, FillRule::NonZero)
                      : Difference(result, darkAcc, FillRule::NonZero);
    }
    L.copper = SimplifyPaths(result, 0.001);
    if (!L.copper.empty()) {
        auto b = GetBounds(L.copper);
        L.minX = b.left; L.minY = b.top;   // note: RectD top/bottom naming
        L.maxX = b.right; L.maxY = b.bottom;
        if (L.minY > L.maxY) std::swap(L.minY, L.maxY);
    }
    res.ok = true;
    return res;
}

}  // namespace scnc
