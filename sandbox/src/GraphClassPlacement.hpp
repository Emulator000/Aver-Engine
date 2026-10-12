#pragma once
// Placing a graph CLASS actor from the editor: finding which .ocgraph files declare one, building the
// `PLACE none ... class <Name>` record the level loader already reads, and the small pure rules around
// it (role refusal, the viewport marker). Header-only and Core + Formats only, so GraphClassPlacementTest
// can include it directly; the ImGui menu, the drop and the spawn live in SandboxApp.
//
// CLASS is not modelled by the C++ graph reader (OcGraphData has no field for it), so this scans the
// top-level records itself, with the same rules GameApp.cpp's ocgraphRecord and the C# parser use.
#include "aver/core/Hash.hpp"
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace aver::editor {

struct GraphClassDecl {
    std::string className;
    std::string parent = "Actor";   // CLASS slot 2 unless it is key=value (OcGraphParser.cs:329-334)
    std::string path;               // as enumerated
    std::string relPath;            // content-relative, '/' separators
    u32  componentCount = 0;        // COMP records, for the picker tooltip
    bool duplicate = false;         // another file declares the same className
};

namespace detail {

inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool equalsAsciiCI(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

inline bool isSpaceTab(char c) { return c == ' ' || c == '\t' || c == '\r'; }

} // namespace detail

// Top-level record scan, no full parse (CLASS is unmodelled in C++). Mirrors GameApp.cpp's ocgraphRecord and
// the C# rules: lines trimmed, '#' lines skipped, record names case-insensitive, DOMAIN absent or "gameplay"
// only, a second CLASS record or a CLASS with no name -> false (the C# parser rejects both, OcGraphParser.cs:289-298).
inline bool parseGraphClassDecl(std::string_view text, GraphClassDecl& out) {
    GraphClassDecl d;
    bool sawClass = false, sawDomain = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(pos, nl - pos);
        pos = nl + 1;

        std::size_t b = 0;
        while (b < line.size() && detail::isSpaceTab(line[b])) ++b;
        line.remove_prefix(b);
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string_view> tok;
        for (std::size_t i = 0; i < line.size();) {
            while (i < line.size() && detail::isSpaceTab(line[i])) ++i;
            const std::size_t s = i;
            while (i < line.size() && !detail::isSpaceTab(line[i])) ++i;
            if (i > s) tok.push_back(line.substr(s, i - s));
        }
        if (tok.empty()) continue;

        if (detail::equalsAsciiCI(tok[0], "DOMAIN")) {
            if (tok.size() < 2 || sawDomain) return false;   // blank or repeated: the C# parser rejects the file
            sawDomain = true;
            if (!detail::equalsAsciiCI(tok[1], "gameplay")) return false;
        } else if (detail::equalsAsciiCI(tok[0], "CLASS")) {
            if (tok.size() < 2 || sawClass) return false;
            sawClass = true;
            d.className.assign(tok[1]);
            // The one positional token right after the name is the parent unless it is key=value.
            if (tok.size() > 2 && tok[2].find('=') == std::string_view::npos) d.parent.assign(tok[2]);
        } else if (detail::equalsAsciiCI(tok[0], "COMP")) {
            ++d.componentCount;
        }
    }
    if (!sawClass) return false;
    out = std::move(d);
    return true;
}

// Every *.ocgraph under contentDir (extension compared case-insensitively) that parseGraphClassDecl accepts,
// sorted by className then path; `duplicate` set on all but the first of a repeated name.
inline std::vector<GraphClassDecl> scanGraphClasses(const std::string& contentDir) {
    namespace fs = std::filesystem;
    std::vector<GraphClassDecl> found;
    std::error_code ec;
    if (contentDir.empty() || !fs::is_directory(fs::path(contentDir), ec)) return found;

    const fs::path root(contentDir);
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe) || fe) continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = detail::lowerAscii(c);
        if (ext != ".ocgraph") continue;

        std::ifstream f(it->path(), std::ios::binary);
        if (!f) continue;
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        GraphClassDecl d;
        if (!parseGraphClassDecl(text, d)) continue;
        d.path = it->path().string();
        std::error_code re;
        const fs::path rel = fs::relative(it->path(), root, re);
        d.relPath = re ? it->path().filename().generic_string() : rel.generic_string();
        found.push_back(std::move(d));
    }

    std::sort(found.begin(), found.end(), [](const GraphClassDecl& a, const GraphClassDecl& b) {
        if (a.className != b.className) return a.className < b.className;
        return a.path < b.path;
    });
    for (std::size_t i = 1; i < found.size(); ++i)
        if (found[i].className == found[i - 1].className) found[i].duplicate = true;
    return found;
}

// asset "none" (never read for a class placement, docs/VISUAL_SCRIPTING.md), className set, scale 1,
// collide true, snapToGround false.
inline fmt::OcWorldPlacement makeClassPlacement(const std::string& className, const Vec3& pos, f64 yawDeg) {
    fmt::OcWorldPlacement p;
    p.asset = "none";
    p.objectId = fnv1a64(std::string_view(p.asset));   // what the parser derives
    p.className = className;
    p.x = pos.x; p.y = pos.y; p.z = pos.z;
    p.yaw = yawDeg;
    p.sx = p.sy = p.sz = 1.0;
    p.collide = true;
    p.snapToGround = false;
    return p;
}

struct ClassRoleBits { bool gameMode = false, controller = false, gameInstance = false, abstractClass = false; };

// nullptr when placeable, else the status text to show. The abstract text is a predicate:
// classPlaceRefusalText puts the class name in front of it.
inline const char* classPlaceRefusal(const ClassRoleBits& b) {
    if (b.gameMode)      return "GameMode classes are chosen in World Settings, not placed";
    if (b.controller)    return "a PlayerController is spawned by Play, not placed";
    if (b.gameInstance)  return "a GameInstance is spawned by Play, not placed";
    if (b.abstractClass) return "is abstract and cannot be placed";
    return nullptr;
}

// Full status line for `name`, empty when placeable.
inline std::string classPlaceRefusalText(const ClassRoleBits& b, const std::string& name) {
    const char* r = classPlaceRefusal(b);
    if (!r) return {};
    if (b.gameMode || b.controller || b.gameInstance) return r;
    return "'" + name + "' " + r;
}

inline bool isGraphAssetExt(std::string_view lowerExt) { return lowerExt == ".ocgraph"; }

// Editor marker for a class root: three world-axis segments through `c`, +-half long. Colour differs when selected.
struct MarkerSeg { Vec3 a, b; f32 rgb[3]; };

// Half-size of the marker, and of the box a click on it tests (cm).
inline constexpr f32 kClassMarkerHalf = 25.0f;

inline void classMarkerSegments(const Vec3& c, f32 half, bool selected, std::vector<MarkerSeg>& out) {
    const f32 r = selected ? 1.0f : 0.35f, g = selected ? 0.65f : 0.80f, bl = selected ? 0.10f : 1.0f;
    const Vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const Vec3& ax : axis) {
        MarkerSeg s{c - ax * half, c + ax * half, {r, g, bl}};
        out.push_back(s);
    }
}

} // namespace aver::editor
