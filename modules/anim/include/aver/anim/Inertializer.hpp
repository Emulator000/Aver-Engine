// Inertialization: switch to a new pose source without blending two evaluated sources. At the switch
// the difference between the old output and the new source (and the difference of their velocities)
// is stored per bone, and then decays with a critically damped spring while the new source plays on
// its own. One pose is evaluated per frame, the output is continuous in value and velocity.
//
// Per bone and component the state is (x, v): translation offset (3), rotation offset as a rotation
// vector (3), scale offset (3). Pure arithmetic over Pose.
#pragma once

#include "aver/anim/Pose.hpp"

#include <vector>

namespace aver::anim {

class Inertializer {
public:
    // Seconds for an offset with no initial velocity to halve. Default 0.1.
    void setHalfLife(f32 seconds);
    f32 halfLife() const { return halfLife_; }

    // Starts a transition. from / fromPrev: the last two OUTPUT poses, dtPrev apart. to / toPrev: the NEW
    // source sampled at the switch time and dtPrev earlier. Replaces any running offsets (their current
    // value is already inside `from`). `skipBone` is left untouched (the locked root); pass 0xFFFFFFFF for none.
    void begin(const Pose& from, const Pose& fromPrev, const Pose& to, const Pose& toPrev, f32 dtPrev, u32 skipBone);
    // Decays every offset by dt.
    void advance(f32 dt);
    // Adds the offsets to a pose of the new source.
    void apply(Pose& p) const;
    bool active() const { return active_; }
    void reset();

    // One offset component: channel 0-2 translation, 3-5 rotation vector, 6-8 scale. For tests and the debug view.
    f32 offset(u32 bone, u32 channel) const;
    f32 velocity(u32 bone, u32 channel) const;

private:
    static constexpr u32 kChannels = 9;
    std::vector<f32> x_, v_;
    u32 bones_ = 0;
    bool active_ = false;
    f32 halfLife_ = 0.1f;
};

} // namespace aver::anim
