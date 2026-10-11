// The .ocinput editor tab. See the header for the three-line SandboxApp hook, why this tab merges
// onto the file's own text on save, and InputSchemeEditorHooks for how it reaches SandboxApp.
#include "InputSchemeEditor.hpp"

#include "EditorKeybinds.hpp"
#include "InputKeyNames.hpp"
#include "aver/core/Log.hpp"
#include "aver/platform/FileSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#if AVER_WITH_IMGUI
#  include "EditorIcons.hpp"
#  include "imgui.h"
#  if AVER_MODULE_FRAMEWORK
#    include "aver/framework/framework_abi.h"   // the live State/Elapsed column while playing
#  endif
#endif

namespace aver::editor {
namespace {
InputSchemeEditorHooks g_hooks;

// A name a chord/combo can refer to: the spec grammar splits on ':' and ',' and a file on whitespace.
bool safeRefName(const std::string& n) {
    if (n.empty()) return false;
    for (char c : n)
        if (c == ':' || c == ',' || c == '#' || c == ';' || std::isspace(static_cast<unsigned char>(c))) return false;
    return true;
}

// True for a spec that is a chord/combo naming `name`.
bool triggerRefersTo(const std::string& spec, const std::string& name) {
    fmt::OcTriggerSpec t;
    if (!fmt::parseTriggerSpec(spec, t)) return false;
    if (t.kind != fmt::OcTriggerKind::Chord && t.kind != fmt::OcTriggerKind::Combo) return false;
    return std::find(t.refs.begin(), t.refs.end(), name) != t.refs.end();
}
} // namespace

void setInputSchemeEditorHooks(InputSchemeEditorHooks hooks) { g_hooks = std::move(hooks); }

InputSchemeEditor::InputSchemeEditor(std::string path) : path_(std::move(path)) { loadFromDisk(); }

void InputSchemeEditor::loadFromDisk() {
    std::string text;
    if (!readFileText(path_, text)) {
        loaded_ = false;
        loadError_ = "could not read " + path_;
        return;
    }
    fmt::OcInputData loaded;
    std::string err;
    if (!fmt::parseOcinput(text, loaded, &err)) {
        loaded_ = false;
        loadError_ = err.empty() ? "failed to parse .ocinput" : err;
        return;
    }
    originalText_ = std::move(text);
    data_ = std::move(loaded);
    loaded_ = true;
    loadError_.clear();
    dirty_ = false;
    history_.clear();
}

std::string InputSchemeEditor::title() const {
    // No manual dirty marker: the host applies ImGuiWindowFlags_UnsavedDocument for every editor
    // whose dirty() is true (AssetEditor.cpp) -- see FoliageTypeEditor::title()'s own comment for
    // the doubled-marker bug this avoids.
    return std::filesystem::path(path_).filename().string() + "###inputscheme:" + path_;
}

bool InputSchemeEditor::save(std::string* why) {
    if (!loaded_) {
        if (why) *why = "cannot save: file failed to load (" + loadError_ + ")";
        return false;
    }
    // Merged against the file's own last-loaded text, not written from scratch -- see this file's
    // header comment and GraphEditor::save()'s identical reason.
    const std::string text = fmt::writeOcinput(data_, originalText_);
    if (!writeFileTextAtomic(path_, text)) {
        if (why) *why = "could not write " + path_;
        return false;
    }
    originalText_ = text;   // the next save merges against what is now actually on disk
    dirty_ = false;
    return true;
}

void InputSchemeEditor::onFileChanged() {
    // A DIRTY TAB KEEPS ITS EDITS -- FoliageTypeEditor/BtEditor/SoundEditor's own rule: reloading
    // here would discard what the author typed because something else touched the file.
    if (dirty_) {
        AVER_WARN("[InputSchemeEditor] '{}' changed on disk, but this tab has unsaved edits -- "
                  "keeping them", path_);
        return;
    }
    loadFromDisk();
}

// ---- undo -----------------------------------------------------------------------------------------

void InputSchemeEditor::pushUndo() { history_.push(data_); }

void InputSchemeEditor::undo() {
    if (!history_.undo(data_)) return;
    dirty_ = true;
}

void InputSchemeEditor::redo() {
    if (!history_.redo(data_)) return;
    dirty_ = true;
}

// ---- actions ----------------------------------------------------------------------------------

void InputSchemeEditor::addAction() {
    if (!loaded_) return;
    pushUndo();
    std::string name = "NewAction";
    for (int n = 1; ; ++n) {
        bool clash = false;
        for (const auto& a : data_.actions) if (a.name == name) { clash = true; break; }
        if (!clash) break;
        name = "NewAction" + std::to_string(n);
    }
    fmt::OcInputAction a;
    a.name = name;
    a.type = fmt::OcInputValueType::Digital;
    data_.actions.push_back(std::move(a));
    dirty_ = true;
}

void InputSchemeEditor::removeAction(usize index) {
    if (!loaded_ || index >= data_.actions.size()) return;
    pushUndo();
    const std::string name = data_.actions[index].name;
    data_.actions.erase(data_.actions.begin() + static_cast<isize>(index));
    // A chord/combo naming the removed action would make the file unreloadable, so it goes too.
    rewriteRefs(name, std::string());
    // CASCADE -- see this method's own header comment for why an orphaned binding cannot be left.
    data_.bindings.erase(
        std::remove_if(data_.bindings.begin(), data_.bindings.end(),
                       [&](const fmt::OcInputBinding& b) { return b.action == name; }),
        data_.bindings.end());
    dirty_ = true;
}

void InputSchemeEditor::renameAction(usize index, std::string newName) {
    if (!loaded_ || index >= data_.actions.size()) return;
    if (newName.empty() || data_.actions[index].name == newName) return;
    const std::string oldName = data_.actions[index].name;
    // A referenced action must stay nameable inside a spec token.
    if (!safeRefName(newName)) {
        bool referenced = false;
        const auto scan = [&](const std::vector<std::string>& l) {
            for (const std::string& sp : l) if (triggerRefersTo(sp, oldName)) referenced = true;
        };
        for (const auto& a : data_.actions) scan(a.triggers);
        for (const auto& b : data_.bindings) scan(b.triggers);
        if (referenced) {
            specError_ = "\"" + oldName + "\" is used by a chord or combo; its name cannot contain : , # ; or spaces";
            return;
        }
    }
    pushUndo();
    data_.actions[index].name = newName;
    for (auto& b : data_.bindings) if (b.action == oldName) b.action = newName;
    rewriteRefs(oldName, newName);
    dirty_ = true;
}

void InputSchemeEditor::setActionType(usize index, fmt::OcInputValueType type) {
    if (!loaded_ || index >= data_.actions.size() || data_.actions[index].type == type) return;
    pushUndo();
    data_.actions[index].type = type;
    dirty_ = true;
}

// ---- bindings ----------------------------------------------------------------------------------

void InputSchemeEditor::addBinding() {
    if (!loaded_ || data_.actions.empty()) return;
    pushUndo();
    fmt::OcInputBinding b;
    b.action = data_.actions.front().name;
    b.source = fmt::OcInputSource::Key;
    b.key = kFwKeyNames[0];   // never empty -- a Key-source binding requires one (OcInput.hpp)
    data_.bindings.push_back(std::move(b));
    dirty_ = true;
}

void InputSchemeEditor::removeBinding(usize index) {
    if (!loaded_ || index >= data_.bindings.size()) return;
    pushUndo();
    data_.bindings.erase(data_.bindings.begin() + static_cast<isize>(index));
    dirty_ = true;
}

void InputSchemeEditor::setBindingAction(usize index, std::string actionName) {
    if (!loaded_ || index >= data_.bindings.size() || data_.bindings[index].action == actionName) return;
    pushUndo();
    data_.bindings[index].action = std::move(actionName);
    dirty_ = true;
}

void InputSchemeEditor::setBindingSource(usize index, fmt::OcInputSource source) {
    if (!loaded_ || index >= data_.bindings.size() || data_.bindings[index].source == source) return;
    pushUndo();
    fmt::OcInputBinding& b = data_.bindings[index];
    b.source = source;
    // See this method's own header comment: a Key/GamepadButton/GamepadAxis binding needs a
    // non-empty `key` the moment it becomes one of those three -- the PREVIOUS source (a mouse one)
    // may have left it empty, which parseOcinput refuses to reload ("BIND key requires a key name",
    // OcInput.cpp). Seed the first name in the NEW source's own vocabulary rather than leave a save
    // this tab could not itself reopen.
    if (b.key.empty()) {
        if (source == fmt::OcInputSource::Key) b.key = kFwKeyNames[0];
        else if (source == fmt::OcInputSource::GamepadButton) b.key = kFwGamepadButtonNames[0];
        else if (source == fmt::OcInputSource::GamepadAxis) b.key = kFwGamepadAxisNames[0];
    }
    dirty_ = true;
}

void InputSchemeEditor::setBindingKey(usize index, std::string key) {
    if (!loaded_ || index >= data_.bindings.size() || data_.bindings[index].key == key) return;
    pushUndo();
    data_.bindings[index].key = std::move(key);
    dirty_ = true;
}

void InputSchemeEditor::setBindingScale(usize index, f32 scale) {
    if (!loaded_ || index >= data_.bindings.size()) return;
    pushUndo();
    data_.bindings[index].scale = scale;
    dirty_ = true;
}

void InputSchemeEditor::setBindingComponent(usize index, i32 component) {
    if (!loaded_ || index >= data_.bindings.size() || data_.bindings[index].component == component) return;
    pushUndo();
    data_.bindings[index].component = component;
    dirty_ = true;
}

// ---- triggers and modifiers -------------------------------------------------------------------------

std::vector<std::string>* InputSchemeEditor::specList(SpecOwner owner, SpecKind kind) {
    const bool trig = kind == SpecKind::Trigger;
    if (owner.isAction) {
        if (owner.index >= data_.actions.size()) return nullptr;
        fmt::OcInputAction& a = data_.actions[owner.index];
        return trig ? &a.triggers : &a.modifiers;
    }
    if (owner.index >= data_.bindings.size()) return nullptr;
    fmt::OcInputBinding& b = data_.bindings[owner.index];
    return trig ? &b.triggers : &b.modifiers;
}

std::string InputSchemeEditor::ownerActionName(SpecOwner owner) const {
    if (owner.isAction) return owner.index < data_.actions.size() ? data_.actions[owner.index].name : std::string();
    return owner.index < data_.bindings.size() ? data_.bindings[owner.index].action : std::string();
}

bool InputSchemeEditor::validateSpec(SpecOwner owner, SpecKind kind, const std::string& spec, std::string& canonical) {
    std::string err;
    if (kind == SpecKind::Trigger) {
        fmt::OcTriggerSpec t;
        if (!fmt::parseTriggerSpec(spec, t, &err)) { specError_ = err; return false; }
        const std::string own = ownerActionName(owner);
        for (const std::string& r : t.refs) {
            bool declared = false;
            for (const auto& a : data_.actions) if (a.name == r) { declared = true; break; }
            if (!declared) { specError_ = "\"" + r + "\" is not a declared action"; return false; }
            if (t.kind == fmt::OcTriggerKind::Chord && r == own) {
                specError_ = "a chord cannot name its own action";
                return false;
            }
        }
        canonical = fmt::formatTriggerSpec(t);
    } else {
        fmt::OcModifierSpec m;
        if (!fmt::parseModifierSpec(spec, m, &err)) { specError_ = err; return false; }
        canonical = fmt::formatModifierSpec(m);
    }
    specError_.clear();
    return true;
}

bool InputSchemeEditor::addSpec(SpecOwner owner, SpecKind kind, std::string spec) {
    if (!loaded_) return false;
    if (!specList(owner, kind)) { specError_ = "no such action or binding"; return false; }
    std::string canonical;
    if (!validateSpec(owner, kind, spec, canonical)) return false;
    pushUndo();
    specList(owner, kind)->push_back(std::move(canonical));
    dirty_ = true;
    return true;
}

bool InputSchemeEditor::setSpec(SpecOwner owner, SpecKind kind, usize i, std::string spec) {
    if (!loaded_) return false;
    std::vector<std::string>* list = specList(owner, kind);
    if (!list || i >= list->size()) { specError_ = "no such entry"; return false; }
    std::string canonical;
    if (!validateSpec(owner, kind, spec, canonical)) return false;
    if ((*list)[i] == canonical) return true;
    pushUndo();
    (*specList(owner, kind))[i] = std::move(canonical);
    dirty_ = true;
    return true;
}

void InputSchemeEditor::removeSpec(SpecOwner owner, SpecKind kind, usize i) {
    if (!loaded_) return;
    std::vector<std::string>* list = specList(owner, kind);
    if (!list || i >= list->size()) return;
    pushUndo();
    list->erase(list->begin() + static_cast<isize>(i));
    dirty_ = true;
}

void InputSchemeEditor::moveSpec(SpecOwner owner, SpecKind kind, usize i, int delta) {
    if (!loaded_) return;
    std::vector<std::string>* list = specList(owner, kind);
    if (!list || i >= list->size() || delta == 0) return;
    const isize target = static_cast<isize>(i) + delta;
    if (target < 0 || target >= static_cast<isize>(list->size())) return;
    pushUndo();
    std::swap((*list)[i], (*list)[static_cast<usize>(target)]);
    dirty_ = true;
}

void InputSchemeEditor::rewriteRefs(const std::string& oldName, const std::string& newName) {
    const auto fix = [&](std::vector<std::string>& list) {
        for (usize i = 0; i < list.size();) {
            fmt::OcTriggerSpec t;
            if (!fmt::parseTriggerSpec(list[i], t) ||
                (t.kind != fmt::OcTriggerKind::Chord && t.kind != fmt::OcTriggerKind::Combo)) { ++i; continue; }
            bool hit = false;
            for (std::string& r : t.refs) if (r == oldName) { r = newName; hit = true; }
            if (!hit) { ++i; continue; }
            if (newName.empty()) { list.erase(list.begin() + static_cast<isize>(i)); continue; }
            list[i] = fmt::formatTriggerSpec(t);
            ++i;
        }
    };
    for (auto& a : data_.actions) fix(a.triggers);
    for (auto& b : data_.bindings) fix(b.triggers);
}

// ---- context ------------------------------------------------------------------------------------

void InputSchemeEditor::setContextName(std::string name) {
    if (!loaded_ || data_.contextName == name) return;
    pushUndo();
    data_.contextName = std::move(name);
    dirty_ = true;
}

void InputSchemeEditor::setContextPriority(i32 priority) {
    if (!loaded_ || data_.contextPriority == priority) return;
    pushUndo();
    data_.contextPriority = priority;
    dirty_ = true;
}

// ---- drawing --------------------------------------------------------------------------------------

#if AVER_WITH_IMGUI

namespace {
const char* bindingSourceLabel(fmt::OcInputSource s) {
    switch (s) {
        case fmt::OcInputSource::Key:           return "Key";
        case fmt::OcInputSource::MouseX:        return "Mouse X";
        case fmt::OcInputSource::MouseY:        return "Mouse Y";
        case fmt::OcInputSource::MouseWheel:    return "Mouse Wheel";
        case fmt::OcInputSource::GamepadButton: return "Gamepad Button";
        case fmt::OcInputSource::GamepadAxis:   return "Gamepad Axis";
        default:                                return "Key";
    }
}
const char* componentLabel(i32 c) { return c == 1 ? "Y" : c == 2 ? "Z" : "X"; }

// Every source the bindings table's combo offers, in the order the format's own grammar comment
// lists them (OcInput.cpp's header) plus the two gamepad sources point B of this change adds.
constexpr fmt::OcInputSource kAllSources[] = {
    fmt::OcInputSource::Key,        fmt::OcInputSource::MouseX,        fmt::OcInputSource::MouseY,
    fmt::OcInputSource::MouseWheel, fmt::OcInputSource::GamepadButton, fmt::OcInputSource::GamepadAxis,
};

// ---- trigger / modifier entry editors -------------------------------------------------------------

const char* const kTriggerKindNames[] = {"Down", "Pressed", "Released", "Hold", "Hold And Release", "Tap", "Pulse", "Chord", "Combo"};
const char* const kModifierKindNames[] = {"Dead Zone", "Scale", "Negate", "Swizzle", "Smooth", "Response Curve"};
const char* const kSwizzleNames[] = {"XYZ", "YXZ", "ZYX", "XZY", "YZX", "ZXY"};

// What one editor frame did to the entry: `changed` is a new value, `drag` says it came from a drag
// widget (so the editor coalesces a whole drag into one undo step), `activated` is a drag starting.
struct EditResult {
    bool changed = false, drag = false, activated = false;
};

bool dragFloat(EditResult& r, const char* label, float* v, float speed, float lo, float hi, float width = 70.0f) {
    ImGui::SetNextItemWidth(width);
    const bool ch = ImGui::DragFloat(label, v, speed, lo, hi, "%.3f");
    if (ImGui::IsItemActivated()) r.activated = true;
    if (ch) { r.changed = true; r.drag = true; }
    return ch;
}

fmt::OcTriggerSpec defaultTrigger(fmt::OcTriggerKind kind, const std::vector<std::string>& names, const std::string& own) {
    fmt::OcTriggerSpec t;
    t.kind = kind;
    switch (kind) {
        case fmt::OcTriggerKind::Hold:
        case fmt::OcTriggerKind::HoldRelease: t.time = 0.5f; break;
        case fmt::OcTriggerKind::Tap:         t.time = 0.2f; break;
        case fmt::OcTriggerKind::Pulse:       t.time = 0.25f; break;
        case fmt::OcTriggerKind::Chord:
            for (const std::string& n : names) if (n != own) { t.refs.push_back(n); break; }
            break;
        case fmt::OcTriggerKind::Combo:
            t.time = 0.4f;
            if (!names.empty()) { t.refs.push_back(names[0]); t.refs.push_back(names[names.size() > 1 ? 1 : 0]); }
            break;
        default: break;
    }
    return t;
}

fmt::OcModifierSpec defaultModifier(fmt::OcModifierKind kind) {
    fmt::OcModifierSpec m;
    m.kind = kind;
    switch (kind) {
        case fmt::OcModifierKind::Scale:
        case fmt::OcModifierKind::Curve:  m.v[0] = m.v[1] = m.v[2] = 1.0f; break;
        case fmt::OcModifierKind::Smooth: m.v[0] = 0.05f; break;
        default: break;
    }
    return m;
}

bool actionCombo(const char* label, std::string& value, const std::vector<std::string>& names, const std::string& skip, float width) {
    bool ch = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, value.empty() ? "(pick one)" : value.c_str())) {
        for (const std::string& n : names) {
            if (n == skip) continue;
            if (ImGui::Selectable(n.c_str(), n == value)) { value = n; ch = true; }
        }
        ImGui::EndCombo();
    }
    return ch;
}

