# Level instance tests (docs/LEVEL_INSTANCES.md). No add_test here: aver_register_ctest_in (root CMakeLists) registers
# every target whose name ends in Test, with the timeout and the FAIL_REGULAR_EXPRESSION these suites are written against.

# OcLevelInstTest -- the LEVELINST / LIOVERRIDE / ENDLEVELINST / EXTENT records of .ocworld: round trips, ids, unknown
# records, and that a level with none of them writes what it always wrote. Formats only: no scene, no GPU.
add_executable(OcLevelInstTest src/OcLevelInstTest.cpp)
target_link_libraries(OcLevelInstTest PRIVATE Aver.Formats Aver.Core Aver.Platform)
set_target_properties(OcLevelInstTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# OcPackTest -- the .ocpack sidecar of a baked level pack and the instance-row matrix helper.
add_executable(OcPackTest src/OcPackTest.cpp)
target_link_libraries(OcPackTest PRIVATE Aver.Formats Aver.Core Aver.Platform)
set_target_properties(OcPackTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# LevelAssetLibraryTest -- world::LevelAssetLibrary: parse cache, closure and cycle checks, ids, override resolution,
# bounds. Aver.World compiles it without a scene: nothing here needs an entity.
add_executable(LevelAssetLibraryTest src/LevelAssetLibraryTest.cpp)
target_link_libraries(LevelAssetLibraryTest PRIVATE Aver.World Aver.Formats Aver.Platform Aver.Core)
set_target_properties(LevelAssetLibraryTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# LevelPackTest -- world::bakeLevelPack: which placements pack, the rows, KEEP, determinism and staleness.
add_executable(LevelPackTest src/LevelPackTest.cpp)
target_link_libraries(LevelPackTest PRIVATE Aver.World Aver.Formats Aver.Platform Aver.Core)
set_target_properties(LevelPackTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# LevelInstanceAbiTest -- the plain-C seam: no host, a fake host, null function pointers, bad arguments.
add_executable(LevelInstanceAbiTest src/LevelInstanceAbiTest.cpp)
target_link_libraries(LevelInstanceAbiTest PRIVATE Aver.LevelInstance.Abi Aver.Core)
set_target_properties(LevelInstanceAbiTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# The runtime half needs Aver.Runtime.Game.Core (it exists whenever either host is built).
if(TARGET Aver.Runtime.Game.Core)
  # LevelInstancesTest -- game::LevelInstances against a fake host: realise, nesting, cycles, overrides, streaming,
  # edit in place, break, classes, lights and decals, records, rescan. A real scene::World; no GPU.
  add_executable(LevelInstancesTest src/LevelInstancesTest.cpp)
  target_link_libraries(LevelInstancesTest PRIVATE Aver.Runtime.Game.Core Aver.Scene Aver.World Aver.Formats Aver.Platform Aver.Core)
  set_target_properties(LevelInstancesTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

  # LevelInstancesPackedTest -- packed instances: rows through the instance matrix, KEEP placements as entities, and every
  # reason to fall back to unpacked.
  add_executable(LevelInstancesPackedTest src/LevelInstancesPackedTest.cpp)
  target_link_libraries(LevelInstancesPackedTest PRIVATE Aver.Runtime.Game.Core Aver.Scene Aver.World Aver.Formats Aver.Platform Aver.Core)
  set_target_properties(LevelInstancesPackedTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

  # LevelInstanceEditorModelTest -- the editor's model, with no UI. It COMPILES the sandbox translation unit directly rather
  # than linking a library, the same unusual line PrefabEditorModelTest has, because the editor's code has no business being
  # reachable from the engine just to be testable.
  add_executable(LevelInstanceEditorModelTest
    src/LevelInstanceEditorModelTest.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/src/LevelInstanceEditorModel.cpp)
  target_include_directories(LevelInstanceEditorModelTest PRIVATE ${CMAKE_SOURCE_DIR}/sandbox/src)
  target_link_libraries(LevelInstanceEditorModelTest PRIVATE Aver.Runtime.Game.Core Aver.Scene Aver.World Aver.Formats Aver.Platform Aver.Core)
  set_target_properties(LevelInstanceEditorModelTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
