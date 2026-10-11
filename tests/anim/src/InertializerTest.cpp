// Inertialization: the offset at the switch, continuity of value and velocity, the half-life, the
// no-overshoot rule, shortest-arc rotation, settling, and the locked root. Pure pose arithmetic.
#include "MotionTestKit.hpp"

#include "aver/anim/Inertializer.hpp"
#include "aver/anim/Trajectory.hpp"

using namespace aver;
using mt::check;
using mt::checkNear;

namespace {

anim::Pose rest() {
    anim::Pose p;
    anim::restPose(mt::rig(), p);
    return p;
}

f32 quatDot(const Quat& a, const Quat& b) { return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w); }

// Output of the inertializer over a new-source pose `to`.
anim::Pose applied(const anim::Inertializer& in, const anim::Pose& to) {
    anim::Pose p = to;
    in.apply(p);
    return p;
}

} // namespace

int main() {
    AVER_INFO("InertializerTest");
    const f32 dt = 1.0f / 60.0f;

    AVER_INFO("the offset at the switch");
    {
        anim::Pose from = rest(), to = rest();
        from.local[1].position += Vec3{5, -2, 1};
        from.local[1].rotation = mt::rotZ(0.4f);
        to.local[1].rotation = mt::rotZ(0.1f);
        from.local[2].scale = Vec3{1.2f, 1.0f, 1.0f};
        anim::Inertializer in;
        in.begin(from, from, to, to, dt, 0xFFFFFFFFu);
        check(in.active(), "active after begin");
        checkNear(in.offset(1, 0), 5.0f, 1e-5f, "translation offset = from - to (x)");
        checkNear(in.offset(1, 1), -2.0f, 1e-5f, "(y)");
        checkNear(in.offset(1, 5), 0.3f, 1e-4f, "rotation offset is the rotation vector of from * to^-1 (0.4 - 0.1 about z)");
        checkNear(in.offset(2, 6), 0.2f, 1e-5f, "scale offset");
        const anim::Pose out = applied(in, to);
        check((out.local[1].position - from.local[1].position).size() < 1e-4f, "output equals `from` at t = 0 (position)");
        check(quatDot(out.local[1].rotation, from.local[1].rotation) > 0.99999f, "(rotation)");
        checkNear(out.local[2].scale.x, 1.2f, 1e-5f, "(scale)");
        check((out.local[3].position - to.local[3].position).size() < 1e-6f, "an untouched bone is untouched");
    }

    AVER_INFO("continuity of velocity");
    {
        // `from` was moving +60 cm/s along x when we cut; the new source is static.
        anim::Pose fromPrev = rest(), from = rest(), to = rest();
        fromPrev.local[1].position += Vec3{5, 0, 0};
        from.local[1].position += Vec3{5.0f + 60.0f * dt, 0, 0};
        anim::Inertializer in;
        in.begin(from, fromPrev, to, to, dt, 0xFFFFFFFFu);
        const f32 x0 = applied(in, to).local[1].position.x - to.local[1].position.x;
        in.advance(1e-4f);
        const f32 x1 = applied(in, to).local[1].position.x - to.local[1].position.x;
        checkNear((x1 - x0) / 1e-4f, 60.0f, 1.5f, "the output keeps moving at the old source's velocity");
        checkNear(in.velocity(1, 0), 60.0f, 0.5f, "stored initial velocity");

        // and the new source's own velocity is subtracted: both moving 60 cm/s = no relative velocity
        anim::Pose toPrev = to;
        toPrev.local[1].position -= Vec3{60.0f * dt, 0, 0};
        anim::Inertializer both;
        both.begin(from, fromPrev, to, toPrev, dt, 0xFFFFFFFFu);
        checkNear(both.velocity(1, 0), 0.0f, 0.5f, "velocity of the offset = velocity(from) - velocity(to)");
    }

    AVER_INFO("half-life, settling");
    {
        anim::Pose from = rest(), to = rest();
        from.local[1].position += Vec3{8, 0, 0};
        for (const f32 hl : {0.05f, 0.1f, 0.3f}) {
            anim::Inertializer in;
            in.setHalfLife(hl);
            in.begin(from, from, to, to, dt, 0xFFFFFFFFu);
            const int steps = 100;
            for (int i = 0; i < steps; ++i) in.advance(hl / steps);
            checkNear(in.offset(1, 0), 4.0f, 2e-3f, "with no initial velocity the offset halves at exactly the half-life (" + std::to_string(hl) + ")");
        }
        anim::Inertializer in;
        in.begin(from, from, to, to, dt, 0xFFFFFFFFu);
        int n = 0;
        while (in.active() && n < 1000) { in.advance(dt); ++n; }
        check(!in.active() && n < 200, "settles and goes inactive (" + std::to_string(n) + " ticks)");
        const anim::Pose out = applied(in, to);
        check((out.local[1].position - to.local[1].position).size() == 0.0f, "an inactive inertializer applies as the identity");
        in.begin(from, from, to, to, dt, 0xFFFFFFFFu);
        in.reset();
        check(!in.active() && in.offset(1, 0) == 0.0f, "reset");
        in.advance(dt);
        check(!in.active(), "advance on an inactive inertializer is harmless");
    }

    AVER_INFO("no overshoot");
    {
        // Offset +5 with a velocity hurrying it through zero: the spring would cross, so it is capped.
        anim::Pose fromPrev = rest(), from = rest(), to = rest();
        from.local[1].position += Vec3{5, 0, 0};
        fromPrev.local[1].position += Vec3{5.0f + 200.0f * dt, 0, 0};   // moving toward `to` at -200 cm/s
        anim::Inertializer in;
        in.begin(from, fromPrev, to, to, dt, 0xFFFFFFFFu);
        bool nonNeg = true, decreasing = true;
        f32 prev = in.offset(1, 0);
        for (int i = 0; i < 300; ++i) {
            in.advance(0.005f);
            const f32 x = in.offset(1, 0);
            nonNeg &= x >= -1e-5f;
            decreasing &= x <= prev + 1e-5f;
            prev = x;
        }
        check(nonNeg && decreasing, "the offset decays monotonically and never changes sign");

        // The mirror case: moving away from zero keeps its velocity (no cap needed).
        anim::Pose fromPrev2 = rest();
        fromPrev2.local[1].position += Vec3{5.0f - 200.0f * dt, 0, 0};
        anim::Inertializer away;
        away.begin(from, fromPrev2, to, to, dt, 0xFFFFFFFFu);
        away.advance(0.02f);
        check(away.offset(1, 0) > 5.0f, "an offset moving away grows first, then decays");
    }

    AVER_INFO("rotation");
    {
        anim::Pose from = rest(), to = rest();
        const Quat q = mt::rotZ(0.7f);
        from.local[1].rotation = q;
        to.local[1].rotation = Quat{-q.x, -q.y, -q.z, -q.w};   // the same rotation, the other sign
        anim::Inertializer same;
        same.begin(from, from, to, to, dt, 0xFFFFFFFFu);
        check(std::fabs(same.offset(1, 3)) + std::fabs(same.offset(1, 4)) + std::fabs(same.offset(1, 5)) < 1e-4f,
              "q and -q are the same rotation: no offset");

        anim::Pose a = rest(), b = rest();
        a.local[1].rotation = mt::rotZ(3.0f);
        b.local[1].rotation = mt::rotZ(-3.0f);
        anim::Inertializer arc;
        arc.begin(a, a, b, b, dt, 0xFFFFFFFFu);
        checkNear(std::fabs(arc.offset(1, 5)), kTwoPi - 6.0f, 1e-3f, "across +-pi the offset takes the short arc (0.283 rad, not 6)");
        const anim::Pose out = applied(arc, b);
        check(quatDot(out.local[1].rotation, a.local[1].rotation) > 0.99999f, "and still reproduces `from`");

        // The rotation offset decays toward the new source's rotation.
        for (int i = 0; i < 300; ++i) arc.advance(dt);
        const anim::Pose settled = applied(arc, b);
        check(quatDot(settled.local[1].rotation, b.local[1].rotation) > 0.99999f, "and ends on the new source");
    }

    AVER_INFO("the locked root, mismatches");
    {
        anim::Pose from = rest(), to = rest();
        from.local[0].position += Vec3{50, 0, 0};
        from.local[1].position += Vec3{1, 0, 0};
        anim::Inertializer in;
        in.begin(from, from, to, to, dt, 0);
        check(in.offset(0, 0) == 0.0f && in.offset(1, 0) == 1.0f, "skipBone gets no offset, the others do");
        const anim::Pose out = applied(in, to);
        check((out.local[0].position - to.local[0].position).size() == 0.0f, "and apply leaves it alone");

        anim::Pose shorter;
        shorter.local.resize(2);
        anim::Inertializer bad;
        bad.begin(from, from, shorter, shorter, dt, 0xFFFFFFFFu);
        check(!bad.active(), "mismatched bone counts start nothing");
        anim::Pose small = rest();
        small.local.pop_back();
        in.apply(small);   // wrong size for the running offsets: no write, no crash
        check(small.local.size() == 3, "apply on a pose of another size is a no-op");
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
