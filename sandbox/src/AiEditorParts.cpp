// Shared draw code of the AI asset editors. See AiEditorParts.hpp: everything that is not drawing is
// inline there, so this translation unit is empty in a build without ImGui.

#include "AiEditorParts.hpp"

#if AVER_WITH_IMGUI

#include "imgui.h"

#include <cfloat>
#include <cstdio>

#if AVER_MODULE_SYNAPSE_SCENE && AVER_MODULE_SCENE
#  include "aver/scene/World.hpp"
#  include "aver/synapse/SynapseBt.hpp"
#  define AVER_AIEP_LIVE 1
#endif

namespace aver::editor {

using synapse::BbKeyDef;
using synapse::BbScope;
using synapse::BbType;
using synapse::BbValue;

bool bbValueEditor(const char* label, BbValue& v, BbType type, bool& activated) {
    bool changed = false;
    activated = false;
    switch (type) {
        case BbType::Bool: {
            bool b = v.i != 0;
            if (ImGui::Checkbox(label, &b)) { v.i = b ? 1 : 0; changed = true; activated = true; }
            break;
        }
        case BbType::Int:
        case BbType::Entity: {
            int n = static_cast<int>(v.i);
            if (ImGui::InputInt(label, &n, 0, 0)) { v.i = type == BbType::Entity ? std::max(0, n) : n; changed = true; }
            activated = ImGui::IsItemActivated();
            break;
        }
        case BbType::Float:
            changed = ImGui::DragFloat(label, &v.f, 0.05f);
            activated = ImGui::IsItemActivated();
            break;
        case BbType::Vec3: {
            f32 c[3] = {v.v.x, v.v.y, v.v.z};
            if (ImGui::DragFloat3(label, c, 0.5f)) { v.v = Vec3{c[0], c[1], c[2]}; changed = true; }
            activated = ImGui::IsItemActivated();
            break;
        }
        case BbType::String: {
            char buf[128];
            std::snprintf(buf, sizeof buf, "%s", v.s.c_str());
            if (ImGui::InputText(label, buf, sizeof buf)) { v.s = buf; changed = true; }
            activated = ImGui::IsItemActivated();
            break;
        }
    }
    return changed;
}

void drawSchemaPanel(synapse::BbSchema& schema, const synapse::Blackboard* live, const LiveBoard& flash,
                     SchemaPanelState& state, SchemaEditHost& host) {
    ImGui::TextWrapped("Keys the tree reads and writes. Agent keys are private to each agent; Shared keys live "
                       "on the team board all agents of a team see.");

    const int cols = live ? 5 : 4;
    if (ImGui::BeginTable("##bbkeys", cols,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Default");
        if (live) ImGui::TableSetupColumn("Live");
        ImGui::TableHeadersRow();

        std::string removeKeyName;
        for (usize ki = 0; ki < schema.keys.size(); ++ki) {
            const BbKeyDef key = schema.keys[ki];   // copy: edits below may reallocate the list
            ImGui::PushID(static_cast<int>(ki));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char nb[64];
            std::snprintf(nb, sizeof nb, "%s", key.name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##name", nb, sizeof nb);
            if (ImGui::IsItemDeactivatedAfterEdit() && key.name != nb) host.renameKey(key.name, nb);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##type", synapse::bbTypeName(key.type))) {
                for (u8 t = 0; t <= static_cast<u8>(BbType::Entity); ++t) {
                    const BbType bt = static_cast<BbType>(t);
                    if (ImGui::Selectable(synapse::bbTypeName(bt), bt == key.type) && bt != key.type) {
                        BbKeyDef nk = key;
                        nk.type = bt;
                        host.setKey(key.name, nk);
                        break;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##scope", synapse::bbScopeName(key.scope))) {
                for (u8 sc = 0; sc <= static_cast<u8>(BbScope::Shared); ++sc) {
                    const BbScope s = static_cast<BbScope>(sc);
                    if (ImGui::Selectable(synapse::bbScopeName(s), s == key.scope) && s != key.scope) {
                        BbKeyDef nk = key;
                        nk.scope = s;
                        host.setKey(key.name, nk);
                        break;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-60.0f);
            BbValue dv = key.defaultValue;
            bool activated = false;
            if (bbValueEditor("##default", dv, key.type, activated)) {
                if (activated) host.pushUndo();
                schema.keys[ki].defaultValue = dv;
                host.markDirty();
            } else if (activated) {
                host.pushUndo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeKeyName = key.name;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this key");

            if (live) {
                ImGui::TableSetColumnIndex(4);
                const i32 li = live->indexOf(key.name);
                if (li < 0) {
                    ImGui::TextDisabled("-");
                } else {
                    const f32 t = flash.flashOf(key.name);
                    const ImVec4 col = ImVec4(0.75f + 0.25f * t, 0.85f - 0.2f * t, 0.75f - 0.45f * t, 1.0f);
                    ImGui::TextColored(col, "%s", synapse::bbToString(live->valueAt(li)).c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!removeKeyName.empty()) host.removeKey(removeKeyName);
    }

    ImGui::Separator();
    char kb[64];
    std::snprintf(kb, sizeof kb, "%s", state.newKeyName.c_str());
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("##newkey", kb, sizeof kb)) state.newKeyName = kb;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::BeginCombo("##newtype", synapse::bbTypeName(static_cast<BbType>(state.newKeyType)))) {
        for (int t = 0; t <= static_cast<int>(BbType::Entity); ++t)
            if (ImGui::Selectable(synapse::bbTypeName(static_cast<BbType>(t)), t == state.newKeyType)) state.newKeyType = t;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.newKeyName.empty() || schema.indexOf(state.newKeyName) >= 0);
    if (ImGui::Button("Add key")) {
        BbKeyDef def;
        def.name = state.newKeyName;
        def.type = static_cast<BbType>(state.newKeyType);
        def.defaultValue = synapse::bbDefault(def.type);
        if (host.addKey(def)) state.newKeyName.clear();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Conventional keys filled from perception each tick: Target (Entity), CanSeeTarget (Bool), TimeSinceSeen (Float).");
}

bool drawEntityPicker(u32& debugEntity, const std::vector<u64>& treeIds, const char* what) {
#if AVER_AIEP_LIVE
    scene::World& world = scene::World::instance();
    synapse::BtSystem& sys = synapse::btSystem();

    std::vector<scene::Entity> candidates;
    for (const u64 id : treeIds)
        for (const scene::Entity e : sys.entitiesUsing(world, id))
            if (std::find(candidates.begin(), candidates.end(), e) == candidates.end()) candidates.push_back(e);
    const bool exact = !candidates.empty();
    if (!exact) candidates = sys.entitiesUsing(world, 0);   // every entity with a brain

    const char* current = "(none)";
    if (debugEntity != 0 && world.valid(static_cast<scene::Entity>(debugEntity)))
        current = world.name(static_cast<scene::Entity>(debugEntity));
    if (ImGui::BeginCombo("Debug entity", current)) {
        if (ImGui::Selectable("(none)", debugEntity == 0)) {
            if (sys.watched() == debugEntity) sys.watch(scene::kInvalidEntity);
            debugEntity = 0;
        }
        for (const scene::Entity e : candidates) {
            if (!world.valid(e)) continue;
            if (ImGui::Selectable(world.name(e), debugEntity == e)) {
                debugEntity = e;
                sys.watch(e);
            }
        }
        ImGui::EndCombo();
    }
    if (!exact)
        ImGui::TextDisabled("No entity in the current scene uses this exact file; showing every entity with a %s.", what);
    return debugEntity != 0;
#else
    (void)debugEntity; (void)treeIds; (void)what;
    ImGui::TextWrapped("Live debugging needs a build with the AI scene module (Aver.Synapse.Scene).");
    return false;
#endif
}

void canvasInput(CanvasView& v, f32 originX, f32 originY, bool hovered, bool clickedEmptySpace) {
    ImGuiIO& io = ImGui::GetIO();
    const f32 gx = (io.MousePos.x - originX - v.panX) / v.zoom;
    const f32 gy = (io.MousePos.y - originY - v.panY) / v.zoom;
    if (clickedEmptySpace) v.panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) v.panning = false;
    if ((v.panning && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) ||
        (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        v.panX += io.MouseDelta.x;
        v.panY += io.MouseDelta.y;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            const f32 z = std::clamp(v.zoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.5f, 1.8f);
            v.panX = (io.MousePos.x - originX) - gx * z;
            v.panY = (io.MousePos.y - originY) - gy * z;
            v.zoom = z;
        } else {
            v.panY += io.MouseWheel * 40.0f;
        }
    }
}

} // namespace aver::editor

#endif  // AVER_WITH_IMGUI
