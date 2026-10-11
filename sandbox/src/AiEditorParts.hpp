#pragma once
// Parts the AI asset editors share, lifted out of BtGraphEditor so the state-tree and query editors
// compose them instead of copying them: the tree canvas layout, pan / zoom state, the blackboard
// schema panel with its live-value flashing, and the debug entity picker.
//
// Everything that is not drawing is plain inline C++ (layout, bookkeeping), so a test that compiles an
// editor with AVER_WITH_IMGUI undefined (BtGraphEditorTest, AiEditorPartsTest) needs no extra source.
// The draw functions live in AiEditorParts.cpp behind AVER_WITH_IMGUI.
#include "aver/core/Types.hpp"
#include "aver/synapse/AiPaths.hpp"
#include "aver/synapse/Blackboard.hpp"
#include "aver/synapse/SmartObjectAsset.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace aver::editor {

// ---- tree layout (pure) -------------------------------------------------------------------------

struct TreeBox {
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // top-left, graph units
};

struct TreeLayoutParams {
    f32 nodeW = 180.0f;
    f32 gapX = 28.0f;
    f32 gapY = 60.0f;
};

// Top-down layout of a forest given as a parent array: one column slot per leaf, parents centred over
// their children, each depth a row as tall as its tallest node, roots side by side. `parents[i]` is -1
// for a root; any other value that is not an earlier index marks an orphan, left at x = 0 on the first
// row (a malformed tree draws instead of crashing). Index-parallel to `parents`; `heights` supplies each
// box's height.
inline std::vector<TreeBox> layoutTree(const std::vector<i32>& parents, const std::vector<f32>& heights,
                                       const TreeLayoutParams& p = {}) {
    const usize n = parents.size();
    std::vector<TreeBox> boxes(n);
    if (n == 0) return boxes;

    std::vector<std::vector<i32>> kids(n);
    std::vector<i32> roots;
    std::vector<i32> depth(n, 0);
    for (usize i = 0; i < n; ++i) {
        const i32 parent = parents[i];
        if (parent == -1) {
            roots.push_back(static_cast<i32>(i));
        } else if (parent >= 0 && static_cast<usize>(parent) < i) {
            kids[static_cast<usize>(parent)].push_back(static_cast<i32>(i));
            depth[i] = depth[static_cast<usize>(parent)] + 1;
        }
    }

    f32 cursor = 0.0f;
    struct Placer {
        const std::vector<std::vector<i32>>& kids;
        const TreeLayoutParams& p;
        std::vector<TreeBox>& boxes;
        f32& cursor;
        void place(i32 idx) {
            const std::vector<i32>& k = kids[static_cast<usize>(idx)];
            if (k.empty()) {
                boxes[static_cast<usize>(idx)].x = cursor;
                cursor += p.nodeW + p.gapX;
                return;
            }
            for (const i32 c : k) place(c);
            const f32 first = boxes[static_cast<usize>(k.front())].x;
            const f32 last = boxes[static_cast<usize>(k.back())].x;
            boxes[static_cast<usize>(idx)].x = (first + last) * 0.5f;
        }
    } placer{kids, p, boxes, cursor};
    for (const i32 r : roots) placer.place(r);

    i32 maxDepth = 0;
    for (const i32 d : depth) maxDepth = std::max(maxDepth, d);
    std::vector<f32> rowH(static_cast<usize>(maxDepth) + 1, 0.0f);
    for (usize i = 0; i < n; ++i) {
        boxes[i].w = p.nodeW;
        boxes[i].h = i < heights.size() ? heights[i] : 0.0f;
        rowH[static_cast<usize>(depth[i])] = std::max(rowH[static_cast<usize>(depth[i])], boxes[i].h);
    }
    std::vector<f32> rowY(rowH.size(), 0.0f);
    for (usize d = 1; d < rowY.size(); ++d) rowY[d] = rowY[d - 1] + rowH[d - 1] + p.gapY;
    for (usize i = 0; i < n; ++i) boxes[i].y = rowY[static_cast<usize>(depth[i])];
    return boxes;
}

