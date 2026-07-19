#include "excellon.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>

namespace scnc {

DrillFile parseExcellon(const std::string& text) {
    DrillFile out;
    double unit = 1.0;          // mm
    bool inHeader = false;
    bool leadingZeroOmit = true;
    int intDigits = 3, decDigits = 3;
    int curTool = 0;

    auto coordVal = [&](const std::string& digits) {
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
            if (leadingZeroOmit)
                while (static_cast<int>(d.size()) < total) d.insert(0, "0");
            else
                while (static_cast<int>(d.size()) < total) d.push_back('0');
            v = std::atof(d.c_str()) / std::pow(10.0, decDigits);
        }
        return (neg ? -v : v) * unit;
    };

    std::istringstream ss(text);
    std::string raw;
    while (std::getline(ss, raw)) {
        std::string line;
        for (char c : raw)
            if (!std::isspace(static_cast<unsigned char>(c)))
                line.push_back(static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c))));
        if (line.empty() || line[0] == ';') continue;

        if (line == "M48") { inHeader = true; continue; }
        if (line == "%" || line == "M95") { inHeader = false; continue; }
        if (line.starts_with("METRIC")) {
            unit = 1.0;
            if (line.find("TZ") != std::string::npos) leadingZeroOmit = false;
            continue;
        }
        if (line.starts_with("INCH")) {
            unit = 25.4;
            intDigits = 2; decDigits = 4;
            if (line.find("TZ") != std::string::npos) leadingZeroOmit = false;
            continue;
        }
        if (line == "M30" || line == "M00") break;
        if (line.starts_with("G") || line.starts_with("FMAT")) continue;

        if (line[0] == 'T') {
            int t = std::atoi(line.c_str() + 1);
            auto cpos = line.find('C');
            if (inHeader && cpos != std::string::npos) {
                DrillTool tool;
                tool.number = t;
                tool.diameter = std::atof(line.c_str() + cpos + 1) * unit;
                out.tools.push_back(tool);
            } else if (t > 0) {
                curTool = t;
            }
            continue;
        }

        if (line[0] == 'X' || line[0] == 'Y') {
            double x = 0, y = 0;
            bool hx = false, hy = false;
            size_t p = 0;
            while (p < line.size()) {
                char L = line[p];
                if (L == 'X' || L == 'Y') {
                    size_t q = p + 1, start = q;
                    while (q < line.size() &&
                           (std::isdigit(static_cast<unsigned char>(line[q])) ||
                            line[q] == '-' || line[q] == '+' || line[q] == '.'))
                        ++q;
                    double v = coordVal(line.substr(start, q - start));
                    if (L == 'X') { x = v; hx = true; }
                    else { y = v; hy = true; }
                    p = q;
                } else {
                    ++p;
                }
            }
            if (hx || hy) {
                DrillHit h;
                h.x = x; h.y = y; h.tool = curTool;
                out.hits.push_back(h);
                for (auto& t : out.tools)
                    if (t.number == curTool) ++t.count;
            }
        }
    }
    std::sort(out.tools.begin(), out.tools.end(),
              [](auto& a, auto& b) { return a.diameter < b.diameter; });
    out.ok = true;
    return out;
}

}  // namespace scnc
