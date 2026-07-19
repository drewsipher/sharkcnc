// sharkcam: fast PCB CAM from the command line.
//   sharkcam iso   front.gbr -o iso.nc [--tool 0.2] [--passes 2] ...
//   sharkcam drill board.drl -o drill.nc [--multi-tool] [--depth -1.8] ...
//   sharkcam info  file.gbr|file.drl
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "cam/excellon.h"
#include "cam/facing.h"
#include "cam/gerber.h"
#include "cam/isolation.h"
#include "cam/outline.h"

using namespace scnc;

namespace {

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    ok = f.good();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
    return f.good();
}

double argD(int argc, char** argv, const char* name, double dflt) {
    for (int i = 0; i < argc - 1; ++i)
        if (!std::strcmp(argv[i], name)) return std::atof(argv[i + 1]);
    return dflt;
}
int argI(int argc, char** argv, const char* name, int dflt) {
    for (int i = 0; i < argc - 1; ++i)
        if (!std::strcmp(argv[i], name)) return std::atoi(argv[i + 1]);
    return dflt;
}
bool argB(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i)
        if (!std::strcmp(argv[i], name)) return true;
    return false;
}
const char* argS(int argc, char** argv, const char* name, const char* dflt) {
    for (int i = 0; i < argc - 1; ++i)
        if (!std::strcmp(argv[i], name)) return argv[i + 1];
    return dflt;
}

