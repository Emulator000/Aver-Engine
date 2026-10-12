// Details > Add Component for Skeletal Mesh, Animator, Root Motion, Motion Matching, Control Rig, IK Rig, AI Brain
// and Smart Object: the menu entries, the inspector sections, add / remove / edit and their undo.
//
// The rules (what may be attached where, creation defaults, asset listing) are PlacedComponentModel.hpp. The
// component state is read and written through game::applyPlacedComponent / capturePlacedComponent
// (aver/game/LevelComponents.hpp), the code that also loads a COMPONENT record in the packaged game, so what
// the editor edits is exactly what the level saves and reloads. Plain numeric fields are edited on the live
// component in place; a gesture is bracketed by a "before" capture and one undo entry.
// docs/plans/parity_demo/ADD_COMPONENT.md sections 4 and 7.
#include "SandboxApp.hpp"

#if AVER_MODULE_SCENE
#include "PlacedComponentModel.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/anim/ControlRig.hpp"
#include "aver/anim/MotionMatchSystem.hpp"
#include "aver/game/LevelComponents.hpp"
#if AVER_MODULE_SYNAPSE_SCENE
#  include "aver/synapse/SynapseBt.hpp"
#  include "aver/synapse/SynapseSmartObject.hpp"
#  if AVER_WITH_SYNAPSE_AI
#    include "aver/synapse/SynapseAi.hpp"
#    include "aver/synapse/synapse_ai_abi.h"
#  endif
#endif

