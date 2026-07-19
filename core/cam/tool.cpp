#include "tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "../gcode/parser.h"  // fmtNum

namespace scnc {

const char* toString(ToolType t) {
    switch (t) {
        case ToolType::EndMill: return "endmill";
        case ToolType::BallNose: return "ballnose";
        case ToolType::BullNose: return "bullnose";
        case ToolType::VBit: return "vbit";
        case ToolType::Chamfer: return "chamfer";
        case ToolType::Drill: return "drill";
        case ToolType::Engraver: return "engraver";
    }
    return "endmill";
}

ToolType toolTypeFromString(const std::string& s) {
    if (s == "ballnose") return ToolType::BallNose;
    if (s == "bullnose") return ToolType::BullNose;
    if (s == "vbit") return ToolType::VBit;
    if (s == "chamfer") return ToolType::Chamfer;
    if (s == "drill") return ToolType::Drill;
    if (s == "engraver") return ToolType::Engraver;
    return ToolType::EndMill;
}

double Tool::widthAtDepth(double depth) const {
    depth = std::max(0.0, depth);
    switch (type) {
        case ToolType::VBit:
        case ToolType::Chamfer:
        case ToolType::Engraver: {
            // cone: width = 2 * depth * tan(halfAngle), capped at the tool's
            // full cutting diameter (plus any flat tip is ignored — small)
            double half = tipAngle * 0.5 * M_PI / 180.0;
            double w = 2.0 * depth * std::tan(half);
            return std::min(w, diameter > 0 ? diameter : w);
        }
        case ToolType::BallNose: {
            // width of the spherical tip at depth d (<= radius)
            double r = diameter / 2.0;
            if (depth >= r) return diameter;
            double w = 2.0 * std::sqrt(std::max(0.0, r * r - (r - depth) * (r - depth)));
            return w;
        }
        case ToolType::EndMill:
        case ToolType::BullNose:
        case ToolType::Drill:
        default:
            return diameter;
    }
}

std::string Tool::summary() const {
    std::ostringstream os;
    os << name << "  (" << toString(type) << ", ";
    if (type == ToolType::VBit || type == ToolType::Chamfer ||
        type == ToolType::Engraver)
        os << fmtNum(tipAngle) << "\xC2\xB0 tip, " << fmtNum(diameter) << "mm";
    else
        os << fmtNum(diameter) << "mm";
    if (flutes > 0) os << ", " << flutes << "fl";
    os << ")";
    return os.str();
}

int ToolLibrary::add(Tool t) {
    t.id = nextId_++;
    tools_.push_back(std::move(t));
    return tools_.back().id;
}

bool ToolLibrary::remove(int id) {
    auto it = std::remove_if(tools_.begin(), tools_.end(),
                             [id](const Tool& t) { return t.id == id; });
    if (it == tools_.end()) return false;
    tools_.erase(it, tools_.end());
    return true;
}

Tool* ToolLibrary::find(int id) {
    for (auto& t : tools_)
        if (t.id == id) return &t;
    return nullptr;
}
const Tool* ToolLibrary::find(int id) const {
    for (auto& t : tools_)
        if (t.id == id) return &t;
    return nullptr;
}

namespace {
std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}
// minimal value extractors keyed within one object's text span
double numField(const std::string& obj, const char* key, double dflt) {
    auto p = obj.find(std::string("\"") + key + "\"");
    if (p == std::string::npos) return dflt;
    p = obj.find(':', p);
    if (p == std::string::npos) return dflt;
    return std::strtod(obj.c_str() + p + 1, nullptr);
}
std::string strField(const std::string& obj, const char* key) {
    auto p = obj.find(std::string("\"") + key + "\"");
    if (p == std::string::npos) return "";
    p = obj.find(':', p);
    p = obj.find('"', p);
    if (p == std::string::npos) return "";
    std::string out;
    for (size_t i = p + 1; i < obj.size(); ++i) {
        if (obj[i] == '\\' && i + 1 < obj.size()) {
            out.push_back(obj[++i]);
            continue;
        }
        if (obj[i] == '"') break;
        out.push_back(obj[i]);
    }
    return out;
}
}  // namespace