// Smallest box containing every node; zero-sized for none.
inline TreeBox treeBounds(const std::vector<TreeBox>& boxes) {
    if (boxes.empty()) return {};
    f32 x0 = boxes[0].x, y0 = boxes[0].y, x1 = boxes[0].x + boxes[0].w, y1 = boxes[0].y + boxes[0].h;
    for (const TreeBox& b : boxes) {
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    return TreeBox{x0, y0, x1 - x0, y1 - y0};
}

// Topmost box containing the point, or -1.
inline i32 treeHitTest(const std::vector<TreeBox>& boxes, f32 x, f32 y) {
    for (usize i = boxes.size(); i-- > 0;) {
        const TreeBox& b = boxes[i];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return static_cast<i32>(i);
    }
    return -1;
}

// ---- canvas view --------------------------------------------------------------------------------

struct CanvasView {
    f32 panX = 24.0f, panY = 24.0f, zoom = 1.0f;
    bool panning = false;
    void reset() { panX = 24.0f; panY = 24.0f; zoom = 1.0f; panning = false; }
};

// ---- live blackboard values ---------------------------------------------------------------------

// Remembers each key's change stamp between frames so a value that just changed can flash in the live
// column. Call update() once a frame with the watched entity's board (null clears it).
class LiveBoard {
public:
    void update(const synapse::Blackboard* live, f32 dt) {
        for (auto& kv : flash_) kv.second = std::max(0.0f, kv.second - dt * 1.5f);
        if (!live) {
            lastStamp_.clear();
            return;
        }
        for (usize i = 0; i < live->size(); ++i) {
            const std::string& name = live->keyDef(static_cast<i32>(i)).name;
            const u64 stamp = live->stampAt(static_cast<i32>(i));
            u64& last = lastStamp_[name];
            if (last != 0 && stamp != last) flash_[name] = 1.0f;
            last = stamp;
        }
    }
    // 1 right after a change, fading to 0.
    f32 flashOf(const std::string& key) const {
        const auto it = flash_.find(key);
        return it == flash_.end() ? 0.0f : it->second;
    }

private:
    std::unordered_map<std::string, u64> lastStamp_;
    std::unordered_map<std::string, f32> flash_;
};

// ---- schema panel -------------------------------------------------------------------------------

// What the schema panel needs from the editor that owns the schema: undo snapshots and the structural
// edits (renaming a key also rewrites every use of it, which only the owner knows how to do).
class SchemaEditHost {
public:
    virtual ~SchemaEditHost() = default;
    virtual void pushUndo() = 0;
    virtual void markDirty() = 0;
    virtual bool addKey(const synapse::BbKeyDef& def) = 0;
    virtual bool removeKey(const std::string& name) = 0;
    virtual bool renameKey(const std::string& from, const std::string& to) = 0;
    virtual bool setKey(const std::string& name, const synapse::BbKeyDef& def) = 0;
};

struct SchemaPanelState {
    std::string newKeyName;
    int newKeyType = 0;
};

// ---- asset paths --------------------------------------------------------------------------------

// The content-relative form of an absolute asset path ("AI/Guard.ocst"), or "" when it is not under the
// AI content root. Entities hold their brain under the relative spelling (the id is a hash of it), so a
// save reloads both spellings.
inline std::string aiRelativeAssetPath(const std::string& absolute) {
    std::string root = synapse::aiContentRoot();
    std::string path = absolute;
    for (char& c : root) if (c == '\\') c = '/';
    for (char& c : path) if (c == '\\') c = '/';
    if (root.empty()) return {};
    if (root.back() != '/') root += '/';
    if (path.size() <= root.size()) return {};
    for (usize i = 0; i < root.size(); ++i) {
        const char a = root[i] >= 'A' && root[i] <= 'Z' ? static_cast<char>(root[i] - 'A' + 'a') : root[i];
        const char b = path[i] >= 'A' && path[i] <= 'Z' ? static_cast<char>(path[i] - 'A' + 'a') : path[i];
        if (a != b) return {};
    }
    return path.substr(root.size());
}

// Writes the commented starter bench (two slots) to `path` (Content Browser "New Smart Object Definition").
inline bool aiWriteSmartObjectStarter(const std::string& path, std::string* why = nullptr) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (why) *why = "cannot write " + path;
        return false;
    }
    f << "# Smart object definition: claimable slots in the owner's local space (cm, +X forward, +Y right, +Z up).\n"
         "# Add the CSmartObject component to an entity and set its asset to this file. See\n"
         "# docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.\n"
      << synapse::writeSoDef(synapse::soStarter());
    if (!f) {
        if (why) *why = "write failed for " + path;
        return false;
    }
    return true;
}

#if AVER_WITH_IMGUI
// An editable value of `type`. True when `v` changed. The widgets write in place, so the caller takes
// its undo snapshot when `activated` is set.
bool bbValueEditor(const char* label, synapse::BbValue& v, synapse::BbType type, bool& activated);

// The blackboard schema table (name, type, scope, default, live value) with its add-key row.
void drawSchemaPanel(synapse::BbSchema& schema, const synapse::Blackboard* live, const LiveBoard& flash,
                     SchemaPanelState& state, SchemaEditHost& host);

// "Debug entity" combo: entities running `treeIds`' asset (any brain when none do). Writes the pick to
// `debugEntity` and keeps BtSystem's watch in step. Returns true when an entity is picked.
bool drawEntityPicker(u32& debugEntity, const std::vector<u64>& treeIds, const char* what);

// Pan with the left button on empty space or the middle button, Ctrl+wheel zoom about the cursor, wheel
// scroll. `origin` is the canvas' top-left in screen space; call while the canvas surface is the last item.
void canvasInput(CanvasView& view, f32 originX, f32 originY, bool hovered, bool clickedEmptySpace);
#endif

} // namespace aver::editor