namespace aver {
namespace {

u32 kindComponentId(const scene::World& w, const editor::PlacedKind& k) {
    return k.builtinId != 0 ? k.builtinId : w.componentId(k.componentName);
}

// Sets `key` (dropping its unresolved `key#` hex twin), or removes it when `value` is empty.
void setKey(fmt::OcPlacedComponent& r, const std::string& key, const std::string& value) {
    const std::string hexKey = key + "#";
    r.kv.erase(std::remove_if(r.kv.begin(), r.kv.end(),
                              [&](const auto& kv) { return kv.first == key || kv.first == hexKey; }),
               r.kv.end());
    if (!value.empty()) r.kv.emplace_back(key, value);
}

} // namespace

// The content-relative path an asset id names, "" when the content index does not hold it.
std::string SandboxApp::pathOfContentId(u64 id) const {
    if (id == 0) return {};
    const std::string abs = content_.pathFor(id);
    return abs.empty() ? std::string() : cbRelativeToContent(abs);
}

editor::AttachContext SandboxApp::placedAttachContext(scene::Entity e) const {
    const scene::World& w = scene::World::instance();
    editor::AttachContext c;
    c.levelOwned = isLevelOwned(e);
    c.hasMesh = w.component<scene::CMeshRenderer>(e, scene::kComponentMeshRenderer) != nullptr;
    const auto* sm = w.component<scene::CSkeletalMesh>(e, scene::kComponentSkeletalMesh);
    c.hasSkeleton = sm && sm->skeleton != 0;
    const EntityAnim* oa = entityAnim(e);
    c.hasObjectAnim = oa && !oa->clip.empty();
    c.legacyOcmap = levelIsLegacyOcmap_;
    c.prefabLinked = prefabSys_.isLinked(e);
    c.instanceMember = level_.instances().isMember(e);
    return c;
}

void SandboxApp::beginPlacedGesture(const editor::PlacedKind& kind) {
    placedBefore_ = game::capturePlacedComponent(scene::World::instance(), selEntity_, kind.type,
                                                 [this](u64 id) { return pathOfContentId(id); });
    placedGesture_ = &kind;
}

// Pushes one undo entry when the component differs from `before` (nullopt = it was absent). A no-op entry would
// make Ctrl+Z visibly do nothing once, so an unchanged gesture pushes nothing.
void SandboxApp::commitPlacedEdit(const editor::PlacedKind& kind, std::optional<fmt::OcPlacedComponent> before) {
    placedBefore_.reset();
    placedGesture_ = nullptr;
    scene::World& w = scene::World::instance();
    const scene::Entity e = selEntity_;
    if (!w.valid(e)) return;
    auto after = game::capturePlacedComponent(w, e, kind.type, [this](u64 id) { return pathOfContentId(id); });
    if (before == after) return;
    EditCmd c;
    c.kind = EditCmd::Kind::PlacedComponent;
    c.id = editIdFor(e);
    c.pcType = kind.type;
    c.pcBefore = std::move(before);
    c.pcAfter = std::move(after);
    pushEdit(std::move(c));
    markLevelUnsaved();
}

bool SandboxApp::addPlacedComponent(const editor::PlacedKind& kind) {
    scene::World& w = scene::World::instance();
    const scene::Entity e = selEntity_;
    if (!w.valid(e)) return false;
    if (anyPlayActive()) { cbStatus_ = "Stop Play to add components."; return false; }
    const u32 cid = kindComponentId(w, kind);
    if (cid == 0) return false;
    editor::AttachContext ctx = placedAttachContext(e);
    ctx.alreadyPresent = w.hasComponent(e, cid);
    std::string why;
    if (!editor::canAttach(ctx, kind, &why)) { cbStatus_ = why; return false; }

    fmt::OcPlacedComponent rec = editor::makeNewRecord(kind);
    if (std::string_view(kind.type) == "SkeletalMesh") {
        // The skeleton next to the mesh with the same stem, when there is exactly that.
        std::vector<std::string> listing;
        for (const auto& kv : content_.index())
            if (lowerExt(std::filesystem::path(kv.second)) == ".ocskel") listing.push_back(cbRelativeToContent(kv.second));
        const std::string preset = editor::presetSkeletonFor(w.name(e), listing);
        if (!preset.empty()) setKey(rec, "skeleton", preset);
    }
    if (!game::applyPlacedComponent(w, e, rec)) {
        cbStatus_ = std::string("Could not add ") + kind.menuLabel + " (see the log)";
        return false;
    }
    EditCmd c;
    c.kind = EditCmd::Kind::PlacedComponent;
    c.id = editIdFor(e);
    c.pcType = kind.type;
    c.pcBefore = std::nullopt;
    c.pcAfter = game::capturePlacedComponent(w, e, kind.type, [this](u64 id) { return pathOfContentId(id); });
    pushEdit(std::move(c));
    markLevelUnsaved();
    cbStatus_ = std::string("Added ") + kind.menuLabel;
    return true;
}

bool SandboxApp::removePlacedComponentFromSelection(const editor::PlacedKind& kind) {
    scene::World& w = scene::World::instance();
    const scene::Entity e = selEntity_;
    if (!w.valid(e)) return false;
    if (anyPlayActive()) { cbStatus_ = "Stop Play to remove components."; return false; }
    const auto has = [&](const char* name) {
        const u32 id = w.componentId(name);
        return id != 0 && w.hasComponent(e, id);
    };
    std::string why;
    if (!editor::canRemove(w.hasComponent(e, scene::kComponentAnimator), has("CRootMotion"), has("CMotionMatcher"),
                           has("CControlRig"), has("CIkRig"), kind, &why)) {
        cbStatus_ = why;
        return false;
    }
    auto before = game::capturePlacedComponent(w, e, kind.type, [this](u64 id) { return pathOfContentId(id); });
    if (!before) return false;
    if (!game::removePlacedComponent(w, e, kind.type)) return false;
    EditCmd c;
    c.kind = EditCmd::Kind::PlacedComponent;
    c.id = editIdFor(e);
    c.pcType = kind.type;
    c.pcBefore = std::move(before);
    c.pcAfter = std::nullopt;
    pushEdit(std::move(c));
    markLevelUnsaved();
    cbStatus_ = std::string("Removed ") + kind.menuLabel;
    return true;
}

// Undo / redo of one PlacedComponent command: put the component into state `st` (nullopt = absent).
void SandboxApp::applyPlacedComponentState(EditId id, const std::string& type,
                                           const std::optional<fmt::OcPlacedComponent>& st) {
    scene::World& w = scene::World::instance();
    const scene::Entity e = entityForEdit(id);
    if (e == scene::kInvalidEntity || !w.valid(e)) return;
    if (st) game::applyPlacedComponent(w, e, *st);
    else    game::removePlacedComponent(w, e, type);
    markLevelUnsaved();
}

#if AVER_WITH_IMGUI
// A path row: the current path, a drop target and a Change... picker. Candidates are built only while the popup is
// open (scanning the whole index every frame once cost real time, see the Particle Emitter picker).
bool SandboxApp::drawAssetField(const char* id, const char* label, std::string& path,
                                std::span<const char* const> exts,
                                const std::function<bool(u64, const std::string&)>& accept) {
    bool changed = false;
    ImGui::PushID(id);
    const auto extWanted = [&](const std::string& ext) {
        for (const char* e : exts)
            if (ext == e) return true;
        return false;
    };
    ImGui::TextDisabled("%s  %s", label, path.empty() ? "(none)" : path.c_str());
    if (ImGui::BeginDragDropTarget()) {
        // The Content Browser sends its selection under kCbMoveDragDropType (one payload per drag), so take the
        // first entry with a wanted extension; kAssetDragDropType is still accepted from a single-asset source.
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kCbMoveDragDropType);
        if (!payload) payload = ImGui::AcceptDragDropPayload(kAssetDragDropType);
        if (payload && payload->Data) {
            const std::string blob(static_cast<const char*>(payload->Data),
                                   payload->DataSize > 0 ? static_cast<usize>(payload->DataSize - 1) : usize(0));
            bool taken = false;
            for (const std::string& one : editor::splitDropPayload(blob)) {
                if (!extWanted(lowerExt(std::filesystem::path(one)))) continue;
                const std::string rel = cbRelativeToContent(one);
                if (rel.empty() || std::filesystem::path(rel).is_absolute() || rel.rfind("..", 0) == 0) {
                    cbStatus_ = "That asset is not under the project's Content folder";
                    taken = true;
                    break;
                }
                path = rel;
                changed = taken = true;
                break;
            }
            if (!taken) {
                std::string list;
                for (const char* e : exts) list += (list.empty() ? "" : "/") + std::string(e);
                cbStatus_ = "Only a " + list + " asset can be dropped here";
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...")) ImGui::OpenPopup("##pick");

    static ImGuiID s_owner = 0;   // which row's candidates pickPlacedCands_ holds; one popup is open at a time
    const ImGuiID me = ImGui::GetID("##pick");
    if (ImGui::IsPopupOpen("##pick")) {
        PickerCands& pc = pickPlacedCands_;
        if (s_owner != me) { pc.drop(); s_owner = me; }
        const int frame = ImGui::GetFrameCount();
        if (pc.lastFrame < frame - 1 || pc.stamp != content_.index().size()) {
            pc.list.clear();
            pc.list.push_back({"(None)", 0});
            for (const editor::PlacedAssetChoice& a : editor::listAssets(
                     content_.index(), exts, [this](const std::string& abs) { return cbRelativeToContent(abs); }, accept))
                pc.list.push_back({a.label, a.id});
            pc.stamp = content_.index().size();
        }
        pc.lastFrame = frame;
        u64 picked = 0;
        const u64 current = path.empty() ? 0 : fnv1a64(std::string_view(path));
        if (assetPicker("##pick", pc.list, current, &picked)) {
            path.clear();
            for (const AssetChoice& c : pc.list)
                if (c.id == picked) { path = picked == 0 ? std::string() : c.label; break; }
            changed = true;
        }
    } else if (s_owner == me) {
        pickPlacedCands_.drop();
        s_owner = 0;
    }
    ImGui::PopID();
    return changed;
}

void SandboxApp::drawPlacedAddMenu() {
    scene::World& w = scene::World::instance();
    const bool playing = anyPlayActive();
    const editor::AttachContext base = placedAttachContext(selEntity_);   // the popup body only runs while open
    bool headed = false;
    for (const editor::PlacedKind& k : editor::kPlacedKinds) {
        const u32 id = kindComponentId(w, k);
        if (id == 0) continue;   // not registered in this build
        if (!headed) { ImGui::Separator(); ImGui::TextDisabled("Animation and AI"); headed = true; }
        editor::AttachContext ctx = base;
        ctx.alreadyPresent = w.hasComponent(selEntity_, id);
        std::string why;
        bool ok = editor::canAttach(ctx, k, &why);
        if (ok && playing) { ok = false; why = "Stop Play to add components."; }
        ImGui::BeginDisabled(!ok);
        if (ImGui::MenuItem(k.menuLabel)) addPlacedComponent(k);
        ImGui::EndDisabled();
        if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k.tip);
    }
}

bool SandboxApp::drawPlacedRemoveMenu() {
    scene::World& w = scene::World::instance();
    const scene::Entity e = selEntity_;
    const bool playing = anyPlayActive();
    const auto has = [&](const char* name) {
        const u32 id = w.componentId(name);
        return id != 0 && w.hasComponent(e, id);
    };
    const bool hasAnimator = w.hasComponent(e, scene::kComponentAnimator), hasRoot = has("CRootMotion"),
               hasMatcher = has("CMotionMatcher"), hasRig = has("CControlRig"), hasIk = has("CIkRig");
    bool anyPresent = false, headed = false;
    for (const editor::PlacedKind& k : editor::kPlacedKinds) {
        const u32 id = kindComponentId(w, k);
        if (id == 0) continue;
        if (!headed) { ImGui::Separator(); ImGui::TextDisabled("Animation and AI"); headed = true; }
        const bool present = w.hasComponent(e, id);
        anyPresent = anyPresent || present;
        std::string why;
        bool ok = present && editor::canRemove(hasAnimator, hasRoot, hasMatcher, hasRig, hasIk, k, &why);
        if (!present) why = "This entity does not have one.";
        else if (ok && playing) { ok = false; why = "Stop Play to remove components."; }
        ImGui::BeginDisabled(!ok);
        if (ImGui::MenuItem(k.menuLabel)) removePlacedComponentFromSelection(k);
        ImGui::EndDisabled();
        if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
    }
    return anyPresent;
}

// The Details sections of the components this entity carries, in canonical order.
void SandboxApp::drawPlacedComponentSections() {
    scene::World& w = scene::World::instance();
    const scene::Entity e = selEntity_;
    if (!w.valid(e)) return;
    const bool playing = anyPlayActive();
    const ImVec4 kRed(0.95f, 0.35f, 0.25f, 1.0f);
    const auto pathFn = [this](u64 id) { return pathOfContentId(id); };
    const auto cap = [&](const editor::PlacedKind& k) { return game::capturePlacedComponent(w, e, k.type, pathFn); };

    // A drag / slider / text edit changes the live value in place; the whole gesture is one undo entry, bracketed by
    // the item's activation and its deactivation. `changed` is the widget's return, passed in so the "before" is
    // captured on the activating frame, ahead of the first write.
    const auto gestureEdit = [&](const editor::PlacedKind& k, bool changed, const auto& write) {
        if (ImGui::IsItemActivated()) beginPlacedGesture(k);
        if (changed) {
            const bool open = placedGesture_ == &k;
            std::optional<fmt::OcPlacedComponent> before;
            if (!open) before = cap(k);
            write();
            if (!open) commitPlacedEdit(k, std::move(before));   // a one-shot change (typed value, keyboard step)
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && placedGesture_ == &k) commitPlacedEdit(k, placedBefore_);
        else if (ImGui::IsItemDeactivated() && placedGesture_ == &k) { placedGesture_ = nullptr; placedBefore_.reset(); }   // clicked, changed nothing
    };
    // A checkbox, combo or button finishes in the frame it changes: one capture either side of the write.
    const auto instantEdit = [&](const editor::PlacedKind& k, bool changed, const auto& write) {
        if (!changed) return;
        auto before = cap(k);
        write();
        commitPlacedEdit(k, std::move(before));
    };
    // An asset change goes through the applier, so the component, the saved record and undo agree.
    const auto changeAsset = [&](const editor::PlacedKind& k, const char* key, const std::string& path) {
        auto before = cap(k);
        fmt::OcPlacedComponent rec = before ? *before : editor::makeNewRecord(k);
        setKey(rec, key, path);
        if (!game::applyPlacedComponent(w, e, rec)) {
            cbStatus_ = std::string("Could not use '") + path + "' for " + k.menuLabel + " (see the log)";
            return;
        }
        commitPlacedEdit(k, std::move(before));
    };
    const auto assetRow = [&](const editor::PlacedKind& k, const char* key, const char* label, u64 curId,
                              const std::function<bool(u64, const std::string&)>& accept = {}) {
        std::string path = pathOfContentId(curId);
        if (curId != 0 && path.empty())
            ImGui::TextColored(kRed, "%s asset 0x%016llx is not under Content", label, static_cast<unsigned long long>(curId));
        ImGui::BeginDisabled(playing);
        const bool changed = drawAssetField(key, label, path, editor::placedAssetExts(k), accept);
        ImGui::EndDisabled();
        if (changed) changeAsset(k, key, path);
    };

    for (const editor::PlacedKind& k : editor::kPlacedKinds) {
        const u32 cid = kindComponentId(w, k);
        if (cid == 0 || !w.hasComponent(e, cid)) continue;
        if (!ImGui::CollapsingHeader(k.header, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        ImGui::PushID(k.type);
        if (playing) ImGui::TextDisabled("Stop Play to edit.");
        const std::string_view t = k.type;

        // ---- Skeletal Mesh ----
        if (t == "SkeletalMesh") {
            auto* sm = w.component<scene::CSkeletalMesh>(e, scene::kComponentSkeletalMesh);
            assetRow(k, "skeleton", "Skeleton", sm->skeleton);
            if (sm->skeleton == 0) {
                ImGui::TextDisabled("Pick a skeleton (.ocskel); nothing is posed until one is chosen.");
            } else if (sm->boneCount > 0) {
                ImGui::TextDisabled("Bones  %u", sm->boneCount);
            } else {
                // AnimSystem fills boneCount on its first tick with the skeleton; still 0 after a couple of frames
                // means it did not load.
                static u32 s_entity = 0;
                static u64 s_skeleton = 0;
                static int s_first = 0, s_last = 0;
                const int frame = ImGui::GetFrameCount();
                if (s_entity != static_cast<u32>(e) || s_skeleton != sm->skeleton || frame - s_last > 1) {
                    s_entity = static_cast<u32>(e);
                    s_skeleton = sm->skeleton;
                    s_first = frame;
                }
                s_last = frame;
                if (frame - s_first > 2) ImGui::TextColored(kRed, "Bones  0: the skeleton did not load");
                else                     ImGui::TextDisabled("Bones  resolving...");
            }
            const auto collideIt = entityCollide_.find(static_cast<u32>(e));
            if (collideIt == entityCollide_.end() || collideIt->second)
                ImGui::TextDisabled(ICON_WARNING " A character should not have a static collision body: turn Collides off.");
        }
        // ---- Animator ----
        else if (t == "Animator") {
            auto* an = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
            // Object clips move the entity instead of posing bones, so they are not offered here.
            assetRow(k, "clip", "Clip", an->clip, [](u64 clipId, const std::string&) {
                const fmt::OcAnimation* c = anim::animSystem().clip(clipId);
                return c && !(c->flags & fmt::kOcAnimObject);
            });
            ImGui::BeginDisabled(playing);
            f32 speed = an->speed;
            gestureEdit(k, ImGui::DragFloat("Speed", &speed, 0.01f, -4.0f, 4.0f, "%.2f"), [&] { an->speed = speed; });
            f32 weight = an->blendWeight;
            gestureEdit(k, ImGui::SliderFloat("Blend weight", &weight, 0.0f, 1.0f, "%.2f"), [&] { an->blendWeight = weight; });
            bool loop = (an->flags & scene::kAnimatorOnce) == 0;
            instantEdit(k, ImGui::Checkbox("Loop", &loop), [&] {
                if (loop) an->flags &= ~scene::kAnimatorOnce; else an->flags |= scene::kAnimatorOnce;
            });
            bool playingClip = (an->flags & scene::kAnimatorPaused) == 0;
            instantEdit(k, ImGui::Checkbox("Playing", &playingClip), [&] {
                if (playingClip) an->flags &= ~scene::kAnimatorPaused; else an->flags |= scene::kAnimatorPaused;
            });
            ImGui::EndDisabled();
            ImGui::TextDisabled("Time  %.2f s", an->time);
            const u32 mmId = w.componentId("CMotionMatcher");
            if (const auto* mm = mmId ? w.component<anim::CMotionMatcher>(e, mmId) : nullptr; mm && mm->database != 0)
                ImGui::TextDisabled("Pose comes from Motion Matching; this clip is ignored.");
        }
        // ---- Root Motion ----
        else if (t == "RootMotion") {
            auto* rm = w.component<anim::CRootMotion>(e, cid);
            ImGui::BeginDisabled(playing);
            static const char* const kModes[] = {"Ignore", "Apply", "Accumulate"};   // kRootMotionIgnore..Accumulate = 1..3
            const char* preview = rm->mode >= anim::kRootMotionIgnore && rm->mode <= anim::kRootMotionAccumulate
                                      ? kModes[rm->mode - 1] : "None";
            if (ImGui::BeginCombo("Mode", preview)) {
                for (int i = 0; i < 3; ++i)
                    if (ImGui::Selectable(kModes[i], rm->mode == i + 1)) instantEdit(k, rm->mode != i + 1, [&] { rm->mode = i + 1; });
                ImGui::EndCombo();
            }
            // 0 reads as X | Y | Yaw (and both sources): show the effective bits, never let the last one clear to 0.
            u32 axes = rm->axes ? rm->axes : anim::kRootAxesDefault;
            const struct { const char* label; u32 bit; } kAxes[] = {
                {"X", anim::kRootAxisX}, {"Y", anim::kRootAxisY}, {"Z", anim::kRootAxisZ}, {"Yaw", anim::kRootAxisYaw}};
            ImGui::TextDisabled("Axes");
            for (const auto& a : kAxes) {
                ImGui::SameLine();
                bool on = (axes & a.bit) != 0;
                instantEdit(k, ImGui::Checkbox(a.label, &on) && (on || (axes & ~a.bit) != 0), [&] {
                    rm->axes = on ? (axes | a.bit) : (axes & ~a.bit);
                });
            }
            u32 sources = rm->sources ? rm->sources : (anim::kRootSrcGraph | anim::kRootSrcMontage);
            ImGui::TextDisabled("Sources");
            ImGui::SameLine();
            bool fromGraph = (sources & anim::kRootSrcGraph) != 0;
            instantEdit(k, ImGui::Checkbox("State machine", &fromGraph) && (fromGraph || (sources & anim::kRootSrcMontage) != 0), [&] {
                rm->sources = fromGraph ? (sources | anim::kRootSrcGraph) : (sources & ~anim::kRootSrcGraph);
            });
            ImGui::SameLine();
            bool fromMontage = (sources & anim::kRootSrcMontage) != 0;
            instantEdit(k, ImGui::Checkbox("Montage", &fromMontage) && (fromMontage || (sources & anim::kRootSrcGraph) != 0), [&] {
                rm->sources = fromMontage ? (sources | anim::kRootSrcMontage) : (sources & ~anim::kRootSrcMontage);
            });
            f32 scale = rm->scale > 0.0f ? rm->scale : 1.0f;
            gestureEdit(k, ImGui::DragFloat("Scale", &scale, 0.01f, 0.0f, 100.0f, "%.2f"), [&] { rm->scale = scale; });
            bool external = (rm->flags & anim::kRootMotionExternal) != 0;
            instantEdit(k, ImGui::Checkbox("External consumer", &external), [&] {
                if (external) rm->flags |= anim::kRootMotionExternal; else rm->flags &= ~anim::kRootMotionExternal;
            });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A physics character reads the accumulator; Apply leaves the transform alone.");
            ImGui::EndDisabled();
            if (playing)
                ImGui::TextDisabled("Accumulated  %.1f  %.1f  %.1f cm   yaw %.2f rad", rm->accumX, rm->accumY, rm->accumZ, rm->accumYaw);
            ImGui::TextDisabled("Apply moves the entity during Play only.");
            const auto* an = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
            const u32 mmId = w.componentId("CMotionMatcher"), graphId = w.componentId("CAnimGraph");
            const auto* mm = mmId ? w.component<anim::CMotionMatcher>(e, mmId) : nullptr;
            if (!(an && an->clip != 0) && !(mm && mm->database != 0) && !(graphId && w.hasComponent(e, graphId)))
                ImGui::TextDisabled("Nothing produces root motion on this entity.");
        }
        // ---- Motion Matching ----
        else if (t == "MotionMatcher") {
            auto* mm = w.component<anim::CMotionMatcher>(e, cid);
            assetRow(k, "database", "Database", mm->database);
            ImGui::BeginDisabled(playing);
            int input = mm->inputSource == anim::kMotionInputEntityVelocity ? 1 : 0;
            static const char* const kInputs[] = {"Script", "Entity velocity"};
            if (ImGui::Combo("Input", &input, kInputs, 2)) instantEdit(k, true, [&] { mm->inputSource = input == 1 ? anim::kMotionInputEntityVelocity : anim::kMotionInputScript; });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Script: a script or graph writes the desired velocity.\nEntity velocity: follows how the entity actually moves.");
            int facing = (mm->flags & anim::kMotionFaceYaw) ? 1 : 0;
            static const char* const kFacing[] = {"Velocity", "Entity heading"};
            if (ImGui::Combo("Facing", &facing, kFacing, 2)) instantEdit(k, true, [&] {
                if (facing == 1) mm->flags |= anim::kMotionFaceYaw; else mm->flags &= ~anim::kMotionFaceYaw;
            });
            f32 rate = mm->playRate > 0.0f ? mm->playRate : 1.0f;
            gestureEdit(k, ImGui::DragFloat("Play rate", &rate, 0.01f, 0.1f, 4.0f, "%.2f"), [&] { mm->playRate = rate; });
            f32 interval = mm->searchInterval;
            gestureEdit(k, ImGui::DragFloat("Search interval (s)", &interval, 0.005f, 0.0f, 2.0f, interval > 0.0f ? "%.3f" : "database default"),
                        [&] { mm->searchInterval = interval; });
            f32 velHL = mm->velHalfLife;
            gestureEdit(k, ImGui::DragFloat("Velocity half-life (s)", &velHL, 0.005f, 0.0f, 2.0f, velHL > 0.0f ? "%.3f" : "database default"),
                        [&] { mm->velHalfLife = velHL; });
            f32 turnHL = mm->turnHalfLife;
            gestureEdit(k, ImGui::DragFloat("Turn half-life (s)", &turnHL, 0.005f, 0.0f, 2.0f, turnHL > 0.0f ? "%.3f" : "database default"),
                        [&] { mm->turnHalfLife = turnHL; });
            bool paused = (mm->flags & anim::kMotionPaused) != 0;
            instantEdit(k, ImGui::Checkbox("Paused", &paused), [&] {
                if (paused) mm->flags |= anim::kMotionPaused; else mm->flags &= ~anim::kMotionPaused;
            });
            ImGui::EndDisabled();
            // The component's OUT fields and the database's load state.
            anim::MotionMatchSystem& mms = anim::motionMatchSystem();
            if (mm->database == 0)            ImGui::TextDisabled("Database state  none");
            else if (mms.database(mm->database)) ImGui::TextDisabled("Database state  Ready");
            else if (mms.pendingBuilds() > 0) ImGui::TextDisabled("Database state  Loading");
            else ImGui::TextColored(kRed, "Database state  Failed, or it does not fit the skeleton (see the log)");
            const std::string playingClipPath = pathOfContentId(mm->clip);
            ImGui::TextDisabled("Playing  %s", playingClipPath.empty() ? "-" : playingClipPath.c_str());
            ImGui::TextDisabled("cost %.3f   switches %u   row %u", mm->cost, mm->switches, mm->row);
        }
        // ---- Control Rig ----
        else if (t == "ControlRig") {
            auto* cr = w.component<anim::CControlRig>(e, cid);
            assetRow(k, "rig", "Rig", cr->rig);
            ImGui::BeginDisabled(playing);
            f32 weight = cr->weight;
            gestureEdit(k, ImGui::SliderFloat("Weight", &weight, 0.0f, 1.0f, "%.2f"), [&] { cr->weight = weight; });
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 turns the whole rig off without detaching it.");
            ImGui::TextDisabled("applied to %u entities last tick", anim::controlRigSystem().appliedLastTick());
            ImGui::TextDisabled("An @input a script never sets uses its default.");
        }
        // ---- IK Rig ----
        else if (t == "IkRig") {
            auto* ik = w.component<anim::CIkRig>(e, cid);
            assetRow(k, "asset", "IK Rig", ik->asset);
            ImGui::BeginDisabled(playing);
            f32 weight = ik->weight;
            gestureEdit(k, ImGui::SliderFloat("IK weight", &weight, 0.0f, 1.0f, "%.2f"), [&] { ik->weight = weight; });
            const struct { const char* label; u32 off; } kParts[] = {
                {"Foot placement", anim::kIkRigFeetOff}, {"Goals", anim::kIkRigGoalsOff}, {"Body", anim::kIkRigBodyOff}};
            for (const auto& p : kParts) {
                bool on = (ik->flags & p.off) == 0;
                instantEdit(k, ImGui::Checkbox(p.label, &on), [&] {
                    if (on) ik->flags &= ~p.off; else ik->flags |= p.off;
                });
            }
            // "Foot IK weight" is IK weight while goals and body are off: the one authored weight, no extra field.
            if (ImGui::Button("Feet only"))
                instantEdit(k, true, [&] { ik->flags = (ik->flags & ~anim::kIkRigFeetOff) | anim::kIkRigGoalsOff | anim::kIkRigBodyOff; });
            ImGui::EndDisabled();
            anim::IkRigRuntime& rt = anim::controlRigSystem().ik();
            ImGui::TextDisabled("Ground probe  %s   probes last tick  %u", rt.hasGroundProbe() ? "installed" : "none", rt.probesLastTick());
            if (playing && rt.hasGroundProbe() && rt.probesLastTick() == 0)
                ImGui::TextDisabled("No foot rays ran: feet need collision under them.");
            if (!playing)
                ImGui::TextDisabled("Foot placement casts through physics, which is fully live in Play.");
        }
#if AVER_MODULE_SYNAPSE_SCENE
        // ---- AI Brain ----
        else if (t == "Brain") {
            synapse::BtSystem& bt = synapse::btSystem();
            auto* sb = w.component<synapse::CSynapseBehavior>(e, cid);
            std::string brainPath;
            if (const auto* tree = bt.findTree(sb->treeAssetId)) brainPath = tree->path;
            else if (const auto* st = bt.findStateTree(sb->treeAssetId)) brainPath = st->path;
            ImGui::BeginDisabled(playing);
            {
                std::string path = brainPath;
                if (sb->treeAssetId != 0 && path.empty())
                    ImGui::TextColored(kRed, "Brain asset 0x%016llx is not loaded", static_cast<unsigned long long>(sb->treeAssetId));
                // Clearing the path leaves an empty brain (the applier stops the tree first).
                if (drawAssetField("asset", "Brain", path, editor::placedAssetExts(k))) changeAsset(k, "asset", path);
            }
            static char s_team[64] = {};
            static bool s_teamActive = false;
            if (!s_teamActive) {
                const std::string* tn = bt.teamName(sb->teamId);
                std::snprintf(s_team, sizeof s_team, "%s", tn ? tn->c_str() : "");
            }
            ImGui::InputText("Team", s_team, sizeof s_team);
            s_teamActive = ImGui::IsItemActive();
            if (ImGui::IsItemActivated()) beginPlacedGesture(k);
            if (ImGui::IsItemDeactivatedAfterEdit() && placedGesture_ == &k) {
                bt.setTeam(w, e, s_team);
                commitPlacedEdit(k, placedBefore_);
            } else if (ImGui::IsItemDeactivated() && placedGesture_ == &k) {
                placedGesture_ = nullptr;
                placedBefore_.reset();
            }
            ImGui::EndDisabled();
            static const char* const kKinds[] = {"none", "behaviour tree", "state tree"};
            static const char* const kStatus[] = {"idle", "running", "succeeded", "failed"};
            const i32 kind = bt.brainKind(e), status = bt.brainStatus(e);
            ImGui::TextDisabled("Kind  %s   status  %s", kKinds[kind >= 0 && kind <= 2 ? kind : 0],
                                kStatus[status >= 0 && status <= 3 ? status : 0]);
            if (kind == 2) {
                const std::string active = bt.activeStatePath(e);
                ImGui::TextDisabled("Active state  %s", active.empty() ? "-" : active.c_str());
            }
            ImGui::BeginDisabled(!playing);
            if (ImGui::Button("Restart")) bt.brainRestart(e);
            ImGui::SameLine();
            if (ImGui::Button("Stop")) bt.brainStop(e);
            ImGui::EndDisabled();
            if (!playing) ImGui::TextDisabled("A brain runs during Play.");
        }
        // ---- Smart Object ----
        else if (t == "SmartObject") {
            auto* so = w.component<synapse::CSmartObject>(e, cid);
            const u32 assetField = w.fieldId("CSmartObject.asset");
            const char* cur = assetField ? aver_scene_get_str(static_cast<i32>(e), static_cast<i32>(assetField)) : "";
            std::string path = cur ? cur : "";
            ImGui::BeginDisabled(playing);
            if (drawAssetField("asset", "Smart Object", path, editor::placedAssetExts(k))) changeAsset(k, "asset", path);
            bool enabled = so->enabled != 0;
            instantEdit(k, ImGui::Checkbox("Enabled", &enabled), [&] { so->enabled = enabled ? 1 : 0; });
            ImGui::EndDisabled();
#if AVER_WITH_SYNAPSE_AI
            // The slot count lives in the Synapse DLL's registry, which registers the object on first ask: Play only.
            if (playing) {
                auto* ai = static_cast<synapse::SynapseAi*>(aver_syn_ai_instance());
                const i32 slots = ai ? ai->smartObjects().slotCount(w, e) : -1;
                if (slots >= 0) ImGui::TextDisabled("Slots  %d   free  %d", slots, so->slotsFree);
                else            ImGui::TextDisabled("Slots  not registered yet");
            } else {
                ImGui::TextDisabled("Slots and Free are shown during Play.");
            }
#else
            ImGui::TextDisabled("Free  %d", so->slotsFree);
#endif
        }
#endif
        ImGui::PopID();
    }
}
#endif // AVER_WITH_IMGUI

} // namespace aver
#endif // AVER_MODULE_SCENE
