// Excellon drill file parser (KiCad-style decimal or fixed formats).
#pragma once
#include <string>
#include <vector>

namespace scnc {

struct DrillHit {
    double x = 0, y = 0;   // mm
    int tool = 0;
};

struct DrillTool {
    int number = 0;
    double diameter = 0;   // mm
    int count = 0;
};

struct DrillFile {
    bool ok = false;
    std::string error;
    std::vector<DrillTool> tools;
    std::vector<DrillHit> hits;
};

DrillFile parseExcellon(const std::string& text);

}  // namespace scnc
