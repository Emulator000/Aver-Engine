// AI debug viewport overlays. See the header for the wiring.

#include "AiDebugOverlay.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#if AVER_MODULE_SYNAPSE_SCENE && AVER_MODULE_SCENE
#  include "aver/scene/World.hpp"
#  include "aver/synapse/SynapseAgent.hpp"
#  include "aver/synapse/SynapseBt.hpp"
#  include "aver/synapse/SynapseCrowd.hpp"
#  include "aver/synapse/SynapseHearing.hpp"
#  include "aver/synapse/SynapsePerception.hpp"
#  define AVER_AI_DEBUG_SCENE 1
#  if AVER_WITH_SYNAPSE_AI
#    include "aver/synapse/SynapseAi.hpp"          // AI: wave 3 #21 -- queries and smart objects live in the DLL's instance
#    include "aver/synapse/synapse_ai_abi.h"
#    define AVER_AI_DEBUG_DLL 1
#  endif
#endif

#if AVER_WITH_IMGUI
#  include "imgui.h"
#endif

namespace aver::editor {

u32 AiDebugOptions::mask() const {
    u32 m = 0;
    if (sight)    m |= synapse::kAiDebugSight;
    if (hearing)  m |= synapse::kAiDebugHearing;
    if (path)     m |= synapse::kAiDebugPath;
    if (steering) m |= synapse::kAiDebugSteering;
    if (btState)  m |= synapse::kAiDebugBtState;
    if (cover)    m |= synapse::kAiDebugCover;
    if (eqs)          m |= synapse::kAiDebugEqs;
    if (smartObjects) m |= synapse::kAiDebugSmartObjects;
    if (stateTree)    m |= synapse::kAiDebugStateTree;
    return m;
}

std::vector<rhi::LineVertex> aiDebugLineVertices(const synapse::AiDebugSink& sink) {
    std::vector<rhi::LineVertex> out;
    out.reserve(sink.lines().size() * 2);
    for (const synapse::AiDebugLine& l : sink.lines()) {
        const f32 r = static_cast<f32>((l.rgba >> 24) & 0xFFu) / 255.0f;
        const f32 g = static_cast<f32>((l.rgba >> 16) & 0xFFu) / 255.0f;
        const f32 b = static_cast<f32>((l.rgba >> 8) & 0xFFu) / 255.0f;
        out.push_back(rhi::LineVertex{l.a.x, l.a.y, l.a.z, r, g, b});
        out.push_back(rhi::LineVertex{l.b.x, l.b.y, l.b.z, r, g, b});
    }
    return out;
}

bool aiDebugProject(const Mat4& m, const Vec3& p, f32 vpX, f32 vpY, f32 vpW, f32 vpH, f32& outX, f32& outY) {
    const f32 x = p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0];
    const f32 y = p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1];
    const f32 w = p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3];
    if (w <= 1e-4f) return false;
    outX = vpX + (x / w * 0.5f + 0.5f) * vpW;
    outY = vpY + (0.5f - y / w * 0.5f) * vpH;
    return true;
}

#if AVER_AI_DEBUG_SCENE
namespace {

using namespace synapse;

constexpr f32 kPi = 3.14159265358979323846f;

Vec3 worldPos(scene::World& w, scene::Entity e) {
    const Mat4& m = w.worldMatrix(e);
    return Vec3{m.m[3][0], m.m[3][1], m.m[3][2]};
}

// Row 0 of the world matrix is the entity's +X, the convention SynapsePerception uses for "forward".
Vec3 worldForward(scene::World& w, scene::Entity e) {
    const Mat4& m = w.worldMatrix(e);
    const Vec3 f{m.m[0][0], m.m[0][1], m.m[0][2]};
    const f32 len = f.size();
    return len > 1e-6f ? f / len : Vec3{1.0f, 0.0f, 0.0f};
}

void feedSight(AiDebugSink& sink, scene::World& w) {
    const u32 type = perceptionSystem().componentType();
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* p = static_cast<const CSynapsePerception*>(pool->dataAt(i));
        if (!p || !w.valid(e)) continue;
        const Vec3 eye = worldPos(w, e) + Vec3{0.0f, 0.0f, p->eyeHeightCm};
        const bool sees = p->canSeeTarget != 0;
        const u32 col = sees ? aiRgba(80, 220, 90) : aiRgba(120, 160, 255, 200);
        sink.cone(eye, worldForward(w, e), p->sightRangeCm, p->sightHalfAngleDeg * (kPi / 180.0f), col, kAiDebugSight);
        if (sees && p->lastKnownTargetEntity != 0) {
            const scene::Entity t = static_cast<scene::Entity>(p->lastKnownTargetEntity);
            if (w.valid(t)) sink.arrow(eye, worldPos(w, t) + Vec3{0.0f, 0.0f, 100.0f}, aiRgba(80, 220, 90), kAiDebugSight);
        }
    }
}

