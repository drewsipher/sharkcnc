// Tiny G-code text builder shared by the CAM generators.
#pragma once
#include <sstream>
#include <string>

namespace scnc {

class GcodeOut {
public:
    GcodeOut() { os_.setf(std::ios::fixed); }
    void comment(const std::string& s) { os_ << "; " << s << "\n"; }
    void raw(const std::string& s) { os_ << s << "\n"; }
    void header(int spindleRpm);
    void footer();
    void rapidZ(double z);
    void rapidXY(double x, double y);
    void feedZ(double z, double f);
    void feedXY(double x, double y, double f);
    std::string str() const { return os_.str(); }

private:
    std::ostringstream os_;
};

}  // namespace scnc
