# Montage C ABI (framework_anim_abi.h): the relay to an installed provider and the zeros without one.
# Links the Aver.Framework DLL only: the provider is a fake here, so no animation module is needed.
# NO add_test: aver_register_ctest_in (root CMakeLists) picks up *Test executables.
add_executable(FrameworkAnimAbiTest src/FrameworkAnimAbiTest.cpp)
target_link_libraries(FrameworkAnimAbiTest PRIVATE Aver.Framework Aver.Core)
set_target_properties(FrameworkAnimAbiTest PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
