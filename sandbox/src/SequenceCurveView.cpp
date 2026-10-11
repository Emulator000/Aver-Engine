// The Sequencer's Curves view: the selected track's channels as time x value curves, drawn by sampling
// game::sampleSeqChannel (the sampler the runtime uses), with tangent handles on the selected key.
// Part of SequenceEditor; the editing itself is SequenceCurveOps. docs/SEQUENCER.md.
#include "SequenceEditor.hpp"

#if AVER_MODULE_SCENE

#include "SequenceCurveOps.hpp"
#include "SequenceTrackOps.hpp"

#include "aver/scene/World.hpp"

#if AVER_WITH_IMGUI
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace aver::editor {

#if AVER_WITH_IMGUI

namespace {

using fmt::OcSeqInterp;
using fmt::OcSeqKey;
using fmt::OcSeqTrack;
using fmt::OcSeqTrackKind;

struct Channel {
    std::string name;
    ImU32 colour;
};

// The channels a track edits as curves, with their names and colours.
std::vector<Channel> channelsOf(const OcSeqTrack& tr, u32 propertyArity) {
    const ImU32 red = IM_COL32(235, 90, 90, 255), green = IM_COL32(110, 205, 110, 255), blue = IM_COL32(100, 155, 245, 255);
    const ImU32 amber = IM_COL32(235, 205, 90, 255), violet = IM_COL32(205, 125, 235, 255), cyan = IM_COL32(90, 215, 215, 255);
    const ImU32 grey = IM_COL32(200, 200, 205, 255);
    switch (tr.kind) {
        case OcSeqTrackKind::Transform:
            return {{"X", red}, {"Y", green}, {"Z", blue}, {"Yaw", amber}, {"Pitch", violet}, {"Roll", cyan},
                    {"Scale X", red}, {"Scale Y", green}, {"Scale Z", blue}};
        case OcSeqTrackKind::Camera:
            return {{"X", red}, {"Y", green}, {"Z", blue}, {"Yaw", amber}, {"Pitch", violet}};
        case OcSeqTrackKind::Material:
            return {{"R", red}, {"G", green}, {"B", blue}, {"Intensity", grey}};
        case OcSeqTrackKind::Property: {
            const bool colour = tr.property.find("olour") != std::string::npos || tr.property.find("olor") != std::string::npos;
            if (propertyArity == 1) return {{"Value", amber}};
            if (propertyArity == 3) return colour ? std::vector<Channel>{{"R", red}, {"G", green}, {"B", blue}}
                                                  : std::vector<Channel>{{"X", red}, {"Y", green}, {"Z", blue}};
            return {{"X", red}, {"Y", green}, {"Z", blue}, {"W", grey}};
        }
        default: return {};
    }
}

f64 niceStep(f64 range, int target) {
    const f64 raw = range / std::max(target, 1);
    const f64 mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-12))));
    const f64 n = raw / mag;
    return (n < 1.5 ? 1.0 : n < 3.5 ? 2.0 : n < 7.5 ? 5.0 : 10.0) * mag;
}

} // namespace

