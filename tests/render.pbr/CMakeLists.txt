# CoatEnergyTest -- the layered BSDF's coat lobe, decided by arithmetic rather than by looking.
#
# Links Aver.Core alone. The composition under test is a handful of floats, and the property that
# matters -- a coat must never reflect more light than it receives -- is exactly the kind of claim a
# rendered image cannot settle: a plate that is 3% too bright still looks like a plate.
#
# The white furnace (--furnace-grid) is the GPU-side oracle for the same property and stays the
# confirmation, but it reads TONEMAPPED 8-BIT codes and cannot say WHICH term is wrong when a plate
# comes back bright. This reads raw floats and tests the terms separately.
#
# AVER_REPO_ROOT so it can read the shader it mirrors: the arithmetic here is a C++ port of HLSL, and
# a port that has drifted passes its own tests while the GPU does something else. Same define, and
# the same reason, as tests/render.voxi and tests/repo.
add_executable(CoatEnergyTest src/CoatEnergyTest.cpp)
target_link_libraries(CoatEnergyTest PRIVATE Aver.Core)
target_compile_definitions(CoatEnergyTest PRIVATE AVER_REPO_ROOT="${CMAKE_SOURCE_DIR}")
set_target_properties(CoatEnergyTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# TerrainLayoutTest -- the layered terrain material's CPU/GPU contract (wave 3, landscape layers): the
# HLSL defines against TerrainLayers.hpp, and the parameter texels. Reads the shader like CoatEnergyTest.
if(TARGET Aver.Render.PBR.Materials)
  add_executable(TerrainLayoutTest src/TerrainLayoutTest.cpp)
  target_link_libraries(TerrainLayoutTest PRIVATE Aver.Render.PBR.Materials Aver.Core)
  target_compile_definitions(TerrainLayoutTest PRIVATE AVER_REPO_ROOT="${CMAKE_SOURCE_DIR}")
  set_target_properties(TerrainLayoutTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