// The agent's current waypoint and goal. The full baked path is private to AgentSystem.
void feedPath(AiDebugSink& sink, scene::World& w) {
    const u32 type = agentSystem().componentType();
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* a = static_cast<const CSynapseAgent*>(pool->dataAt(i));
        if (!a || !w.valid(e)) continue;
        const Vec3 foot = worldPos(w, e) + Vec3{0.0f, 0.0f, 20.0f};
        const AgentStatus st = static_cast<AgentStatus>(a->status);
        if (st == AgentStatus::Pathing) {
            const Vec3 wp{a->targetXCm, a->targetYCm, a->targetZCm + 20.0f};
            sink.arrow(foot, wp, aiRgba(255, 200, 60), kAiDebugPath);
            const Vec3 goal{a->goalXCm, a->goalYCm, a->goalZCm + 20.0f};
            sink.line(wp, goal, aiRgba(255, 200, 60, 90), kAiDebugPath);
            sink.circle(goal, a->arriveRadiusCm, aiRgba(255, 120, 60), kAiDebugPath, 20);
        } else if (st == AgentStatus::Failed) {
            sink.circle(foot, a->radiusCm * 1.5f, aiRgba(255, 60, 60), kAiDebugPath, 12);
        }
    }
}

// Hearing and crowd components are found by registered name, so this file needs neither system's
// singleton and shows nothing when they are not registered.
void feedHearing(AiDebugSink& sink, scene::World& w) {
    const u32 type = w.componentId("CSynapseHearing");
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* h = static_cast<const CSynapseHearing*>(pool->dataAt(i));
        if (!h || !w.valid(e)) continue;
        const Vec3 ear = worldPos(w, e) + Vec3{0.0f, 0.0f, h->earHeightCm};
        sink.circle(ear, h->maxRangeCm, aiRgba(255, 170, 60, 110), kAiDebugHearing, 40);
        if (h->hasMemory != 0) {
            const Vec3 heard{h->heardXCm, h->heardYCm, h->heardZCm};
            const u32 col = aiRgba(255, 170, 60, static_cast<u32>(90.0f + 165.0f * std::clamp(h->confidence, 0.0f, 1.0f)));
            sink.line(ear, heard, col, kAiDebugHearing);
            sink.sphere(heard, 25.0f + 50.0f * std::clamp(h->heardLevel, 0.0f, 1.0f), col, kAiDebugHearing, 12);
        }
    }
}

// Crowd steering: the avoidance-corrected velocity and the preferred one it started from.
void feedSteering(AiDebugSink& sink, scene::World& w) {
    const u32 type = w.componentId("CSynapseCrowd");
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    constexpr f32 kSeconds = 0.6f;   // an arrow is where the agent would be this far ahead
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* c = static_cast<const CSynapseCrowd*>(pool->dataAt(i));
        if (!c || c->active == 0 || !w.valid(e)) continue;
        const Vec3 foot = worldPos(w, e) + Vec3{0.0f, 0.0f, 30.0f};
        sink.arrow(foot, foot + Vec3{c->desiredXCm, c->desiredYCm, 0.0f} * kSeconds, aiRgba(255, 230, 90, 170), kAiDebugSteering, 14.0f);
        sink.arrow(foot, foot + Vec3{c->velXCm, c->velYCm, 0.0f} * kSeconds, aiRgba(70, 210, 255), kAiDebugSteering, 14.0f);
        if (c->stuck != 0) sink.circle(foot, c->radiusCm * 1.3f, aiRgba(255, 70, 70), kAiDebugSteering, 14);
    }
}

const char* statusName(i32 s) {
    switch (static_cast<BtStatus>(s)) {
        case BtStatus::Running: return "Running";
        case BtStatus::Success: return "Success";
        case BtStatus::Failure: return "Failure";
    }
    return "?";
}

void feedBtState(AiDebugSink& sink, scene::World& w) {
    BtSystem& sys = btSystem();
    const u32 type = sys.componentType();
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* b = static_cast<const CSynapseBehavior*>(pool->dataAt(i));
        if (!b || b->treeAssetId == 0 || !w.valid(e)) continue;
        BtDebugView v;
        if (!sys.debugView(e, v)) continue;

        std::string text = std::filesystem::path(v.tree->path).filename().string();
        text += " | ";
        text += statusName(b->lastStatus);

        if (v.state) {
            std::vector<i32> running;
            for (const auto& kv : v.state->actionElapsed) running.push_back(kv.first);
            std::sort(running.begin(), running.end());
            if (!running.empty()) {
                text += "\n> ";
                for (usize k = 0; k < running.size() && k < 3; ++k) {
                    const i32 n = running[k];
                    if (n >= 0 && static_cast<usize>(n) < v.tree->tree.nodes.size())
                        text += (k ? ", " : "") + v.tree->tree.nodes[static_cast<usize>(n)].name;
                }
            }
        }
        if (v.board && v.board->size() > 0) {
            text += "\n";
            for (i32 k = 0; k < static_cast<i32>(v.board->size()) && k < 4; ++k) {
                if (k) text += "  ";
                text += v.board->keyDef(k).name + "=" + bbToString(v.board->valueAt(k));
            }
        }
        sink.text(worldPos(w, e) + Vec3{0.0f, 0.0f, 230.0f}, std::move(text), aiRgba(255, 255, 255), kAiDebugBtState);
    }
}

