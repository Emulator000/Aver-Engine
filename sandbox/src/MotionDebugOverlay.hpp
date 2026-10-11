#pragma once
// Motion-matching viewport overlay (Show > Motion Matching): for every entity with a CMotionMatcher the
// measured history (grey), the predicted trajectory with a heading tick at each database time (green), the
// trajectory of the playing database row re-expressed from the entity's pose (cyan: predicted against matched
// shows at a glance), the desired velocity (yellow), and a label with the playing clip, its cost, the switch
// count and the groups that cost the most.
//
// Pure feed -> rhi::LineVertex, drawn like the AI overlay (see AiDebugOverlay.hpp): once per frame while an
// option is on, motionDebugCollect(opts, frame), upload frame.lines as a line mesh and draw it with an
// identity matrix, then motionDebugDrawLabels inside the viewport window.
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/rhi/RHI.hpp"

#if AVER_MODULE_SCENE
#  include "aver/anim/MotionMatchSystem.hpp"
#endif

#include <string>
#include <vector>

namespace aver::editor {

struct MotionDebugOptions {
    bool trajectory = false;   // history, predicted trajectory, desired velocity
    bool matched = false;      // the playing row's own trajectory
    bool labels = false;       // clip, time, cost, switches
    bool costs = false;        // adds the most expensive feature groups to the label
    f32 labelMaxDistanceCm = 4000.0f;
    bool any() const { return trajectory || matched || labels || costs; }
};

struct MotionDebugLabel {
    Vec3 world;
    std::string text;
    u32 rgba = 0xFFFFFFFFu;
};

struct MotionDebugFrame {
    std::vector<rhi::LineVertex> lines;   // two vertices per line, colours 0..1, world space
    std::vector<MotionDebugLabel> labels;
    void clear() { lines.clear(); labels.clear(); }
};

#if AVER_MODULE_SCENE
// Appends one entity's debug data. Exposed so a test can feed a hand-built Debug.
void motionDebugFeed(const anim::MotionMatchSystem::Debug& d, const MotionDebugOptions& opts, MotionDebugFrame& out);

// Clears `out`, then feeds every entity that has a CMotionMatcher in the process-global world.
void motionDebugCollect(const MotionDebugOptions& opts, MotionDebugFrame& out);
#endif

#if AVER_WITH_IMGUI
// Draws the frame's labels on the foreground draw list, skipping those beyond maxDistCm of `eye`.
void motionDebugDrawLabels(const MotionDebugFrame& frame, const Mat4& viewProj, f32 vpX, f32 vpY, f32 vpW, f32 vpH,
                           const Vec3& eye, f32 maxDistCm);
#endif

} // namespace aver::editor