EditResult editTrigger(fmt::OcTriggerSpec& t, const std::vector<std::string>& names, const std::string& own) {
    EditResult r;
    const auto actuation = [&] { dragFloat(r, "act", &t.actuation, 0.01f, 0.0f, 1.0f, 60.0f); };
    switch (t.kind) {
        case fmt::OcTriggerKind::Down:
        case fmt::OcTriggerKind::Pressed:
        case fmt::OcTriggerKind::Released:
            actuation();
            break;
        case fmt::OcTriggerKind::Hold:
            dragFloat(r, "sec", &t.time, 0.01f, 0.0f, 60.0f);
            ImGui::SameLine(); actuation();
            ImGui::SameLine();
            if (ImGui::Checkbox("once", &t.once)) r.changed = true;
            break;
        case fmt::OcTriggerKind::HoldRelease:
            dragFloat(r, "sec", &t.time, 0.01f, 0.0f, 60.0f);
            ImGui::SameLine(); actuation();
            break;
        case fmt::OcTriggerKind::Tap:
            dragFloat(r, "max sec", &t.time, 0.01f, 0.0f, 10.0f);
            ImGui::SameLine(); actuation();
            break;
        case fmt::OcTriggerKind::Pulse: {
            dragFloat(r, "interval", &t.time, 0.01f, 0.01f, 60.0f);
            ImGui::SameLine();
            int limit = t.limit;
            ImGui::SetNextItemWidth(60.0f);
            if (ImGui::DragInt("limit", &limit, 0.2f, 0, 1000)) { t.limit = limit < 0 ? 0 : limit; r.changed = true; r.drag = true; }
            if (ImGui::IsItemActivated()) r.activated = true;
            ImGui::SameLine(); actuation();
            ImGui::SameLine();
            if (ImGui::Checkbox("on start", &t.onStart)) r.changed = true;
            break;
        }
        case fmt::OcTriggerKind::Chord: {
            if (t.refs.empty()) t.refs.emplace_back();
            if (actionCombo("##chord", t.refs[0], names, own, 160.0f)) r.changed = true;
            break;
        }
        case fmt::OcTriggerKind::Combo:
            dragFloat(r, "window", &t.time, 0.01f, 0.01f, 10.0f);
            for (usize k = 0; k < t.refs.size(); ++k) {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(k));
                if (actionCombo("##step", t.refs[k], names, std::string(), 110.0f)) r.changed = true;
                ImGui::PopID();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("+ step") && !names.empty()) { t.refs.push_back(names[0]); r.changed = true; }
            if (t.refs.size() > 2) {
                ImGui::SameLine();
                if (ImGui::SmallButton("- step")) { t.refs.pop_back(); r.changed = true; }
            }
            break;
    }
    return r;
}

