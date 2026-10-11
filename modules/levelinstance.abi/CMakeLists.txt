# Aver.LevelInstance.Abi -- the plain-C seam a script places, streams and queries level instances through
# (docs/LEVEL_INSTANCES.md). The shape of Aver.Prefab.Abi: SHARED because the .NET host P/Invokes it, and
# STATELESS -- the host installs function pointers (aver_levelinst_set_host), so the one game::LevelInstances lives
# in the editor or the runtime and this DLL never owns a world.
add_library(Aver.LevelInstance.Abi SHARED abi/LevelInstanceAbi.cpp)
target_include_directories(Aver.LevelInstance.Abi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(Aver.LevelInstance.Abi PUBLIC cxx_std_20)
target_compile_definitions(Aver.LevelInstance.Abi PRIVATE AVER_LEVELINST_ABI_BUILD)
# Core is static, so this DLL gets its own aver_levelinst_last_error slot.
target_link_libraries(Aver.LevelInstance.Abi PRIVATE Aver.Core)
set_target_properties(Aver.LevelInstance.Abi PROPERTIES
  FOLDER "modules"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)   # next to the exe, where DllImport looks
