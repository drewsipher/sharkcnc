#include "resume.h"

#include <cctype>

#include "parser.h"  // fmtNum

namespace scnc {

namespace {

// Uppercased, comment-stripped, whitespace-free copy for word scanning.
std::string clean(const std::string& in) {
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

bool hasWord(const std::string& s, const std::string& w) {
    auto p = s.find(w);
    if (p == std::string::npos) return false;
    // ensure the char after the word isn't a digit continuation (G9 vs G90)
    size_t e = p + w.size();
    if (e < s.size() && std::isdigit(static_cast<unsigned char>(s[e])))
        return false;
    return true;
}

}  // namespace

std::vector<std::string> buildResumeJob(const std::vector<std::string>& lines,
                                        size_t fromLine, double safeZ) {
    std::vector<std::string> out;
    if (fromLine >= lines.size()) return out;

    std::string units = "G21", distance = "G90", spindle;
    for (size_t i = 0; i < fromLine && i < lines.size(); ++i) {
        std::string c = clean(lines[i]);
        if (hasWord(c, "G20")) units = "G20";
        if (hasWord(c, "G21")) units = "G21";
        if (hasWord(c, "G90")) distance = "G90";
        if (hasWord(c, "G91")) distance = "G91";
        // spindle: capture the whole M3/M4 [Sn] intent
        if (hasWord(c, "M3") || hasWord(c, "M4")) {
            std::string s = hasWord(c, "M4") ? "M4" : "M3";
            auto sp = c.find('S');
            if (sp != std::string::npos) {
                std::string num;
                for (size_t k = sp + 1;
                     k < c.size() && (std::isdigit((unsigned char)c[k]) ||
                                      c[k] == '.');
                     ++k)
                    num += c[k];
                if (!num.empty()) s += " S" + num;
            }
            spindle = s;
        }
        if (hasWord(c, "M5")) spindle.clear();
    }

    out.push_back("; --- sharkcnc resume from line " +
                  std::to_string(fromLine + 1) +
                  " (re-home & re-zero first) ---");
    out.push_back(units);
    out.push_back(distance);
    if (!spindle.empty()) out.push_back(spindle);
    out.push_back("G0 Z" + fmtNum(safeZ));  // lift before repositioning
    for (size_t i = fromLine; i < lines.size(); ++i)
        out.push_back(lines[i]);
    return out;
}

}  // namespace scnc