EditResult editModifier(fmt::OcModifierSpec& m) {
    EditResult r;
    switch (m.kind) {
        case fmt::OcModifierKind::DeadZone: {
            dragFloat(r, "lower", &m.v[0], 0.01f, 0.0f, 0.99f, 60.0f);
            ImGui::SameLine();
            dragFloat(r, "upper", &m.v[1], 0.01f, 0.01f, 1.0f, 60.0f);
            if (m.v[0] >= m.v[1]) m.v[0] = m.v[1] - 0.01f;   // the format needs lower < upper
            ImGui::SameLine();
            int mode = m.mode == 1 ? 1 : 0;
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##dzmode", &mode, "Radial\0Axial\0")) { m.mode = mode; r.changed = true; }
            break;
        }
        case fmt::OcModifierKind::Scale:
        case fmt::OcModifierKind::Curve: {
            const bool curve = m.kind == fmt::OcModifierKind::Curve;
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::DragFloat3(curve ? "exponent xyz" : "scale xyz", m.v, 0.01f, curve ? 0.05f : -100.0f, 100.0f, "%.3f")) {
                r.changed = true; r.drag = true;
            }
            if (ImGui::IsItemActivated()) r.activated = true;
            break;
        }
        case fmt::OcModifierKind::Negate: {
            bool x = (m.mask & 1) != 0, y = (m.mask & 2) != 0, z = (m.mask & 4) != 0;
            bool ch = ImGui::Checkbox("X", &x);
            ImGui::SameLine(); ch |= ImGui::Checkbox("Y", &y);
            ImGui::SameLine(); ch |= ImGui::Checkbox("Z", &z);
            if (ch) {
                m.mask = static_cast<u8>((x ? 1 : 0) | (y ? 2 : 0) | (z ? 4 : 0));
                r.changed = true;
            }
            break;
        }
        case fmt::OcModifierKind::Swizzle: {
            int mode = m.mode;
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##swizzle", &mode, kSwizzleNames, IM_ARRAYSIZE(kSwizzleNames))) { m.mode = mode; r.changed = true; }
            break;
        }
        case fmt::OcModifierKind::Smooth:
            dragFloat(r, "time constant", &m.v[0], 0.005f, 0.0f, 5.0f, 90.0f);
            break;
    }
    return r;
}
} // namespace

