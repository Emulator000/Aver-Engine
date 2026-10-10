# RagdollBuilderTest -- a skeleton's bind pose to capsules and joint limits. Pure arithmetic and a
# little text parsing: no world, no physics, so a wrong radius or a finger given a part is decidable
# here rather than by watching a character fall over.
add_executable(RagdollBuilderTest src/RagdollBuilderTest.cpp)
target_link_libraries(RagdollBuilderTest PRIVATE Aver.Ragdoll Aver.Anim Aver.Formats Aver.Core Aver.Platform)
set_target_properties(RagdollBuilderTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# RagdollSystemTest -- the whole chain in a real physics world with no GPU: the pose-modifier seam,
# the world <-> local conversion for a moved, turned and scaled entity, a limp body falling onto a
# floor, the blend ramp, stop restoring the animation, and an impulse. Links the physics ABI directly
# (never Aver.Physics.Jolt): everything it checks is visible from outside the ABI.
add_executable(RagdollSystemTest src/RagdollSystemTest.cpp)
target_link_libraries(RagdollSystemTest PRIVATE Aver.Ragdoll Aver.Anim.Scene Aver.Anim Aver.Scene Aver.Formats
                                                Aver.Physics Aver.Core Aver.Platform)
set_target_properties(RagdollSystemTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
