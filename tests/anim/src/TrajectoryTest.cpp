// Trajectory prediction: the half-life constant, the closed forms against numeric integration, heading by the
// shortest arc, the heading-frame output and the history ring. Pure arithmetic, no scene.
#include "MotionTestKit.hpp"

#include "aver/anim/Trajectory.hpp"

using namespace aver;
using mt::check;
using mt::checkNear;

namespace {

f32 wrapPi(f32 a) {
    while (a > kPi) a -= kTwoPi;
    while (a <= -kPi) a += kTwoPi;
    return a;
}

// Walks an entity along a straight line at `speed` for `seconds`, one observation per `dt`.
void walk(anim::TrajectoryPredictor& tp, Vec3 start, f32 yaw, Vec3 dir, f32 speed, f32 seconds, f32 dt, f32 hist) {
    const int n = static_cast<int>(seconds / dt + 0.5f);
    for (int i = 0; i <= n; ++i) tp.observe(start + dir * (speed * dt * static_cast<f32>(i)), yaw, i ? dt : 0.0f, hist);
}

} // namespace

int main() {
    AVER_INFO("TrajectoryTest");
    const std::vector<f32> kFuture{0.2f, 0.4f, 0.7f, 1.0f}, kPast{-0.2f, -0.5f};
    const anim::SpringParams sp;

    AVER_INFO("spring constants and closed forms");
    {
        const f32 z = anim::kSpringHalfLifeK;
        checkNear((1.0f + z) * std::exp(-z), 0.5f, 1e-4f, "(1 + z) e^-z = 0.5 at the constant");
        for (const f32 hl : {0.1f, 0.2f, 0.5f})
            checkNear(anim::springVelocity(0.0f, 1.0f, hl, hl), 0.5f, 1e-4f, "from rest, half way to the target at exactly the half-life");
        checkNear(anim::springVelocity(30.0f, 90.0f, 0.2f, 0.0f), 30.0f, 1e-4f, "starts at v0");
        {
            // A forward difference carries h v''(0) / 2 (about 2 here); Richardson cancels it, leaving v'(0).
            auto fd = [](f32 h) { return (anim::springVelocity(30.0f, 90.0f, 0.2f, h) - 30.0f) / h; };
            checkNear(2.0f * fd(5e-4f) - fd(1e-3f), 0.0f, 0.5f, "with zero initial acceleration");
        }
        checkNear(anim::springVelocity(30.0f, 90.0f, 0.2f, 5.0f), 90.0f, 1e-3f, "settles on the target");

        // displacement = integral of velocity (Simpson, fine enough to be exact for this smooth function)
        for (const f32 T : {0.3f, 0.7f, 1.0f}) {
            const int n = 2000;
            double acc = 0;
            for (int i = 0; i < n; ++i) {
                const f32 a = T * static_cast<f32>(i) / n, b = T * static_cast<f32>(i + 1) / n, m = 0.5f * (a + b);
                acc += (b - a) / 6.0 * (anim::springVelocity(300.0f, 100.0f, 0.2f, a) + 4.0 * anim::springVelocity(300.0f, 100.0f, 0.2f, m) +
                                        anim::springVelocity(300.0f, 100.0f, 0.2f, b));
            }
            checkNear(anim::springDisplacement(300.0f, 100.0f, 0.2f, T), static_cast<f32>(acc), 4e-3f,
                      "displacement equals the integral of the velocity at " + std::to_string(T));
        }
        bool monotone = true, below = true, above = true;
        f32 prev = 0.0f;
        for (int i = 1; i <= 400; ++i) {
            const f32 t = 0.005f * static_cast<f32>(i);
            const f32 up = anim::springVelocity(0.0f, 100.0f, 0.2f, t), down = anim::springVelocity(300.0f, 100.0f, 0.2f, t);
            monotone &= up >= prev - 1e-4f;
            prev = up;
            below &= up <= 100.0f + 1e-3f;
            above &= down >= 100.0f - 1e-3f;
        }
        check(monotone && below && above, "velocity never overshoots the target from either side");
    }

    AVER_INFO("heading");
    {
        // 3.0 -> -3.0: the short way is +0.283 rad through pi.
        const f32 early = anim::springAngle(3.0f, 0.0f, -3.0f, 0.15f, 0.05f);
        check(wrapPi(early) > 3.0f, "turns through +-pi, not the long way round");
        checkNear(wrapPi(anim::springAngle(3.0f, 0.0f, -3.0f, 0.15f, 3.0f) + 3.0f), 0.0f, 1e-3f, "and lands on the target");
        checkNear(anim::springAngle(0.4f, 0.0f, 0.4f, 0.15f, 0.7f), 0.4f, 1e-5f, "already on target stays");
        checkNear((anim::springAngle(0.0f, 2.0f, 0.0f, 0.15f, 1e-3f) - 0.0f) / 1e-3f, 2.0f, 0.05f, "honours the measured angular velocity");
        checkNear(anim::springAngle(1.0f, 0.0f, 0.0f, 0.15f, 0.0f), 1.0f, 1e-5f, "starts at the current heading");
    }

    AVER_INFO("a character standing still");
    {
        anim::TrajectoryPredictor tp;
        anim::MotionQuery q;
        tp.fill(sp, kFuture, kPast, q);
        check(!q.valid, "nothing observed: not valid");
        for (int i = 0; i < 20; ++i) tp.observe(Vec3{100, 50, 0}, 0.3f, 0.016f, 1.0f);
        tp.setIntent(0, 0, false, 0, 5.0f);
        tp.fill(sp, kFuture, kPast, q);
        bool zero = q.valid;
        for (usize i = 0; i < 4; ++i)
            zero &= std::fabs(q.futurePos[i][0]) < 1e-3f && std::fabs(q.futurePos[i][1]) < 1e-3f &&
                    std::fabs(q.futureDir[i][0] - 1.0f) < 1e-4f && std::fabs(q.futureDir[i][1]) < 1e-4f;
        for (usize i = 0; i < 2; ++i) zero &= std::fabs(q.pastPos[i][0]) < 1e-3f && std::fabs(q.pastPos[i][1]) < 1e-3f;
        zero &= std::fabs(q.rootVel[0]) < 1e-3f && std::fabs(q.rootVel[1]) < 1e-3f && std::fabs(q.rootVel[2]) < 1e-3f;
        check(zero, "zero intent and zero motion give an all-zero trajectory");
    }

    AVER_INFO("heading frame, history ring");
    {
        // A character facing +Y (yaw 90 deg) running along +Y at 200 cm/s.
        anim::TrajectoryPredictor tp;
        walk(tp, {0, 0, 0}, kPi * 0.5f, {0, 1, 0}, 200.0f, 1.5f, 1.0f / 60.0f, 1.0f);
        tp.setIntent(0, 200.0f, false, 0, 5.0f);
        anim::MotionQuery q;
        tp.fill(sp, kFuture, kPast, q);
        checkNear(q.rootVel[0], 200.0f, 0.5f, "measured speed is along the heading (+x)");
        checkNear(q.rootVel[1], 0.0f, 0.5f, "none sideways");
        checkNear(q.futurePos[0][0], 40.0f, 0.3f, "future position at 0.2 s rotates into the heading frame");
        checkNear(q.futurePos[0][1], 0.0f, 0.3f, "and not across");
        checkNear(q.futurePos[3][0], 200.0f, 1.0f, "at 1.0 s");
        checkNear(q.futureDir[3][0], 1.0f, 1e-3f, "no turning when already facing the velocity");
        checkNear(q.pastPos[0][0], -40.0f, 0.5f, "history interpolated at -0.2 s");
        checkNear(q.pastPos[1][0], -100.0f, 0.8f, "and at -0.5 s");

        // the ring is 1 s long: -1.5 s holds the oldest value
        const std::vector<f32> far{-0.5f, -1.5f};
        tp.fill(sp, kFuture, far, q);
        check(q.pastPos[1][0] > -215.0f && q.pastPos[1][0] < -195.0f, "before the oldest entry the oldest value holds (about -1 s of travel)");

        std::vector<Vec3> pts, hist;
        std::vector<f32> yaws;
        tp.predictedWorld(sp, kFuture, pts, yaws);
        check(pts.size() == 4 && yaws.size() == 4, "predictedWorld: one point per time");
        checkNear(pts[0].y - tp.position().y, 40.0f, 0.3f, "predicted world points continue along +Y");
        checkNear(pts[0].x - tp.position().x, 0.0f, 0.3f, "and not along X");
        tp.historyWorld(hist);
        check(hist.size() > 10 && hist.size() < 100, "the ring is trimmed to the history length");
        checkNear(tp.velocity().y, 200.0f, 0.5f, "velocity accessor");
    }

    AVER_INFO("facing");
    {
        anim::MotionQuery q;
        anim::TrajectoryPredictor slow;
        for (int i = 0; i < 20; ++i) slow.observe(Vec3{0, 0, 0}, 0.8f, 0.016f, 1.0f);
        slow.setIntent(2.0f, 0.0f, false, 0.0f, 5.0f);
        slow.fill(sp, kFuture, kPast, q);
        checkNear(q.futureDir[3][0], 1.0f, 1e-3f, "below faceSpeedMin the heading is held (no turn toward the velocity)");

        anim::TrajectoryPredictor fast;
        for (int i = 0; i < 20; ++i) fast.observe(Vec3{0, 0, 0}, 0.0f, 0.016f, 1.0f);
        fast.setIntent(0.0f, 100.0f, false, 0.0f, 5.0f);
        fast.fill(sp, kFuture, kPast, q);
        checkNear(q.futureDir[3][0], 0.0f, 0.02f, "above it the character turns toward its velocity (cos)");
        checkNear(q.futureDir[3][1], 1.0f, 0.02f, "(sin: a quarter turn left)");
        check(q.futureDir[0][1] > 0.0f && q.futureDir[0][1] < q.futureDir[1][1], "and the turn builds up over the horizon");

        anim::TrajectoryPredictor yawMode;
        for (int i = 0; i < 20; ++i) yawMode.observe(Vec3{0, 0, 0}, 0.0f, 0.016f, 1.0f);
        yawMode.setIntent(100.0f, 0.0f, true, -1.0f, 5.0f);
        yawMode.fill(sp, kFuture, kPast, q);
        checkNear(std::atan2(q.futureDir[3][1], q.futureDir[3][0]), -1.0f, 0.02f, "yaw mode follows the target, not the velocity");

        anim::TrajectoryPredictor spin;   // measured angular velocity carries into the prediction
        f32 yaw = 0.0f;
        for (int i = 0; i < 60; ++i) { spin.observe(Vec3{0, 0, 0}, yaw, 0.016f, 1.0f); yaw += 0.016f * 2.0f; }
        spin.setIntent(0, 0, false, 0, 5.0f);
        spin.fill(sp, kFuture, kPast, q);
        checkNear(spin.yawRate(), 2.0f, 0.05f, "yaw rate is measured from unwrapped differences");
        check(q.rootVel[2] > 1.9f && q.rootVel[2] < 2.1f, "and reported in the query");

        anim::TrajectoryPredictor wrap;   // crossing +-pi is not a spin
        f32 a = 3.1f;
        for (int i = 0; i < 30; ++i) { wrap.observe(Vec3{0, 0, 0}, a, 0.016f, 1.0f); a = wrapPi(a + 0.016f * 1.0f); }
        checkNear(wrap.yawRate(), 1.0f, 0.05f, "a heading that wraps through pi keeps a sane rate");
        wrap.reset();
        check(!wrap.observed(), "reset forgets the history");
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