void SequenceEditor::drawCurveView(SequenceHost&, f32 laneX0, f32 nameW, f32 laneW, f32 ui) {
    ImGuiIO& io = ImGui::GetIO();
    const f64 len = std::fmax(model_.length, 0.01);
    const f32 minPps = static_cast<f32>(laneW / len);
    const f32 pps = pxPerSec_ > minPps ? std::fmin(pxPerSec_, std::fmax(minPps, 4000.0f * ui)) : minPps;
    clampSelection();

    const bool haveTrack = selTrack_ >= 0 && static_cast<usize>(selTrack_) < model_.tracks.size();
    const bool curved = haveTrack && (model_.tracks[static_cast<usize>(selTrack_)].kind == OcSeqTrackKind::Transform ||
                                      model_.tracks[static_cast<usize>(selTrack_)].kind == OcSeqTrackKind::Camera ||
                                      model_.tracks[static_cast<usize>(selTrack_)].kind == OcSeqTrackKind::Material ||
                                      model_.tracks[static_cast<usize>(selTrack_)].kind == OcSeqTrackKind::Property);
    if (!curved) {
        ImGui::TextDisabled("Select a transform, camera, emissive or property track to edit its curves.");
        return;
    }
    OcSeqTrack& tr = model_.tracks[static_cast<usize>(selTrack_)];
    u32 arity = 1;
    if (tr.kind == OcSeqTrackKind::Property) {
        scene::World& w = scene::World::instance();
        if (const scene::FieldDesc* d = w.field(w.fieldId(tr.property))) arity = seqFieldArity(d->kind);
    }
    const std::vector<Channel> chans = channelsOf(tr, arity);
    const u32 nch = static_cast<u32>(chans.size());
    u32 visible = 0;
    for (u32 c = 0; c < nch; ++c) if (!(curve_.hidden & (1u << c))) visible |= 1u << c;

    // ---- toolbar for the selected keys ----
    const bool haveSel = selectedCount() > 0;
    ImGui::BeginDisabled(!haveSel);
    const auto forSelected = [&](auto&& fn) {
        pushUndo();
        for (usize i = 0; i < tr.keys.size() && i < keySel_.size(); ++i) if (keySel_[i]) fn(i);
        edited();
    };
    if (ImGui::SmallButton("Auto")) forSelected([&](usize i) { seqSetTangentMode(tr, i, SeqTangentMode::Auto); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Linear")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Cubic); seqSetTangentMode(tr, i, SeqTangentMode::Linear); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Flat")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Cubic); seqSetTangentMode(tr, i, SeqTangentMode::Flat); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Step")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Step); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Smooth")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Smooth); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Cubic (user)")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Cubic); seqSetTangentMode(tr, i, SeqTangentMode::User); });
    ImGui::SameLine();
    if (ImGui::SmallButton("Break")) forSelected([&](usize i) { seqSetKeyInterp(tr, i, OcSeqInterp::Cubic); seqSetTangentMode(tr, i, SeqTangentMode::Broken); });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit  (F)")) curve_.fit = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cubic keys use the handles (slopes); Auto handles follow the neighbours.\n"
                          "Drag a key for time and value, a handle for its slope (Alt breaks the pair),\n"
                          "double-click the curve to add a key. Smooth keeps the classic spline; Cubic is Hermite on the raw numbers\n"
                          "(rotations included: key 370 to go the long way).");

    // ---- channel list (left) and plot (right) ----
    ImGui::BeginChild("##curveList", ImVec2(nameW, 0), false);
    for (u32 c = 0; c < nch; ++c) {
        ImGui::PushID(static_cast<int>(c));
        bool shown = !(curve_.hidden & (1u << c));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(chans[c].colour));
        if (ImGui::Checkbox(chans[c].name.c_str(), &shown)) {
            if (shown) curve_.hidden &= ~(1u << c); else curve_.hidden |= 1u << c;
            curve_.fit = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine(laneX0 - ImGui::GetWindowPos().x, 0.0f);   // the plot's time axis is the ruler's

    ImGui::BeginChild("##curvePlot", ImVec2(laneW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const f32 pw = std::fmax(avail.x, 40.0f), ph = std::fmax(avail.y, 40.0f);
    const ImVec2 p1(p0.x + pw, p0.y + ph);
    ImGui::InvisibleButton("##plot", ImVec2(pw, ph));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Value range: fitted to the visible channels' keys with a margin, until the user zooms or pans.
    if (curve_.fit) {
        f64 t0, t1, lo, hi;
        seqCurveBounds(tr, visible, arity, t0, t1, lo, hi);
        const f64 pad = (hi - lo) * 0.12;
        curve_.v0 = lo - pad;
        curve_.v1 = hi + pad;
        curve_.fit = false;
    }
    if (curve_.v1 - curve_.v0 < 1e-9) curve_.v1 = curve_.v0 + 1.0;
    const auto px = [&](f64 t) { return p0.x + static_cast<f32>((t - viewStart_) * pps); };
    const auto py = [&](f64 v) { return p1.y - static_cast<f32>((v - curve_.v0) / (curve_.v1 - curve_.v0) * ph); };
    const auto timeAt = [&](f32 x) { return viewStart_ + static_cast<f64>(x - p0.x) / pps; };
    const auto valueAt = [&](f32 y) { return curve_.v0 + static_cast<f64>(p1.y - y) / ph * (curve_.v1 - curve_.v0); };

    // Wheel: Ctrl = time zoom, Alt = value zoom about the mouse; middle-drag pans both.
    if (hovered) {
        const f32 mx = io.MousePos.x - p0.x;
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            const f32 target = std::clamp(pps * std::pow(1.2f, io.MouseWheel), minPps, std::fmax(minPps, 4000.0f * ui));
            const f64 anchor = viewStart_ + mx / pps;
            pxPerSec_ = target <= minPps * 1.001f ? 0.0f : target;
            const f32 np = pxPerSec_ > minPps ? pxPerSec_ : minPps;
            viewStart_ = std::clamp(anchor - mx / np, 0.0, std::fmax(0.0, len - laneW / np));
        } else if (io.KeyAlt && io.MouseWheel != 0.0f) {
            const f64 anchor = valueAt(io.MousePos.y);
            const f64 k = std::pow(1.2, -static_cast<f64>(io.MouseWheel));
            curve_.v0 = anchor - (anchor - curve_.v0) * k;
            curve_.v1 = anchor + (curve_.v1 - anchor) * k;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            viewStart_ = std::clamp(viewStart_ - io.MouseDelta.x / pps, 0.0, std::fmax(0.0, len - laneW / pps));
            const f64 dv = io.MouseDelta.y / ph * (curve_.v1 - curve_.v0);
            curve_.v0 += dv;
            curve_.v1 += dv;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.WantTextInput) curve_.fit = true;
    }

    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, IM_COL32(26, 26, 30, 255));
    // Grid: value lines at nice steps, the time axis shares the ruler's span.
    {
        const f64 step = niceStep(curve_.v1 - curve_.v0, std::max(2, static_cast<int>(ph / (44.0f * ui))));
        for (f64 v = std::ceil(curve_.v0 / step) * step; v <= curve_.v1; v += step) {
            const f32 y = py(v);
            dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), std::fabs(v) < step * 1e-6 ? IM_COL32(90, 90, 100, 255) : IM_COL32(44, 44, 52, 255));
            char b[32];
            std::snprintf(b, sizeof b, "%g", v);
            dl->AddText(ImVec2(p0.x + 3.0f, y - ImGui::GetTextLineHeight()), IM_COL32(120, 120, 130, 255), b);
        }
        const f32 ex = px(len);
        dl->AddLine(ImVec2(ex, p0.y), ImVec2(ex, p1.y), IM_COL32(220, 90, 90, 160), 2.0f);
    }

    // The curves, exactly as the runtime samples them (every 4 px).
    std::vector<ImVec2> pts;
    for (u32 c = 0; c < nch; ++c) {
        if (!(visible & (1u << c))) continue;
        pts.clear();
        for (f32 x = p0.x; x <= p1.x + 4.0f; x += 4.0f) {
            const f64 t = std::clamp(timeAt(x), 0.0, len);
            pts.push_back(ImVec2(x, py(game::sampleSeqChannel(tr, c, t))));
        }
        if (pts.size() >= 2) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), chans[c].colour, 0, 1.6f);
    }

    // Keys as squares; the primary selected key shows its handles.
    const f32 kr = 4.5f * ui;
    struct Hit { int key = -1; u32 channel = 0; int kind = 0; };   // kind: 1 key, 2 arrive handle, 3 leave handle
    Hit hit;
    f32 hitBest = 8.0f * ui;
    const auto consider = [&](int key, u32 c, int kind, ImVec2 at) {
        const f32 d = std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y);
        if (d <= hitBest) { hitBest = d; hit = {key, c, kind}; }
    };
    for (u32 c = 0; c < nch; ++c) {
        if (!(visible & (1u << c))) continue;
        for (usize k = 0; k < tr.keys.size(); ++k) {
            const OcSeqKey& key = tr.keys[k];
            const ImVec2 at(px(key.t), py(key.v[c]));
            const bool ks = k < keySel_.size() && keySel_[k] != 0;
            dl->AddRectFilled(ImVec2(at.x - kr, at.y - kr), ImVec2(at.x + kr, at.y + kr), ks ? IM_COL32(255, 255, 255, 255) : chans[c].colour);
            dl->AddRect(ImVec2(at.x - kr, at.y - kr), ImVec2(at.x + kr, at.y + kr), IM_COL32(0, 0, 0, 180));
            consider(static_cast<int>(k), c, 1, at);
            if (static_cast<int>(k) != selKey_) continue;
            // Handles: a third of the neighbouring span, from the effective slopes.
            f64 arrive = 0, leave = 0;
            game::seqEffectiveTangent(tr.keys, k, c, arrive, leave);
            const f64 dtPrev = k > 0 ? key.t - tr.keys[k - 1].t : (k + 1 < tr.keys.size() ? tr.keys[k + 1].t - key.t : 1.0);
            const f64 dtNext = k + 1 < tr.keys.size() ? tr.keys[k + 1].t - key.t : dtPrev;
            const bool leaving = key.interp == OcSeqInterp::Cubic && k + 1 < tr.keys.size();
            const bool arriving = k > 0 && tr.keys[k - 1].interp == OcSeqInterp::Cubic;
            if (arriving) {
                const ImVec2 h(px(key.t - dtPrev / 3.0), py(key.v[c] - arrive * dtPrev / 3.0));
                dl->AddLine(at, h, IM_COL32(200, 200, 200, 180));
                dl->AddCircleFilled(h, 3.5f * ui, IM_COL32(230, 230, 230, 255));
                consider(static_cast<int>(k), c, 2, h);
            }
            if (leaving) {
                const ImVec2 h(px(key.t + dtNext / 3.0), py(key.v[c] + leave * dtNext / 3.0));
                dl->AddLine(at, h, IM_COL32(200, 200, 200, 180));
                dl->AddCircleFilled(h, 3.5f * ui, IM_COL32(230, 230, 230, 255));
                consider(static_cast<int>(k), c, 3, h);
            }
        }
    }
    const f32 phx = px(player_.time());
    dl->AddLine(ImVec2(phx, p0.y), ImVec2(phx, p1.y), IM_COL32(255, 190, 70, 190), 1.0f);

    // ---- mouse ----
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        const bool additive = io.KeyCtrl || io.KeyShift;
        if (hit.key >= 0) {
            if (hit.kind == 1) {
                if (additive) {
                    keySel_[static_cast<usize>(hit.key)] = keySel_[static_cast<usize>(hit.key)] ? char(0) : char(1);
                    selKey_ = keySel_[static_cast<usize>(hit.key)] ? hit.key : -1;
                } else if (!keySel_[static_cast<usize>(hit.key)]) {
                    selectOnly(hit.key);
                } else {
                    selKey_ = hit.key;
                }
            }
            curve_.dragKind = hit.kind;
            curve_.dragKey = hit.key;
            curve_.dragChannel = hit.channel;
            curve_.dragMoved = false;
            ++editRev_;
        } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            pushUndo();
            const int idx = static_cast<int>(seqInsertKeyAt(tr, std::clamp(timeAt(io.MousePos.x), 0.0, len), arity));
            keySel_.assign(tr.keys.size(), 0);
            selectOnly(idx);
            edited();
            curve_.dragKind = 0;
        } else {
            curve_.dragKind = 4;   // box select
            curve_.boxX0 = io.MousePos.x;
            curve_.boxY0 = io.MousePos.y;
            if (!additive) selectOnly(-1);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (curve_.dragKind == 4) {
            const f32 x0 = std::min(curve_.boxX0, io.MousePos.x), x1 = std::max(curve_.boxX0, io.MousePos.x);
            const f32 y0 = std::min(curve_.boxY0, io.MousePos.y), y1 = std::max(curve_.boxY0, io.MousePos.y);
            for (usize k = 0; k < tr.keys.size() && k < keySel_.size(); ++k)
                for (u32 c = 0; c < nch; ++c) {
                    if (!(visible & (1u << c))) continue;
                    const f32 x = px(tr.keys[k].t), y = py(tr.keys[k].v[c]);
                    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) { keySel_[k] = 1; if (selKey_ < 0) selKey_ = static_cast<int>(k); }
                }
            ++editRev_;
        }
        curve_.dragKind = 0;
    }
    if (curve_.dragKind == 4) {
        dl->AddRect(ImVec2(curve_.boxX0, curve_.boxY0), io.MousePos, IM_COL32(255, 255, 255, 160));
        dl->AddRectFilled(ImVec2(curve_.boxX0, curve_.boxY0), io.MousePos, IM_COL32(255, 255, 255, 24));
    } else if (curve_.dragKind != 0 && curve_.dragKey >= 0 && static_cast<usize>(curve_.dragKey) < tr.keys.size()) {
        const usize k = static_cast<usize>(curve_.dragKey);
        const u32 c = curve_.dragChannel;
        const f64 mt = timeAt(io.MousePos.x), mv = valueAt(io.MousePos.y);
        if (!curve_.dragMoved && (std::fabs(io.MouseDelta.x) > 0.0f || std::fabs(io.MouseDelta.y) > 0.0f) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            pushUndo();
            curve_.dragMoved = true;
        }
        if (curve_.dragMoved) {
            if (curve_.dragKind == 1) {
                // Time stays between the neighbours so the key keeps its place in the order.
                const f64 lo = k > 0 ? tr.keys[k - 1].t + 1e-3 : 0.0;
                const f64 hi = k + 1 < tr.keys.size() ? tr.keys[k + 1].t - 1e-3 : len;
                const f64 step = game::seqStepSeconds(model_);
                f64 nt = std::clamp(mt, lo, std::fmax(lo, hi));
                if (!io.KeyShift) nt = std::clamp(std::round(nt / step) * step, lo, std::fmax(lo, hi));
                const usize at = seqMoveKey(tr, k, nt, c, mv, model_.length);
                curve_.dragKey = static_cast<int>(at);
                selKey_ = static_cast<int>(at);
                edited();
            } else {
                const OcSeqKey& key = tr.keys[k];
                f64 arrive = 0, leave = 0;
                game::seqEffectiveTangent(tr.keys, k, c, arrive, leave);
                const bool wasBroken = std::fabs(arrive - leave) > 1e-9;
                const bool broken = io.KeyAlt || wasBroken;
                if (curve_.dragKind == 2) {
                    const f64 dt = key.t - mt;
                    if (std::fabs(dt) > 1e-4) {
                        const f64 s = (key.v[c] - mv) / dt;
                        if (k > 0) seqSetKeyInterp(tr, k - 1, OcSeqInterp::Cubic);
                        seqSetTangent(tr, k, c, s, broken ? leave : s, broken);
                    }
                } else {
                    const f64 dt = mt - key.t;
                    if (std::fabs(dt) > 1e-4) {
                        const f64 s = (mv - key.v[c]) / dt;
                        seqSetKeyInterp(tr, k, OcSeqInterp::Cubic);
                        seqSetTangent(tr, k, c, broken ? arrive : s, s, broken);
                    }
                }
                edited();
            }
        }
    }
    dl->PopClipRect();
    ImGui::EndChild();
}

#else  // !AVER_WITH_IMGUI

void SequenceEditor::drawCurveView(SequenceHost&, f32, f32, f32, f32) {}

#endif

} // namespace aver::editor

#endif // AVER_MODULE_SCENE