void InputSchemeEditor::drawContext() {
    ImGui::SeparatorText("Context");
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof nameBuf, "%s", data_.contextName.c_str());
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("Context name", "(none -- writes no CONTEXT record)", nameBuf, sizeof nameBuf);
    if (ImGui::IsItemDeactivatedAfterEdit()) setContextName(nameBuf);
    ImGui::TextDisabled("The name EnhancedInput.AddContext registers this scheme under.");

    int priority = data_.contextPriority;
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("Priority", &priority);
    if (ImGui::IsItemDeactivatedAfterEdit()) setContextPriority(priority);
    ImGui::TextDisabled("Higher priority consumes a key before a lower-priority context sees it.");
}

void InputSchemeEditor::drawActions() {
    ImGui::SeparatorText("Actions");
    static const char* kTypeNames[] = {"Digital", "Axis1D", "Axis2D"};
#if AVER_MODULE_FRAMEWORK
    // While the editor is playing, show each action's trigger state and how long it has been active.
    const bool playing = aver_fw_play_state() == AVER_FW_PLAY_PLAYING;
    constexpr int kColumns = 5;
#else
    constexpr int kColumns = 4;
#endif
    if (ImGui::BeginTable("inputActions", kColumns, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                                    ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Config", ImGuiTableColumnFlags_WidthFixed, 70.0f);
#if AVER_MODULE_FRAMEWORK
        ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, 130.0f);
#endif
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();
        // Direct in-loop removal, size checked fresh each iteration -- GraphEditor's own function-
        // pin list (GraphEditor.cpp) removes the same way: at most one row is skipped for the
        // remainder of THIS frame, which the next frame's redraw fixes, and nothing here holds a
        // reference across the erase.
        for (usize i = 0; i < data_.actions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            char nameBuf[96];
            std::snprintf(nameBuf, sizeof nameBuf, "%s", data_.actions[i].name.c_str());
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##name", nameBuf, sizeof nameBuf);
            if (ImGui::IsItemDeactivatedAfterEdit()) renameAction(i, nameBuf);

            ImGui::TableNextColumn();
            int typeIdx = static_cast<int>(data_.actions[i].type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##type", &typeIdx, kTypeNames, IM_ARRAYSIZE(kTypeNames)))
                setActionType(i, static_cast<fmt::OcInputValueType>(typeIdx));

            ImGui::TableNextColumn();
            const usize nT = data_.actions[i].triggers.size(), nM = data_.actions[i].modifiers.size();
            if (nT + nM == 0) ImGui::TextDisabled("-");
            else ImGui::Text("%zuT %zuM", nT, nM);

#if AVER_MODULE_FRAMEWORK
            ImGui::TableNextColumn();
            if (playing) {
                const int32_t h = aver_fw_action_find(data_.actions[i].name.c_str());
                const int32_t st = h ? aver_fw_action_state(h) : AVER_FW_TRIGGER_STATE_NONE;
                const ImVec4 col = st == AVER_FW_TRIGGER_STATE_TRIGGERED ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                 : st == AVER_FW_TRIGGER_STATE_ONGOING   ? ImVec4(0.95f, 0.8f, 0.3f, 1.0f)
                                                                         : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                const char* label = st == AVER_FW_TRIGGER_STATE_TRIGGERED ? "Triggered"
                                  : st == AVER_FW_TRIGGER_STATE_ONGOING   ? "Ongoing" : "None";
                ImGui::TextColored(col, "%s %.2fs", label, h ? aver_fw_action_elapsed(h) : 0.0f);
            } else {
                ImGui::TextDisabled("-");
            }
#endif

            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_DELETE)) removeAction(i);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(ICON_ADD " Add Action")) addAction();

    // Triggers (when the action fires) and modifiers (how its value is shaped) on the action itself.
    for (usize i = 0; i < data_.actions.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = data_.actions[i].name + "  (triggers and modifiers)###cfg";
        if (ImGui::TreeNode(label.c_str())) {
            drawSpecs(SpecOwner{true, i});
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void InputSchemeEditor::drawBindings() {
    ImGui::SeparatorText("Bindings");
    if (data_.actions.empty()) {
        ImGui::TextDisabled("Declare an action above before adding a binding.");
        return;
    }
    if (ImGui::BeginTable("inputBindings", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Scale", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();
        for (usize i = 0; i < data_.bindings.size(); ++i) {
            const fmt::OcInputBinding& b = data_.bindings[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##action", b.action.c_str())) {
                for (const auto& a : data_.actions)
                    if (ImGui::Selectable(a.name.c_str(), a.name == b.action)) setBindingAction(i, a.name);
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##source", bindingSourceLabel(b.source))) {
                for (fmt::OcInputSource s : kAllSources)
                    if (ImGui::Selectable(bindingSourceLabel(s), s == b.source)) setBindingSource(i, s);
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            // Key/GamepadButton/GamepadAxis pick a NAME from that source's own vocabulary; the three
            // mouse sources ignore `key` entirely (OcInputBinding::key's own comment), so the column
            // shows nothing editable for them rather than a combo with no meaningful entries.
            if (b.source == fmt::OcInputSource::Key || b.source == fmt::OcInputSource::GamepadButton ||
                b.source == fmt::OcInputSource::GamepadAxis) {
                const char* const* names = b.source == fmt::OcInputSource::Key           ? kFwKeyNames
                                          : b.source == fmt::OcInputSource::GamepadButton ? kFwGamepadButtonNames
                                                                                          : kFwGamepadAxisNames;
                const usize count = b.source == fmt::OcInputSource::Key           ? kFwKeyNameCount
                                  : b.source == fmt::OcInputSource::GamepadButton ? kFwGamepadButtonNameCount
                                                                                  : kFwGamepadAxisNameCount;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##key", b.key.empty() ? "(pick one)" : b.key.c_str())) {
                    for (usize k = 0; k < count; ++k)
                        if (ImGui::Selectable(names[k], b.key == names[k])) setBindingKey(i, names[k]);
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            float scale = b.scale;
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##scale", &scale, 0.05f, -100.0f, 100.0f, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) setBindingScale(i, scale);

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##component", componentLabel(b.component))) {
                for (i32 c = 0; c < 3; ++c)
                    if (ImGui::Selectable(componentLabel(c), c == b.component)) setBindingComponent(i, c);
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_DELETE)) removeBinding(i);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(ICON_ADD " Add Binding")) addBinding();

    // A binding's own triggers and modifiers act on that binding's value alone, before the action sums
    // it. A radial dead zone belongs on the action instead: a binding carries a single channel.
    for (usize i = 0; i < data_.bindings.size(); ++i) {
        const fmt::OcInputBinding& b = data_.bindings[i];
        ImGui::PushID(static_cast<int>(i));
        std::string label = b.action + "  " + bindingSourceLabel(b.source);
        if (!b.key.empty()) label += " " + b.key;
        if (!b.triggers.empty() || !b.modifiers.empty())
            label += "  (" + std::to_string(b.triggers.size()) + "T " + std::to_string(b.modifiers.size()) + "M)";
        label += "###bcfg";
        if (ImGui::TreeNode(label.c_str())) {
            drawSpecs(SpecOwner{false, i});
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void InputSchemeEditor::drawSpecs(SpecOwner owner) {
    std::vector<std::string> names;
    for (const auto& a : data_.actions) names.push_back(a.name);
    const std::string own = ownerActionName(owner);

    for (const SpecKind kind : {SpecKind::Trigger, SpecKind::Modifier}) {
        const bool trig = kind == SpecKind::Trigger;
        ImGui::PushID(trig ? 0 : 1);
        ImGui::TextDisabled(trig ? "Triggers (an action with none fires while held)" : "Modifiers (applied in order)");

        for (usize i = 0;; ) {
            const std::vector<std::string>* list = specList(owner, kind);
            if (!list || i >= list->size()) break;
            const std::string spec = (*list)[i];
            ImGui::PushID(static_cast<int>(i));

            std::string replacement;
            EditResult res;
            if (trig) {
                fmt::OcTriggerSpec t;
                if (fmt::parseTriggerSpec(spec, t)) {
                    int k = static_cast<int>(t.kind);
                    ImGui::SetNextItemWidth(130.0f);
                    if (ImGui::Combo("##kind", &k, kTriggerKindNames, IM_ARRAYSIZE(kTriggerKindNames))) {
                        t = defaultTrigger(static_cast<fmt::OcTriggerKind>(k), names, own);
                        res.changed = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", spec.c_str());
                    ImGui::SameLine();
                    const EditResult e = editTrigger(t, names, own);
                    res.changed |= e.changed; res.drag |= e.drag; res.activated |= e.activated;
                    if (res.changed) replacement = fmt::formatTriggerSpec(t);
                } else {
                    ImGui::TextDisabled("%s (unreadable)", spec.c_str());
                }
            } else {
                fmt::OcModifierSpec m;
                if (fmt::parseModifierSpec(spec, m)) {
                    int k = static_cast<int>(m.kind);
                    ImGui::SetNextItemWidth(130.0f);
                    if (ImGui::Combo("##kind", &k, kModifierKindNames, IM_ARRAYSIZE(kModifierKindNames))) {
                        m = defaultModifier(static_cast<fmt::OcModifierKind>(k));
                        res.changed = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", spec.c_str());
                    ImGui::SameLine();
                    const EditResult e = editModifier(m);
                    res.changed |= e.changed; res.drag |= e.drag; res.activated |= e.activated;
                    if (res.changed) replacement = fmt::formatModifierSpec(m);
                } else {
                    ImGui::TextDisabled("%s (unreadable)", spec.c_str());
                }
            }

            ImGui::SameLine();
            int move = 0;
            bool remove = false;
            if (ImGui::SmallButton("Up")) move = -1;
            ImGui::SameLine();
            if (ImGui::SmallButton("Dn")) move = 1;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_DELETE)) remove = true;
            ImGui::PopID();

            if (res.activated) specDragUndone_ = false;
            if (!replacement.empty()) {
                if (res.drag) {
                    // One undo step for a whole drag; the value still lands every frame.
                    std::string canonical;
                    if (validateSpec(owner, kind, replacement, canonical) && (*specList(owner, kind))[i] != canonical) {
                        if (!specDragUndone_) { pushUndo(); specDragUndone_ = true; }
                        (*specList(owner, kind))[i] = std::move(canonical);
                        dirty_ = true;
                    }
                } else {
                    setSpec(owner, kind, i, replacement);
                }
            }
            if (remove) { removeSpec(owner, kind, i); continue; }
            if (move != 0) moveSpec(owner, kind, i, move);
            ++i;
        }

        const char* popup = trig ? "addTrigger" : "addModifier";
        if (ImGui::SmallButton(trig ? ICON_ADD " Trigger" : ICON_ADD " Modifier")) ImGui::OpenPopup(popup);
        if (ImGui::BeginPopup(popup)) {
            if (trig) {
                for (int k = 0; k < IM_ARRAYSIZE(kTriggerKindNames); ++k)
                    if (ImGui::Selectable(kTriggerKindNames[k]))
                        addSpec(owner, kind, fmt::formatTriggerSpec(defaultTrigger(static_cast<fmt::OcTriggerKind>(k), names, own)));
            } else {
                for (int k = 0; k < IM_ARRAYSIZE(kModifierKindNames); ++k)
                    if (ImGui::Selectable(kModifierKindNames[k]))
                        addSpec(owner, kind, fmt::formatModifierSpec(defaultModifier(static_cast<fmt::OcModifierKind>(k))));
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void InputSchemeEditor::drawProjectSchemeLine() {
    if (!g_hooks.contentDir) return;
    const std::string contentDir = g_hooks.contentDir();
    if (contentDir.empty()) return;   // no project open -- nothing to compare against
    std::error_code ec;
    std::string rel = std::filesystem::relative(path_, contentDir, ec).string();
    if (ec || rel.empty()) return;
    for (char& c : rel) if (c == '\\') c = '/';   // content-relative paths are stored '/'-separated
    const std::string projScheme = g_hooks.projectInputScheme ? g_hooks.projectInputScheme() : std::string();
    if (rel == projScheme) return;   // already the project's scheme -- nothing to offer

    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::Separator();
    if (projScheme.empty())
        ImGui::TextDisabled("This project has no Input Scheme set (Project Settings > Description).");
    else
        ImGui::TextDisabled("This is not the project's Input Scheme ('%s').", projScheme.c_str());
    ImGui::BeginDisabled(!g_hooks.useAsProjectInputScheme);
    if (ImGui::Button("Use as Project Input Scheme") && g_hooks.useAsProjectInputScheme)
        g_hooks.useAsProjectInputScheme(rel);
    ImGui::EndDisabled();
}

void InputSchemeEditor::draw(Engine& e) {
    (void)e;
    if (!loaded_) {
        ImGui::TextWrapped("This file could not be read: %s", loadError_.c_str());
        return;
    }

    if (ImGui::Button(ICON_SAVE " Save") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         editor::keybinds().pressed(editor::CommandId::AssetSave, ImGui::GetIO()))) {
        std::string why;
        if (!save(&why)) AVER_ERROR("[InputSchemeEditor] save failed for '{}': {}", path_, why);
    }
    // Ctrl+Z / Ctrl+Y, skipped while an InputText has focus -- FoliageTypeEditor::draw()'s own
    // comment states the reason (WantTextInput is how GraphEditor's canvas tells the two apart).
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (focused && !io.WantTextInput) {
            if (canUndo() && editor::keybinds().pressed(editor::CommandId::EditUndo, io)) undo();
            if (canRedo() && editor::keybinds().pressed(editor::CommandId::EditRedo, io)) redo();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::Button(ICON_UNDO " Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::Button(ICON_REDO " Redo")) redo();
    ImGui::EndDisabled();

    ImGui::Separator();
    drawContext();
    ImGui::Dummy(ImVec2(0, 6.0f));
    drawActions();
    ImGui::Dummy(ImVec2(0, 6.0f));
    drawBindings();
    if (!specError_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", specError_.c_str());
    drawProjectSchemeLine();
}

#else   // AVER_WITH_IMGUI

// The headless build (and tests/editor's own target, which deliberately leaves AVER_WITH_IMGUI
// undefined) still gets load/save/dirty/undo/every field setter; only the window is absent --
// FoliageTypeEditor/BtEditor/SoundEditor's own #else branches do exactly this.
void InputSchemeEditor::draw(Engine& e) { (void)e; }

#endif  // AVER_WITH_IMGUI

std::unique_ptr<AssetEditor> makeInputSchemeEditor(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".ocinput") return nullptr;
    return std::make_unique<InputSchemeEditor>(path);
}

} // namespace aver::editor
