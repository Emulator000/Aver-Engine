# Environment queries, smart objects and state trees (docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md).
# Executables named *Test are registered automatically by the recursive walk in the root CMakeLists.
# Added from the root beside tests/synapse_bt, under the same Aver.Synapse guard.

# The pure suites: the tokenizer, the three text formats and their exact writers, the EQS evaluator against
# a fake environment, the smart-object registry and the StateTree evaluator. No scene, no physics, no device.
foreach(_t TextRecordsTest EqsAssetTest EqsTest SmartObjectAssetTest SmartObjectTest StateTreeAssetTest StateTreeTest)
  add_executable(${_t} src/${_t}.cpp)
  target_link_libraries(${_t} PRIVATE Aver.Synapse Aver.Formats Aver.Core Aver.Platform)
  set_target_properties(${_t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()

# The scene joins, against a real scene::World. EqsSceneTest also traces against real Jolt when physics is built
# (Aver.Synapse.Scene carries Aver.Physics and its AVER_MODULE_PHYSICS definition along).
if(TARGET Aver.Synapse.Scene)
  foreach(_t EqsSceneTest SmartObjectSceneTest StateTreeSystemTest)
    add_executable(${_t} src/${_t}.cpp)
    target_link_libraries(${_t} PRIVATE Aver.Synapse.Scene Aver.Synapse Aver.Scene Aver.Formats Aver.Core Aver.Platform)
    set_target_properties(${_t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  endforeach()
endif()

# QueryAbiTest -- synapse_query_abi.h against the real Aver.Synapse.Abi DLL, with a C fake playing the managed side.
if(TARGET Aver.Synapse.Abi)
  add_executable(QueryAbiTest src/QueryAbiTest.cpp)
  target_link_libraries(QueryAbiTest PRIVATE Aver.Synapse.Abi Aver.Scene Aver.Synapse Aver.Formats Aver.Core Aver.Platform)
  set_target_properties(QueryAbiTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# AiEditorPartsTest -- the editors' headless cores. Like BtGraphEditorTest, Sandbox is add_executable-only, so the
# sandbox sources are compiled here directly with AVER_WITH_IMGUI undefined.
if(TARGET Aver.RHI AND TARGET Aver.Synapse.Scene)
  add_executable(AiEditorPartsTest
    src/AiEditorPartsTest.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/src/BtGraphEditor.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/src/StateTreeEditor.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/src/EqsEditor.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/src/AiEditorParts.cpp)
  target_include_directories(AiEditorPartsTest PRIVATE ${CMAKE_SOURCE_DIR}/sandbox/src)
  target_link_libraries(AiEditorPartsTest PRIVATE Aver.Synapse.Scene Aver.Synapse Aver.Scene Aver.Formats Aver.Core Aver.Platform Aver.RHI)
  set_target_properties(AiEditorPartsTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# ManagedAiClrTest -- the C# seam with a real .NET runtime; a machine without one reports UNAVAILABLE and passes.
# The managed logic itself is scripting/csharp/Aver.Framework.Tests (AiManagedTests.cs).
if(TARGET Aver.Scripting.Host AND TARGET Aver.Framework AND TARGET Aver.Synapse.Abi)
  add_executable(ManagedAiClrTest src/ManagedAiClrTest.cpp)
  target_link_libraries(ManagedAiClrTest PRIVATE Aver.Synapse.Abi Aver.Scene Aver.Synapse Aver.Formats
                                                 Aver.Framework Aver.Scripting.Host Aver.Core Aver.Platform)
  set_target_properties(ManagedAiClrTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  add_dependencies(ManagedAiClrTest Aver.Scripting.Host)   # stages the bridge and the sample actor assembly
endif()