// ---- wave 3 #21: state trees, smart objects, environment queries ---------------------------------------

void feedStateTree(AiDebugSink& sink, scene::World& w) {
    BtSystem& sys = btSystem();
    const u32 type = sys.componentType();
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        const auto* b = static_cast<const CSynapseBehavior*>(pool->dataAt(i));
        if (!b || b->treeAssetId == 0 || !w.valid(e)) continue;
        StateTreeDebugView v;
        if (!sys.stateTreeDebugView(e, v) || !v.tree || !v.state) continue;
        const StateTreeAsset& a = v.tree->asset;
        std::string text = std::filesystem::path(v.tree->path).filename().string() + " | ";
        text += v.state->active.empty() ? std::string("(no state)") : a.statePath(v.state->active.back());
        if (v.state->status == StStatus::Succeeded) text += " | done";
        else if (v.state->status == StStatus::Failed) text += " | failed";
        else if (v.state->status == StStatus::Stopped) text += " | stopped";
        // Task statuses of the active chain, when the editor is recording this entity.
        for (const i32 s : v.state->active) {
            if (s < 0 || static_cast<usize>(s) >= v.state->traceTasks.size()) continue;
            const auto& tt = v.state->traceTasks[static_cast<usize>(s)];
            const StState& st = a.states[static_cast<usize>(s)];
            for (usize k = 0; k < tt.size() && k < st.tasks.size(); ++k)
                text += std::string("\n") + (tt[k] == 0 ? "~ " : tt[k] == 1 ? "+ " : tt[k] == 2 ? "x " : "- ") + st.tasks[k].name;
        }
        // The last transition, for about two seconds.
        if (v.state->tickCount - v.state->lastTransitionTick < 120 && v.state->lastTransitionTick != 0) {
            const std::string from = v.state->lastFrom >= 0 ? a.statePath(v.state->lastFrom) : std::string("(start)");
            const std::string to = v.state->lastTo >= 0 ? a.statePath(v.state->lastTo) : std::string("(end)");
            text += "\n" + from + " -> " + to;
        }
        sink.text(worldPos(w, e) + Vec3{0.0f, 0.0f, 270.0f}, std::move(text), aiRgba(150, 205, 255), kAiDebugStateTree);
    }
}

#if AVER_AI_DEBUG_DLL
SynapseAi& dllAi() { return *static_cast<SynapseAi*>(aver_syn_ai_instance()); }

void feedSmartObjects(AiDebugSink& sink, scene::World& w) {
    const SmartObjectRegistry& reg = dllAi().smartObjects().registry();
    for (const u32 obj : reg.objects()) {
        const u32 n = reg.slotCount(obj);
        for (u32 s = 0; s < n; ++s) {
            SoSlotInfo info;
            if (!reg.slotInfo(obj, s, info)) continue;
            u32 col = aiRgba(90, 220, 110);                       // Free
            switch (info.state) {
                case SoSlotState::Claimed: col = aiRgba(250, 215, 70); break;
                case SoSlotState::Occupied: col = aiRgba(235, 80, 80); break;
                case SoSlotState::Cooldown:
                case SoSlotState::Disabled: col = aiRgba(140, 140, 150); break;
                case SoSlotState::Free: break;
            }
            const f32 yaw = info.world.yawDeg * (kPi / 180.0f);
            const Vec3 tip = info.world.pos + Vec3{std::cos(yaw), std::sin(yaw), 0.0f} * 45.0f;
            sink.arrow(info.world.pos, tip, col, kAiDebugSmartObjects, 14.0f);
            sink.line(info.world.pos, info.world.entry, aiRgba(col >> 24, (col >> 16) & 0xFF, (col >> 8) & 0xFF, 110), kAiDebugSmartObjects);
            sink.circle(info.world.entry, 20.0f, col, kAiDebugSmartObjects, 12);
            std::string label = std::to_string(obj) + "." + std::to_string(s);
            if (info.def) label += " " + info.def->name;
            if (info.user != 0 && w.valid(static_cast<scene::Entity>(info.user))) label += std::string(" [") + w.name(static_cast<scene::Entity>(info.user)) + "]";
            sink.text(info.world.pos + Vec3{0.0f, 0.0f, 30.0f}, std::move(label), col, kAiDebugSmartObjects);
        }
    }
}

