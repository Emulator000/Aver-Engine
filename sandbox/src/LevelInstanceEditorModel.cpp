#include "LevelInstanceEditorModel.hpp"

#if AVER_MODULE_SCENE

#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcLevelInst.hpp"
#include "aver/formats/OcStream.hpp"
#include "aver/world/LevelTransform.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace aver::editor {

namespace {

std::string stemOf(const std::string& ref) {
    std::string p = ref;
    for (char& c : p) if (c == '\\') c = '/';
    const usize slash = p.find_last_of('/');
    if (slash != std::string::npos) p.erase(0, slash + 1);
    const usize dot = p.find_last_of('.');
    if (dot != std::string::npos && dot > 0) p.erase(dot);
    return p;
}

bool endsWithCI(const std::string& s, const char* suffix) {
    const usize n = std::char_traits<char>::length(suffix);
    if (s.size() < n) return false;
    for (usize i = 0; i < n; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - n + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::string num(f64 v) {
    char b[48];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

void collectDescendants(scene::World& w, scene::Entity e, std::vector<scene::Entity>& out) {
    for (scene::Entity c = w.firstChild(e); c != scene::kInvalidEntity; c = w.nextSibling(c)) {
        out.push_back(c);
        collectDescendants(w, c, out);
    }
}

} // namespace

LevelInstanceEditorModel::LevelInstanceEditorModel(scene::World& world, game::LevelInstances& instances,
                                                   world::LevelAssetLibrary& library)
    : world_(world), instances_(instances), lib_(library) {}

bool LevelInstanceEditorModel::isLevelPath(const std::string& path) {
    return endsWithCI(path, ".ocworld") || endsWithCI(path, ".ocmap");
}

std::string LevelInstanceEditorModel::assetRefFor(const std::string& contentDir, const std::string& fullPath) {
    if (contentDir.empty() || !isLevelPath(fullPath)) return {};
    std::error_code ec;
    const std::filesystem::path rel = std::filesystem::relative(fullPath, contentDir, ec);
    if (ec || rel.empty()) return {};
    std::string s = rel.generic_string();
    if (s.rfind("..", 0) == 0) return {};
    return s;
}

std::string LevelInstanceEditorModel::fullPathFor(const std::string& assetRef) const {
    if (contentDir_.empty() || assetRef.empty()) return {};
    return (std::filesystem::path(contentDir_) / assetRef).string();
}

u64 LevelInstanceEditorModel::freshInstanceId(const std::string& assetRef) const {
    std::vector<u64> used;
    for (u32 i = 0; i < instances_.count(); ++i)
        if (const fmt::OcLevelInstance* r = instances_.recordOf(instances_.at(i))) used.push_back(r->id);
    for (;;) {
        const u64 id = fnv1a64(std::string_view(assetRef + "@" + std::to_string(++idCounter_) + "#" + std::to_string(used.size())));
        if (id != 0 && std::find(used.begin(), used.end(), id) == used.end()) return id;
    }
}

// ---- place ------------------------------------------------------------------------------------------------

LevelInstanceEditorModel::PlaceResult LevelInstanceEditorModel::place(const std::string& assetRef, const Transform& xf,
                                                                      scene::Entity parent) {
    PlaceResult r;
    const auto fail = [&](const std::string& m) {
        r.error = m;
        status_ = m;
        return r;
    };
    if (assetRef.empty() || !isLevelPath(assetRef)) return fail("That is not a level.");
    std::string why;
    if (!lib_.get(assetRef, &why)) return fail("Could not read '" + assetRef + "': " + why);
    std::string chain;
    if (!openLevel_.empty() && lib_.wouldCycle(openLevel_, assetRef, &chain))
        return fail("A level cannot contain itself (" + chain + ").");
    std::vector<std::string> order;
    if (!lib_.closure(assetRef, order, &chain)) return fail("'" + assetRef + "' nests itself or is too deep (" + chain + ").");

    // First use gives the child's placements stable ids (overrides are keyed by them). A level that exists only in
    // memory has no file to rewrite; its derived ids serve.
    const std::string full = fullPathFor(assetRef);
    std::error_code ec;
    if (!full.empty() && std::filesystem::exists(full, ec) && !lib_.ensureIds(assetRef, full, &why))
        return fail("Could not give '" + assetRef + "' stable ids: " + why);

    fmt::OcLevelInstance rec;
    rec.asset = assetRef;
    rec.x = xf.position.x; rec.y = xf.position.y; rec.z = xf.position.z;
    const Vec3 e = world::eulerDegFromQuat(xf.rotation);
    rec.roll = e.x; rec.pitch = e.y; rec.yaw = e.z;
    rec.sx = xf.scale.x; rec.sy = xf.scale.y; rec.sz = xf.scale.z;
    rec.id = freshInstanceId(assetRef);
    std::string cwhy;
    const scene::Entity root = instances_.createFromRecord(rec, parent, &cwhy);
    if (root == scene::kInvalidEntity) return fail("Could not place '" + assetRef + "': " + cwhy);

    const u32 id = instances_.instanceIdOf(root);
    game::LevelInstances::LevelInstanceState::Item gone;
    gone.instanceId = id;
    gone.present = false;
    r.edit.label = "Place " + stemOf(assetRef);
    r.edit.before.items.push_back(gone);
    r.edit.after = instances_.captureState({id});
    r.ok = true;
    r.root = root;
    status_ = "Placed " + stemOf(assetRef);
    return r;
}

// ---- picking and locking ------------------------------------------------------------------------------------

scene::Entity LevelInstanceEditorModel::pickTarget(scene::Entity hit, bool altHeld) const {
    if (hit == scene::kInvalidEntity) return hit;
    const scene::Entity top = instances_.topRootOf(hit);
    if (top == scene::kInvalidEntity) return hit;
    if (altHeld || instances_.editing(top)) return hit;
    return top;
}

bool LevelInstanceEditorModel::structureLocked(scene::Entity e, std::string* why) const {
    if (!instances_.isMember(e)) return false;
    const scene::Entity top = instances_.topRootOf(e);
    if (top == scene::kInvalidEntity || e == top || instances_.editing(top)) return false;
    if (why) *why = "That belongs to a level instance: use Edit Instance to change its contents, or Alt+click to override a placement.";
    return true;
}

// ---- edit in place ----------------------------------------------------------------------------------------------

bool LevelInstanceEditorModel::beginEdit(scene::Entity root, std::string* why) {
    if (instances_.anyDirtyEdit() && !instances_.editing(root)) {
        status_ = "Another instance is being edited: commit or discard it first.";
        if (why) *why = status_;
        return false;
    }
    if (!instances_.beginEdit(root)) {
        status_ = "That instance cannot be edited here.";
        if (why) *why = status_;
        return false;
    }
    status_ = "Editing " + stemOf(instances_.assetOf(root));
    return true;
}

bool LevelInstanceEditorModel::commit(scene::Entity root, std::string* why) {
    std::string w;
    if (!instances_.commitEdit(root, &w)) {
        status_ = "Commit failed: " + w;
        if (why) *why = w;
        return false;
    }
    status_ = "Committed " + stemOf(instances_.assetOf(root));
    return true;
}

bool LevelInstanceEditorModel::discard(scene::Entity root) {
    if (!instances_.discardEdit(root)) return false;
    status_ = "Discarded the edit";
    return true;
}

// ---- instance operations -------------------------------------------------------------------------------------------

template <class F>
bool LevelInstanceEditorModel::runOn(scene::Entity root, const char* label, LevelInstanceEdit& edit, F&& op) {
    const u32 id = instances_.instanceIdOf(root);
    if (id == 0) return false;
    LevelInstanceEdit e;
    e.label = label;
    e.before = instances_.captureState({id});
    if (!op()) return false;
    e.after = instances_.captureState({id});
    edit = std::move(e);
    return true;
}

bool LevelInstanceEditorModel::deleteMember(scene::Entity member, LevelInstanceEdit& edit,
                                            const std::function<void(scene::Entity)>& destroy) {
    const scene::Entity top = instances_.topRootOf(member);
    if (top == scene::kInvalidEntity || member == top || instances_.editing(top) || !destroy) return false;
    const u32 id = instances_.instanceIdOf(top);
    if (id == 0) return false;
    LevelInstanceEdit e;
    e.label = "Delete Level Instance Member";
    e.before = instances_.captureState({id});
    destroy(member);
    e.after = instances_.captureState({id});
    edit = std::move(e);
    return true;
}

bool LevelInstanceEditorModel::breakInstance(scene::Entity root, LevelInstanceEdit& edit) {
    const u32 id = instances_.instanceIdOf(root);
    if (id == 0 || instances_.isNested(root)) return false;
    LevelInstanceEdit e;
    e.label = "Break Level Instance";
    e.before = instances_.captureState({id});
    std::vector<scene::Entity> all;
    collectDescendants(world_, root, all);
    if (!instances_.breakInstance(root)) return false;
    e.plain = std::move(all);
    e.isBreak = true;
    e.breakId = id;
    game::LevelInstances::LevelInstanceState::Item gone;
    gone.instanceId = id;
    gone.present = false;
    e.after.items.push_back(gone);
    edit = std::move(e);
    status_ = "Broke the instance into ordinary placements";
    return true;
}

bool LevelInstanceEditorModel::destroyInstance(scene::Entity root, LevelInstanceEdit& edit) {
    const u32 id = instances_.instanceIdOf(root);
    if (id == 0 || instances_.isNested(root)) return false;   // a nested instance deletes as an override
    LevelInstanceEdit e;
    e.label = "Delete Level Instance";
    e.before = instances_.captureState({id});
    if (!instances_.destroy(root)) return false;
    game::LevelInstances::LevelInstanceState::Item gone;
    gone.instanceId = id;
    gone.present = false;
    e.after.items.push_back(gone);
    edit = std::move(e);
    return true;
}

bool LevelInstanceEditorModel::revertAll(scene::Entity root, LevelInstanceEdit& edit) {
    return runOn(root, "Revert Level Instance Overrides", edit, [&] { return instances_.revertAll(root); });
}

bool LevelInstanceEditorModel::revertOverride(scene::Entity root, const fmt::OcPrefabOverride& ov, LevelInstanceEdit& edit) {
    return runOn(root, "Revert Override", edit, [&] { return instances_.revertOverride(root, ov); });
}

bool LevelInstanceEditorModel::setPacked(scene::Entity root, bool packed, LevelInstanceEdit& edit) {
    return runOn(root, packed ? "Pack Level Instance" : "Unpack Level Instance", edit,
                 [&] { instances_.setPacked(root, packed); return true; });
}

bool LevelInstanceEditorModel::setStream(scene::Entity root, i32 stream, LevelInstanceEdit& edit) {
    return runOn(root, "Set Instance Streaming", edit, [&] { return instances_.setRecordStream(root, stream); });
}

bool LevelInstanceEditorModel::rename(scene::Entity root, const std::string& name, LevelInstanceEdit& edit) {
    return runOn(root, "Rename Level Instance", edit, [&] { return instances_.setRecordName(root, name); });
}

bool LevelInstanceEditorModel::reload(scene::Entity root) {
    if (!instances_.reload(root)) return false;
    status_ = "Reloaded " + stemOf(instances_.assetOf(root));
    return true;
}

// ---- Create from selection -------------------------------------------------------------------------------------------------

LevelInstanceEditorModel::CreateResult LevelInstanceEditorModel::createFromSelection(
    const std::vector<scene::Entity>& roots, const std::string& assetRef, const CaptureFn& capture) {
    CreateResult r;
    const auto fail = [&](const std::string& m) {
        r.error = m;
        status_ = m;
        return r;
    };
    if (roots.empty()) return fail("Nothing is selected.");
    if (!capture) return fail("The editor cannot capture entities.");
    if (assetRef.empty() || !isLevelPath(assetRef)) return fail("Give the level a .ocworld name.");
    const std::string full = fullPathFor(assetRef);
    if (full.empty()) return fail("No project is open.");
    std::error_code ec;
    if (std::filesystem::exists(full, ec)) return fail("'" + assetRef + "' already exists.");

    // The pivot: the first entity's position, yaw only.
    const Transform wp = transformFromMatrix(world_.worldMatrix(roots[0]));
    Transform pivot;
    pivot.position = wp.position;
    pivot.rotation = world::quatFromEulerDeg(Vec3{0.0f, 0.0f, world::eulerDegFromQuat(wp.rotation).z});
    const Mat4 inv = pivot.toMatrix().inverse();
    const auto reorigin = [&](const Mat4& worldM) { return transformFromMatrix(worldM * inv); };

    fmt::OcWorldData out;
    out.name = stemOf(assetRef);
    std::function<void(scene::Entity, i32)> visit = [&](scene::Entity e, i32 parentIdx) {
        const usize pb = out.placements.size(), lb = out.lights.size(), db = out.decals.size();
        const bool took = capture(e, out, parentIdx);
        i32 myIdx = parentIdx;
        if (took && out.placements.size() > pb) {
            myIdx = static_cast<i32>(pb);
            fmt::OcWorldPlacement& p = out.placements[pb];
            p.parent = parentIdx;
            if (parentIdx < 0) {   // a root of the new level: re-origined under the pivot
                const Transform t = reorigin(world_.worldMatrix(e));
                p.x = t.position.x; p.y = t.position.y; p.z = t.position.z;
                const Vec3 eul = world::eulerDegFromQuat(t.rotation);
                p.roll = eul.x; p.pitch = eul.y; p.yaw = eul.z;
                p.sx = t.scale.x; p.sy = t.scale.y; p.sz = t.scale.z;
            }
        }
        // Lights and decals are world-space records: through the same inverse.
        for (usize i = lb; i < out.lights.size(); ++i) {
            fmt::OcLight& l = out.lights[i];
            Transform lx;
            lx.position = Vec3{static_cast<f32>(l.x), static_cast<f32>(l.y), static_cast<f32>(l.z)};
            lx.rotation = world::quatFromEulerDeg(Vec3{static_cast<f32>(l.roll), static_cast<f32>(l.pitch), static_cast<f32>(l.yaw)});
            const Transform t = reorigin(lx.toMatrix());
            l.x = t.position.x; l.y = t.position.y; l.z = t.position.z;
            const Vec3 eul = world::eulerDegFromQuat(t.rotation);
            l.roll = eul.x; l.pitch = eul.y; l.yaw = eul.z;
        }
        for (usize i = db; i < out.decals.size(); ++i) {
            fmt::OcDecal& d = out.decals[i];
            Transform dx;
            dx.position = Vec3{static_cast<f32>(d.x), static_cast<f32>(d.y), static_cast<f32>(d.z)};
            dx.rotation = world::quatFromEulerDeg(Vec3{static_cast<f32>(d.roll), static_cast<f32>(d.pitch), static_cast<f32>(d.yaw)});
            const Transform t = reorigin(dx.toMatrix());
            d.x = t.position.x; d.y = t.position.y; d.z = t.position.z;
            const Vec3 eul = world::eulerDegFromQuat(t.rotation);
            d.roll = eul.x; d.pitch = eul.y; d.yaw = eul.z;
        }
        for (scene::Entity c = world_.firstChild(e); c != scene::kInvalidEntity; c = world_.nextSibling(c)) visit(c, myIdx);
    };
    for (const scene::Entity e : roots) visit(e, -1);
    if (out.placements.empty() && out.lights.empty() && out.decals.empty()) return fail("Nothing in the selection can go in a level.");
    fmt::assignPlacementIds(out);

    // EXTENT: the positions of what was captured (the host's mesh bounds are not at hand here).
    {
        std::vector<Transform> wx(out.placements.size());
        bool any = false;
        f64 mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
        for (usize i = 0; i < out.placements.size(); ++i) {
            const fmt::OcWorldPlacement& p = out.placements[i];
            Transform xf;
            xf.position = Vec3{static_cast<f32>(p.x), static_cast<f32>(p.y), static_cast<f32>(p.z)};
            xf.rotation = world::quatFromEulerDeg(Vec3{static_cast<f32>(p.roll), static_cast<f32>(p.pitch), static_cast<f32>(p.yaw)});
            xf.scale = Vec3{static_cast<f32>(p.sx), static_cast<f32>(p.sy), static_cast<f32>(p.sz)};
            if (p.parent >= 0 && static_cast<usize>(p.parent) < i) {
                const Transform& pw = wx[static_cast<usize>(p.parent)];
                Transform o;
                o.scale = Vec3{pw.scale.x * xf.scale.x, pw.scale.y * xf.scale.y, pw.scale.z * xf.scale.z};
                o.rotation = pw.rotation * xf.rotation;
                o.position = pw.position + pw.rotation.rotate(Vec3{xf.position.x * pw.scale.x, xf.position.y * pw.scale.y, xf.position.z * pw.scale.z});
                xf = o;
            }
            wx[i] = xf;
            const f64 v[3] = {xf.position.x, xf.position.y, xf.position.z};
            for (int a = 0; a < 3; ++a) {
                if (!any) { mn[a] = mx[a] = v[a]; }
                else { mn[a] = std::min(mn[a], v[a]); mx[a] = std::max(mx[a], v[a]); }
            }
            any = true;
        }
        if (any) {
            out.hasExtent = true;
            for (int a = 0; a < 3; ++a) { out.extentMin[a] = mn[a]; out.extentMax[a] = mx[a]; }
        }
    }

    std::string why;
    if (!fmt::saveOcworld(full, out, &why)) return fail("Could not write '" + assetRef + "': " + why);
    lib_.put(assetRef, out);

    const PlaceResult placed = place(assetRef, pivot);
    if (!placed.ok) return fail(placed.error);
    r.ok = true;
    r.assetRef = assetRef;
    r.instance = placed.root;
    r.edit = placed.edit;
    r.edit.label = "Create Level Instance " + stemOf(assetRef);
    status_ = "Created " + stemOf(assetRef);
    return r;
}

// ---- undo -----------------------------------------------------------------------------------------------------------------------

bool LevelInstanceEditorModel::undo(LevelInstanceEdit& e) {
    if (e.isBreak) {
        // The members went plain on the break: they are removed again, then the instance comes back from its state.
        for (const scene::Entity p : e.plain)
            if (world_.valid(p) && !world_.destroyPending(p)) world_.destroy(p);
        e.plain.clear();
    }
    return instances_.restoreState(e.before);
}

bool LevelInstanceEditorModel::redo(LevelInstanceEdit& e) {
    if (e.isBreak) {
        const scene::Entity root = instances_.rootOfId(e.breakId);
        if (root == scene::kInvalidEntity) return false;
        std::vector<scene::Entity> all;
        collectDescendants(world_, root, all);
        if (!instances_.breakInstance(root)) return false;
        e.plain = std::move(all);
        return true;
    }
    return instances_.restoreState(e.after);
}

// ---- Details --------------------------------------------------------------------------------------------------------------------------

std::string LevelInstanceEditorModel::describe(const fmt::OcPrefabOverride& ov) {
    std::vector<u64> ids;
    std::string where = ov.path;
    if (fmt::ocLevelInstSplitIds(ov.path, ids)) {
        where.clear();
        for (usize i = 0; i < ids.size(); ++i) {
            char b[24];
            std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(ids[i]));
            if (i) where += '/';
            where += b;
        }
    }
    std::string value;
    switch (ov.value.kind) {
        case fmt::kOcPrefabKindVec3:
            for (usize i = 0; i < ov.value.f.size(); ++i) { if (i) value += ' '; value += num(ov.value.f[i]); }
            break;
        case fmt::kOcPrefabKindString: value = ov.value.s.empty() ? std::string("(mesh's own)") : ov.value.s; break;
        case fmt::kOcPrefabKindBool:   value = ov.value.i ? "on" : "off"; break;
        default:                       value = std::to_string(ov.value.i); break;
    }
    if (ov.value.name == fmt::kLiFieldRemoved) return where + ": removed";
    return where + ": " + ov.value.name + " = " + value;
}

std::vector<LevelInstanceOverrideRow> LevelInstanceEditorModel::rows(scene::Entity root) {
    std::vector<LevelInstanceOverrideRow> out;
    std::vector<fmt::OcPrefabOverride> ov;
    if (!instances_.computeOverrides(root, ov)) return out;
    const world::LevelAsset* asset = lib_.get(instances_.assetOf(root));
    for (const fmt::OcPrefabOverride& o : ov) {
        LevelInstanceOverrideRow row;
        row.ov = o;
        row.label = describe(o);
        std::vector<u64> ids;
        if (asset && fmt::ocLevelInstSplitIds(o.path, ids) && ids.size() == 1) {
            for (const fmt::OcWorldPlacement& p : asset->data.placements)
                if (p.placementId == ids[0]) {
                    const std::string nm = p.name.empty() ? stemOf(p.asset) : p.name;
                    row.label = nm + describe(o).substr(describe(o).find(':'));
                    break;
                }
        }
        out.push_back(std::move(row));
    }
    return out;
}

bool LevelInstanceEditorModel::popReturn(LevelReturnPoint& out) {
    if (returnStack_.empty()) return false;
    out = std::move(returnStack_.back());
    returnStack_.pop_back();
    return true;
}

} // namespace aver::editor

#endif // AVER_MODULE_SCENE
