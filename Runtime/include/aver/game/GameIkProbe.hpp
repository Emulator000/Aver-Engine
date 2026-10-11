// The ground probe IK foot placement casts with (docs/plans/wave5/IK_RETARGETING.md 4.5).
//
// Aver.Anim.Scene has no physics edge: the control rig system takes a function pointer, and this is the one the
// hosts install. It casts straight down in world space through aver_phys_raycast_ex and answers with the first hit
// that is not the character itself. A character is a visible broadphase body and the ray starts inside it (the
// probe origin is above the foot), so a hit on the entity's own body is stepped past and the ray goes again, at
// most four times. Landscape heightfield hits report entity 0, which is never "self".
//
// Included by both hosts (GameApp, SandboxApp); installIkGroundProbe() is called next to the ragdoll install block.
// Outside Play the editor has no physics bodies, so every foot sees "no hit" and the character animates unchanged.
#pragma once

#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"

#if AVER_MODULE_SCENE && AVER_MODULE_PHYSICS
#  include "aver/anim/ControlRig.hpp"
#  include "aver/physics/physics_layers_abi.h"

namespace aver::game {

inline bool probeGround(scene::Entity self, const Vec3& originWorld, f32 distanceCm, u32 layerMask, Vec3& outPoint,
                        Vec3& outNormal, void*) {
    Vec3 o = originWorld;
    f32 left = distanceCm;
    for (int attempt = 0; attempt < 4; ++attempt) {
        float p[3] = {0, 0, 0}, n[3] = {0, 0, 1};
        int32_t entity = 0;
        const int32_t body = aver_phys_raycast_ex(o.x, o.y, o.z, 0.0f, 0.0f, -1.0f, left, layerMask, 0, p, n, &entity);
        if (body == 0) return false;
        if (self != scene::kInvalidEntity && static_cast<scene::Entity>(entity) == self) {
            // The character's own body: continue from half a centimetre below the hit.
            const f32 advance = (o.z - p[2]) + 0.5f;
            o.z -= advance;
            left -= advance;
            if (left <= 0.0f) return false;
            continue;
        }
        outPoint = Vec3{p[0], p[1], p[2]};
        outNormal = Vec3{n[0], n[1], n[2]}.getSafeNormal();
        if (outNormal.sizeSquared() < 0.5f) outNormal = Vec3{0, 0, 1};
        return true;
    }
    return false;
}

inline void installIkGroundProbe() { anim::controlRigSystem().setGroundProbe(&probeGround, nullptr); }

} // namespace aver::game

#else

namespace aver::game {
inline void installIkGroundProbe() {}
} // namespace aver::game

#endif