std::string ToolLibrary::toJson() const {
    std::ostringstream os;
    os << "{\"nextId\":" << nextId_ << ",\"tools\":[";
    for (size_t i = 0; i < tools_.size(); ++i) {
        const Tool& t = tools_[i];
        if (i) os << ",";
        os << "{\"id\":" << t.id << ",\"name\":\"" << esc(t.name)
           << "\",\"type\":\"" << toString(t.type) << "\",\"diameter\":"
           << fmtNum(t.diameter) << ",\"shank\":" << fmtNum(t.shankDiameter)
           << ",\"length\":" << fmtNum(t.length) << ",\"flutes\":" << t.flutes
           << ",\"tipAngle\":" << fmtNum(t.tipAngle) << ",\"cornerRadius\":"
           << fmtNum(t.cornerRadius) << ",\"feed\":" << fmtNum(t.feed)
           << ",\"plunge\":" << fmtNum(t.plunge) << ",\"rpm\":" << t.rpm
           << ",\"notes\":\"" << esc(t.notes) << "\"}";
    }
    os << "]}";
    return os.str();
}

ToolLibrary ToolLibrary::fromJson(const std::string& text) {
    ToolLibrary lib;
    lib.tools_.clear();
    lib.nextId_ = static_cast<int>(numField(text, "nextId", 1));
    // split the tools array into per-object spans by brace matching
    auto arr = text.find("\"tools\"");
    if (arr == std::string::npos) return lib;
    size_t i = text.find('[', arr);
    if (i == std::string::npos) return lib;
    int depth = 0;
    size_t objStart = std::string::npos;
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (c == '{') {
            if (depth == 0) objStart = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && objStart != std::string::npos) {
                std::string obj = text.substr(objStart, i - objStart + 1);
                Tool t;
                t.id = static_cast<int>(numField(obj, "id", 0));
                t.name = strField(obj, "name");
                t.type = toolTypeFromString(strField(obj, "type"));
                t.diameter = numField(obj, "diameter", 1.0);
                t.shankDiameter = numField(obj, "shank", 3.175);
                t.length = numField(obj, "length", 12.0);
                t.flutes = static_cast<int>(numField(obj, "flutes", 2));
                t.tipAngle = numField(obj, "tipAngle", 30.0);
                t.cornerRadius = numField(obj, "cornerRadius", 0.0);
                t.feed = numField(obj, "feed", 0.0);
                t.plunge = numField(obj, "plunge", 0.0);
                t.rpm = static_cast<int>(numField(obj, "rpm", 0));
                t.notes = strField(obj, "notes");
                lib.tools_.push_back(t);
                if (t.id >= lib.nextId_) lib.nextId_ = t.id + 1;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return lib;
}

ToolLibrary ToolLibrary::defaults() {
    ToolLibrary lib;
    Tool v;
    v.name = "V-bit 30\xC2\xB0 0.1mm tip";
    v.type = ToolType::VBit;
    v.diameter = 3.175;
    v.tipAngle = 30;
    v.flutes = 1;
    v.feed = 150;
    v.plunge = 60;
    v.rpm = 12000;
    v.notes = "PCB isolation";
    lib.add(v);

    Tool em;
    em.name = "1mm 2-flute flat";
    em.type = ToolType::EndMill;
    em.diameter = 1.0;
    em.flutes = 2;
    em.feed = 200;
    em.plunge = 80;
    em.rpm = 12000;
    em.notes = "board outline / pockets";
    lib.add(em);

    Tool d;
    d.name = "0.8mm drill";
    d.type = ToolType::Drill;
    d.diameter = 0.8;
    d.flutes = 2;
    d.plunge = 90;
    d.rpm = 12000;
    lib.add(d);

    Tool ball;
    ball.name = "2mm ball nose";
    ball.type = ToolType::BallNose;
    ball.diameter = 2.0;
    ball.cornerRadius = 1.0;
    ball.flutes = 2;
    ball.feed = 250;
    ball.plunge = 100;
    ball.rpm = 12000;
    ball.notes = "3D finishing / facing";
    lib.add(ball);
    return lib;
}

}  // namespace scnc
