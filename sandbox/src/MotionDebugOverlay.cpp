// Motion-matching viewport overlay. See the header for the wiring.

#include "MotionDebugOverlay.hpp"

#if AVER_MODULE_SCENE
#include "AiDebugOverlay.hpp"   // aiDebugProject

#include "aver/scene/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#if AVER_WITH_IMGUI
#  include "imgui.h"
#endif

namespace aver::editor {

namespace {

void line(MotionDebugFrame& f, const Vec3& a, const Vec3& b, f32 r, f32 g, f32 bl) {
    f.lines.push_back(rhi::LineVertex{a.x, a.y, a.z, r, g, bl});
    f.lines.push_back(rhi::LineVertex{b.x, b.y, b.z, r, g, bl});
}

void polyline(MotionDebugFrame& f, const Vec3& from, const std::vector<Vec3>& pts, f32 r, f32 g, f32 b) {
    Vec3 prev = from;
    for (const Vec3& p : pts) {
        line(f, prev, p, r, g, b);
        prev = p;
    }
}

constexpr f32 kLift = 8.0f;   // cm above the ground so the lines are not buried in it

} // namespace

void motionDebugFeed(const anim::MotionMatchSystem::Debug& d, const MotionDebugOptions& opts, MotionDebugFrame& out) {
    const Vec3 lift{0.0f, 0.0f, kLift};
    if (opts.trajectory) {
        for (usize i = 1; i < d.history.size(); ++i) line(out, d.history[i - 1] + lift, d.history[i] + lift, 0.6f, 0.6f, 0.62f);
        std::vector<Vec3> pts;
        for (const Vec3& p : d.predicted) pts.push_back(p + lift);
        polyline(out, d.origin + lift, pts, 0.3f, 0.9f, 0.3f);
        for (usize i = 0; i < d.predicted.size() && i < d.predictedYaw.size(); ++i) {
            const Vec3 base = d.predicted[i] + lift;
            line(out, base, base + Vec3{std::cos(d.predictedYaw[i]), std::sin(d.predictedYaw[i]), 0.0f} * 25.0f, 0.3f, 0.9f, 0.3f);
        }
        line(out, d.origin + lift, d.origin + lift + d.desiredVel * 0.5f, 1.0f, 0.9f, 0.2f);   // half a second of the wanted velocity
    }
    if (opts.matched && !d.matched.empty()) {
        std::vector<Vec3> pts;
        for (const Vec3& p : d.matched) pts.push_back(p + lift);
        polyline(out, d.origin + lift, pts, 0.2f, 0.9f, 0.9f);
    }
    if (opts.labels || opts.costs) {
        MotionDebugLabel l;
        l.world = d.origin + Vec3{0.0f, 0.0f, 210.0f};
        l.rgba = 0xFFE6E6C8u;
        char buf[160];
        const std::string clip = d.clip.empty() ? std::string("(none)") : std::filesystem::path(d.clip).stem().string();
        std::snprintf(buf, sizeof buf, "%s @ %.2f   cost %.2f (was %.2f)   switches %u", clip.c_str(),
                      static_cast<double>(d.clipTime), static_cast<double>(d.cost), static_cast<double>(d.currentCost), d.switches);
        l.text = buf;
        if (opts.costs && !d.groupBest.empty()) {
            // The two groups that cost the most in the best hit: where the match is poorest.
            std::vector<usize> order(d.groupBest.size());
            for (usize i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](usize a, usize b) { return d.groupBest[a] > d.groupBest[b]; });
            for (usize k = 0; k < order.size() && k < 2; ++k) {
                std::snprintf(buf, sizeof buf, "\n%s  %.2f", d.groupNames[order[k]].c_str(), static_cast<double>(d.groupBest[order[k]]));
                l.text += buf;
            }
        }
        out.labels.push_back(std::move(l));
    }
}

void motionDebugCollect(const MotionDebugOptions& opts, MotionDebugFrame& out) {
    out.clear();
    if (!opts.any()) return;
    scene::World& w = scene::World::instance();
    const u32 type = w.componentId("CMotionMatcher");
    scene::ComponentPool* pool = type ? w.pool(type) : nullptr;
    if (!pool) return;
    for (usize i = 0; i < pool->size(); ++i) {
        const scene::Entity e = pool->entityAt(i);
        anim::MotionMatchSystem::Debug d;
        if (w.valid(e) && anim::motionMatchSystem().debug(e, d)) motionDebugFeed(d, opts, out);
    }
}

#if AVER_WITH_IMGUI
void motionDebugDrawLabels(const MotionDebugFrame& frame, const Mat4& viewProj, f32 vpX, f32 vpY, f32 vpW, f32 vpH,
                           const Vec3& eye, f32 maxDistCm) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (const MotionDebugLabel& l : frame.labels) {
        if (dist(l.world, eye) > maxDistCm) continue;
        f32 x, y;
        if (!aiDebugProject(viewProj, l.world, vpX, vpY, vpW, vpH, x, y)) continue;
        const ImU32 col = IM_COL32((l.rgba >> 16) & 0xFF, (l.rgba >> 8) & 0xFF, l.rgba & 0xFF, (l.rgba >> 24) & 0xFF);
        dl->AddText(ImVec2(x, y), col, l.text.c_str());
    }
}
#endif

} // namespace aver::editor

#endif  // AVER_MODULE_SCENE
