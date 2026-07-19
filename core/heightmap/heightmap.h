// Probe-grid height map with bilinear interpolation, plus (de)serialisation.
// Used to warp PCB G-code onto the real, non-flat surface of the stock.
#pragma once
#include <string>
#include <vector>

namespace scnc {

class HeightMap {
public:
    HeightMap() = default;
    HeightMap(double x0, double y0, double dx, double dy, int nx, int ny);

    bool valid() const { return nx_ >= 2 && ny_ >= 2; }
    int nx() const { return nx_; }
    int ny() const { return ny_; }
    double x0() const { return x0_; }
    double y0() const { return y0_; }
    double dx() const { return dx_; }
    double dy() const { return dy_; }

    void set(int ix, int iy, double z);
    double at(int ix, int iy) const;
    bool complete() const;              // every cell probed

    // Bilinear Z offset at (x, y); clamps outside the grid.
    double interpolate(double x, double y) const;

    // Ordered serpentine probe points (row-major, alternating direction)
    // to minimise travel during the probing cycle.
    struct Point { int ix, iy; double x, y; };
    std::vector<Point> probeOrder() const;

    // Simple JSON round-trip (no external deps).
    std::string toJson() const;
    static bool fromJson(const std::string& text, HeightMap& out);

private:
    double x0_ = 0, y0_ = 0, dx_ = 1, dy_ = 1;
    int nx_ = 0, ny_ = 0;
    std::vector<double> z_;
    std::vector<char> has_;
};

}  // namespace scnc