void feedEqs(AiDebugSink& sink, scene::World& w) {
    for (const EqsDebugRun& run : dllAi().eqs().debugRuns()) {
        if (!run.pinned && run.age > 5.0f) continue;
        Vec3 from = run.origin;
        if (run.querier != 0 && w.valid(static_cast<scene::Entity>(run.querier))) from = worldPos(w, static_cast<scene::Entity>(run.querier));
        if (run.failed) {
            sink.circle(from, 60.0f, aiRgba(255, 70, 70), kAiDebugEqs, 16);
            sink.text(from + Vec3{0.0f, 0.0f, 120.0f}, "EQS failed: " + run.reason, aiRgba(255, 120, 120), kAiDebugEqs);
            continue;
        }
        if (run.items.empty()) continue;
        f32 lo = run.items.front().score, hi = lo;
        for (const EqsItem& it : run.items) { lo = std::min(lo, it.score); hi = std::max(hi, it.score); }
        for (usize i = 0; i < run.items.size(); ++i) {
            const EqsItem& it = run.items[i];
            const f32 t = hi > lo ? (it.score - lo) / (hi - lo) : 1.0f;   // 1 = best
            const u32 col = aiRgba(static_cast<u32>(255.0f * (1.0f - t)), static_cast<u32>(255.0f * t), 40);
            sink.sphere(it.pos, i == 0 ? 28.0f : 12.0f, col, kAiDebugEqs, 10);
        }
        const EqsItem& best = run.items.front();
        sink.line(from, best.pos, aiRgba(80, 230, 110), kAiDebugEqs);
        char head[64];
        std::snprintf(head, sizeof head, "%s  score %.2f", run.query.c_str(), static_cast<double>(best.score));
        std::string text = head;
        for (usize k = 0; k < run.bestValues.size() && k < run.testNames.size(); ++k) {
            char row[64];
            std::snprintf(row, sizeof row, "\n%s=%.2f", run.testNames[k].c_str(), static_cast<double>(run.bestValues[k]));
            text += row;
        }
        sink.text(best.pos + Vec3{0.0f, 0.0f, 70.0f}, std::move(text), aiRgba(190, 255, 200), kAiDebugEqs);
    }
}
#endif  // AVER_AI_DEBUG_DLL

} // namespace
#endif  // AVER_AI_DEBUG_SCENE

void aiDebugCollect(const AiDebugOptions& options, synapse::AiDebugSink& sink) {
    sink.clear();
    sink.setEnabled(options.mask());
    if (!options.any()) return;
#if AVER_AI_DEBUG_SCENE
    scene::World& w = scene::World::instance();
    if (options.sight) feedSight(sink, w);
    if (options.path) feedPath(sink, w);
    if (options.hearing) feedHearing(sink, w);
    if (options.steering) feedSteering(sink, w);
    if (options.btState) feedBtState(sink, w);
    if (options.stateTree) feedStateTree(sink, w);   // AI: wave 3 #21
#if AVER_AI_DEBUG_DLL
    if (options.smartObjects) feedSmartObjects(sink, w);
    if (options.eqs) feedEqs(sink, w);
#endif
#endif
    sink.runProviders();
}

#if AVER_WITH_IMGUI
void aiDebugDrawLabels(const synapse::AiDebugSink& sink, const Mat4& viewProj, f32 vpX, f32 vpY, f32 vpW,
                       f32 vpH, const Vec3& eye, f32 maxDistCm) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const f32 maxSq = maxDistCm * maxDistCm;
    for (const synapse::AiDebugText& t : sink.texts()) {
        if (distSquared(t.pos, eye) > maxSq) continue;
        f32 sx, sy;
        if (!aiDebugProject(viewProj, t.pos, vpX, vpY, vpW, vpH, sx, sy)) continue;
        if (sx < vpX || sx > vpX + vpW || sy < vpY || sy > vpY + vpH) continue;
        const ImVec2 size = ImGui::CalcTextSize(t.text.c_str());
        const ImVec2 p0(sx - size.x * 0.5f - 4.0f, sy - size.y - 6.0f);
        const ImVec2 p1(sx + size.x * 0.5f + 4.0f, sy - 2.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(10, 12, 18, 170), 3.0f);
        dl->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f),
                    IM_COL32((t.rgba >> 24) & 0xFF, (t.rgba >> 16) & 0xFF, (t.rgba >> 8) & 0xFF, t.rgba & 0xFF),
                    t.text.c_str());
    }
}
#endif

} // namespace aver::editor
