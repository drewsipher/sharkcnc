#include "heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "../gcode/parser.h"  // fmtNum

namespace scnc {

HeightMap::HeightMap(double x0, double y0, double dx, double dy, int nx, int ny)
    : x0_(x0), y0_(y0), dx_(dx), dy_(dy), nx_(nx), ny_(ny),
      z_(static_cast<size_t>(nx) * ny, 0.0),
      has_(static_cast<size_t>(nx) * ny, 0) {}

void HeightMap::set(int ix, int iy, double z) {
    if (ix < 0 || iy < 0 || ix >= nx_ || iy >= ny_) return;
    z_[static_cast<size_t>(iy) * nx_ + ix] = z;
    has_[static_cast<size_t>(iy) * nx_ + ix] = 1;
}

double HeightMap::at(int ix, int iy) const {
    ix = std::clamp(ix, 0, nx_ - 1);
    iy = std::clamp(iy, 0, ny_ - 1);
    return z_[static_cast<size_t>(iy) * nx_ + ix];
}

bool HeightMap::complete() const {
    return valid() &&
           std::all_of(has_.begin(), has_.end(), [](char c) { return c; });
}

double HeightMap::interpolate(double x, double y) const {
    if (!valid()) return 0.0;
    double fx = (x - x0_) / dx_;
    double fy = (y - y0_) / dy_;
    fx = std::clamp(fx, 0.0, static_cast<double>(nx_ - 1));
    fy = std::clamp(fy, 0.0, static_cast<double>(ny_ - 1));
    int ix = std::min(static_cast<int>(fx), nx_ - 2);
    int iy = std::min(static_cast<int>(fy), ny_ - 2);
    double tx = fx - ix, ty = fy - iy;
    double z00 = at(ix, iy), z10 = at(ix + 1, iy);
    double z01 = at(ix, iy + 1), z11 = at(ix + 1, iy + 1);
    return z00 * (1 - tx) * (1 - ty) + z10 * tx * (1 - ty) +
           z01 * (1 - tx) * ty + z11 * tx * ty;
}

std::vector<HeightMap::Point> HeightMap::probeOrder() const {
    std::vector<Point> out;
    for (int iy = 0; iy < ny_; ++iy) {
        if (iy % 2 == 0)
            for (int ix = 0; ix < nx_; ++ix)
                out.push_back({ix, iy, x0_ + ix * dx_, y0_ + iy * dy_});
        else
            for (int ix = nx_ - 1; ix >= 0; --ix)
                out.push_back({ix, iy, x0_ + ix * dx_, y0_ + iy * dy_});
    }
    return out;
}

std::string HeightMap::toJson() const {
    std::ostringstream os;
    os << "{\"x0\":" << fmtNum(x0_) << ",\"y0\":" << fmtNum(y0_)
       << ",\"dx\":" << fmtNum(dx_) << ",\"dy\":" << fmtNum(dy_)
       << ",\"nx\":" << nx_ << ",\"ny\":" << ny_ << ",\"z\":[";
    for (size_t i = 0; i < z_.size(); ++i) {
        if (i) os << ',';
        os << fmtNum(z_[i]);
    }
    os << "]}";
    return os.str();
}

bool HeightMap::fromJson(const std::string& text, HeightMap& out) {
    auto num = [&](const char* key, double& v) {
        auto p = text.find(std::string("\"") + key + "\":");
        if (p == std::string::npos) return false;
        v = std::strtod(text.c_str() + p + std::strlen(key) + 3, nullptr);
        return true;
    };
    double x0, y0, dx, dy, nxd, nyd;
    if (!num("x0", x0) || !num("y0", y0) || !num("dx", dx) ||
        !num("dy", dy) || !num("nx", nxd) || !num("ny", nyd))
        return false;
    int nx = static_cast<int>(nxd), ny = static_cast<int>(nyd);
    if (nx < 2 || ny < 2 || nx * ny > 100000) return false;
    out = HeightMap(x0, y0, dx, dy, nx, ny);
    auto p = text.find("\"z\":[");
    if (p == std::string::npos) return false;
    const char* c = text.c_str() + p + 5;
    for (int iy = 0; iy < ny; ++iy)
        for (int ix = 0; ix < nx; ++ix) {
            char* end = nullptr;
            double z = std::strtod(c, &end);
            if (end == c) return false;
            out.set(ix, iy, z);
            c = end;
            while (*c == ',' || *c == ' ') ++c;
        }
    return true;
}

}  // namespace scnc