int usage() {
    std::puts(
        "sharkcam - PCB CAM that respects your time\n"
        "\n"
        "  sharkcam iso <front.gbr> -o <out.nc>\n"
        "      --tool <mm=0.2> --passes <n=1> --overlap <0..1=0.5>\n"
        "      --depth <mm=-0.06> --travel <mm=1> --feed <mm/min=120>\n"
        "      --plunge <mm/min=60> --rpm <n=10000> --mirror\n"
        "\n"
        "  sharkcam drill <board.drl> -o <out.nc>\n"
        "      --depth <mm=-1.8> --travel <mm=2> --plunge <mm/min=90>\n"
        "      --rpm <n=10000> --multi-tool --mirror\n"
        "\n"
        "  sharkcam face -o <out.nc>   (surface flattening; no input file)\n"
        "      --w <mm=50> --h <mm=50> --x0 <mm=0> --y0 <mm=0>\n"
        "      --tool <mm=6> --stepover <0..1=0.4> --depth <mm=0.2>\n"
        "      --pass-depth <mm=0.2> --feed <mm/min=800> --plunge <mm/min=300>\n"
        "      --travel <mm=3> --rpm <n=12000> --spiral\n"
        "\n"
        "  sharkcam outline -o <out.nc>   (rectangular board cut-out w/ tabs)\n"
        "      --w <mm> --h <mm> --x0 <mm> --y0 <mm> --tool <mm=1>\n"
        "      --depth <mm=-1.8> --pass-depth <mm=0.4> --tabs <n=4>\n"
        "      --tab-width <mm=3> --tab-height <mm=0.5> --inside\n"
        "\n"
        "  sharkcam info <file.gbr | file.drl>\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    std::string cmd = argv[1];

    if (cmd == "face") {  // parametric, no input file
        const char* outPath = argS(argc, argv, "-o", nullptr);
        if (!outPath) return usage();
        FacingOptions opt;
        opt.x0 = argD(argc, argv, "--x0", opt.x0);
        opt.y0 = argD(argc, argv, "--y0", opt.y0);
        opt.width = argD(argc, argv, "--w", opt.width);
        opt.height = argD(argc, argv, "--h", opt.height);
        opt.toolDiameter = argD(argc, argv, "--tool", opt.toolDiameter);
        opt.stepover = argD(argc, argv, "--stepover", opt.stepover);
        opt.totalDepth = argD(argc, argv, "--depth", opt.totalDepth);
        opt.depthPerPass = argD(argc, argv, "--pass-depth", opt.depthPerPass);
        opt.feed = argD(argc, argv, "--feed", opt.feed);
        opt.plunge = argD(argc, argv, "--plunge", opt.plunge);
        opt.travelZ = argD(argc, argv, "--travel", opt.travelZ);
        opt.spindleRpm = argI(argc, argv, "--rpm", opt.spindleRpm);
        opt.spiral = argB(argc, argv, "--spiral");
        auto r = facingRoutine(opt);
        if (!r.ok) {
            std::fprintf(stderr, "facing error: %s\n", r.error.c_str());
            return 1;
        }
        if (!writeFile(outPath, r.gcode)) return 1;
        std::printf("%d pass(es), %.0f mm of cutting -> %s\n", r.passes,
                    r.lengthMm, outPath);
        return 0;
    }

    if (cmd == "outline") {  // parametric rectangle boundary
        const char* outPath = argS(argc, argv, "-o", nullptr);
        if (!outPath) return usage();
        double x0 = argD(argc, argv, "--x0", 0), y0 = argD(argc, argv, "--y0", 0);
        double w = argD(argc, argv, "--w", 50), h = argD(argc, argv, "--h", 50);
        OutlineOptions opt;
        opt.toolDiameter = argD(argc, argv, "--tool", opt.toolDiameter);
        opt.cutZ = argD(argc, argv, "--depth", opt.cutZ);
        opt.depthPerPass = argD(argc, argv, "--pass-depth", opt.depthPerPass);
        opt.travelZ = argD(argc, argv, "--travel", opt.travelZ);
        opt.feed = argD(argc, argv, "--feed", opt.feed);
        opt.plunge = argD(argc, argv, "--plunge", opt.plunge);
        opt.spindleRpm = argI(argc, argv, "--rpm", opt.spindleRpm);
        opt.tabs = argI(argc, argv, "--tabs", opt.tabs);
        opt.tabWidth = argD(argc, argv, "--tab-width", opt.tabWidth);
        opt.tabHeight = argD(argc, argv, "--tab-height", opt.tabHeight);
        opt.outside = !argB(argc, argv, "--inside");
        auto r = outlineRoutine(rectBoundary(x0, y0, w, h), opt);
        if (!r.ok) {
            std::fprintf(stderr, "outline error: %s\n", r.error.c_str());
            return 1;
        }
        if (!writeFile(outPath, r.gcode)) return 1;
        std::printf("%d pass(es), %d tabs -> %s\n", r.passes, opt.tabs, outPath);
        return 0;
    }

    if (argc < 3) return usage();
    std::string inPath = argv[2];
    bool ok = false;
    std::string text = readFile(inPath, ok);
    if (!ok) {
        std::fprintf(stderr, "cannot read %s\n", inPath.c_str());
        return 1;
    }

    if (cmd == "info") {
        if (text.find("M48") != std::string::npos ||
            inPath.ends_with(".drl")) {
            auto d = parseExcellon(text);
            std::printf("excellon: %zu tools, %zu holes\n", d.tools.size(),
                        d.hits.size());
            for (auto& t : d.tools)
                std::printf("  T%d  %.3fmm  x%d\n", t.number, t.diameter,
                            t.count);
        } else {
            auto g = parseGerber(text);
            if (!g.ok) {
                std::fprintf(stderr, "parse error: %s\n", g.error.c_str());
                return 1;
            }
            std::printf(
                "gerber: %d apertures, %d flashes, %d strokes, %d regions\n"
                "bounds: X %.2f..%.2f  Y %.2f..%.2f  (%.1f x %.1f mm)\n"
                "copper polygons: %zu\n",
                g.layer.apertures, g.layer.flashes, g.layer.strokes,
                g.layer.regions, g.layer.minX, g.layer.maxX, g.layer.minY,
                g.layer.maxY, g.layer.maxX - g.layer.minX,
                g.layer.maxY - g.layer.minY, g.layer.copper.size());
            for (auto& w : g.layer.warnings)
                std::printf("warning: %s\n", w.c_str());
        }
        return 0;
    }

    const char* outPath = argS(argc, argv, "-o", nullptr);
    if (!outPath) return usage();

    if (cmd == "iso") {
        auto g = parseGerber(text);
        if (!g.ok) {
            std::fprintf(stderr, "gerber error: %s\n", g.error.c_str());
            return 1;
        }
        for (auto& w : g.layer.warnings)
            std::fprintf(stderr, "warning: %s\n", w.c_str());
        IsolationOptions opt;
        opt.toolDiameter = argD(argc, argv, "--tool", opt.toolDiameter);
        opt.passes = argI(argc, argv, "--passes", opt.passes);
        opt.overlap = argD(argc, argv, "--overlap", opt.overlap);
        opt.cutZ = argD(argc, argv, "--depth", opt.cutZ);
        opt.travelZ = argD(argc, argv, "--travel", opt.travelZ);
        opt.feed = argD(argc, argv, "--feed", opt.feed);
        opt.plunge = argD(argc, argv, "--plunge", opt.plunge);
        opt.spindleRpm = argI(argc, argv, "--rpm", opt.spindleRpm);
        opt.mirrorX = argB(argc, argv, "--mirror");
        auto iso = isolationRoute(g.layer, opt);
        if (!iso.ok) {
            std::fprintf(stderr, "isolation error: %s\n", iso.error.c_str());
            return 1;
        }
        if (!writeFile(outPath, iso.gcode)) {
            std::fprintf(stderr, "cannot write %s\n", outPath);
            return 1;
        }
        std::printf("%zu toolpaths, %.0f mm of cutting -> %s\n",
                    iso.toolpaths.size(), iso.lengthMm, outPath);
        return 0;
    }

    if (cmd == "drill") {
        auto d = parseExcellon(text);
        if (!d.ok) {
            std::fprintf(stderr, "excellon error: %s\n", d.error.c_str());
            return 1;
        }
        DrillOptions opt;
        opt.cutZ = argD(argc, argv, "--depth", opt.cutZ);
        opt.travelZ = argD(argc, argv, "--travel", opt.travelZ);
        opt.plunge = argD(argc, argv, "--plunge", opt.plunge);
        opt.spindleRpm = argI(argc, argv, "--rpm", opt.spindleRpm);
        opt.singleTool = !argB(argc, argv, "--multi-tool");
        opt.mirrorX = argB(argc, argv, "--mirror");
        auto out = drillGcode(d, opt);
        if (!out.ok) {
            std::fprintf(stderr, "drill error: %s\n", out.error.c_str());
            return 1;
        }
        if (!writeFile(outPath, out.gcode)) {
            std::fprintf(stderr, "cannot write %s\n", outPath);
            return 1;
        }
        std::printf("%d holes -> %s\n", out.holes, outPath);
        return 0;
    }

    return usage();
}
