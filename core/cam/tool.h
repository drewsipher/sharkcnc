// Cutting-tool model + a persistable library. Geometry here drives CAM:
// isolation width, and (later) simulation and tool-change prompts.
#pragma once
#include <string>
#include <vector>

namespace scnc {

enum class ToolType {
    EndMill,    // flat: cut width = diameter at any depth
    BallNose,   // rounded tip, radius = diameter/2
    BullNose,   // flat with corner radius
    VBit,       // conical engraver; width grows with depth
    Chamfer,    // conical, for bevels
    Drill,      // point drill
    Engraver    // conical, treated like a V-bit for width
};

const char* toString(ToolType t);
ToolType toolTypeFromString(const std::string& s);

struct Tool {
    int id = 0;
    std::string name;
    ToolType type = ToolType::EndMill;
    double diameter = 1.0;        // cutting diameter (mm) at full engagement
    double shankDiameter = 3.175; // mm (1/8" default)
    double length = 12.0;         // cutting/flute length (mm)
    int flutes = 2;
    double tipAngle = 30.0;       // included angle (deg) for V/chamfer/engraver
    double cornerRadius = 0.0;    // mm, ball(=d/2)/bull nose
    // cutting defaults (0 = unset; CAM keeps its own value)
    double feed = 0.0;            // mm/min
    double plunge = 0.0;          // mm/min
    int rpm = 0;
    std::string notes;

    // Effective cut width at a positive depth of cut below the surface.
    // For coned tools this is what PCB isolation actually depends on.
    double widthAtDepth(double depth) const;

    std::string summary() const;  // one-line human description
};

class ToolLibrary {
public:
    const std::vector<Tool>& tools() const { return tools_; }
    std::vector<Tool>& tools() { return tools_; }

    int add(Tool t);              // assigns a fresh id, returns it
    bool remove(int id);
    Tool* find(int id);
    const Tool* find(int id) const;

    std::string toJson() const;
    static ToolLibrary fromJson(const std::string& text);
    static ToolLibrary defaults();  // a sensible starter set

private:
    std::vector<Tool> tools_;
    int nextId_ = 1;
};

}  // namespace scnc
