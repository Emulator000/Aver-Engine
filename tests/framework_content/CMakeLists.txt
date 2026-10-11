# Localization store and data-table store behind framework_loc_abi.h / framework_table_abi.h
# (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md 4.2): culture chain, resolution, hot reload, handles.
# Links the Aver.Framework DLL plus the format readers the tests write fixtures with.
# NO add_test: aver_register_ctest_in (root CMakeLists) picks up *Test executables.
add_executable(FrameworkContentTest src/FrameworkContentTest.cpp)
target_link_libraries(FrameworkContentTest PRIVATE Aver.Framework Aver.Formats Aver.Core Aver.Platform)
set_target_properties(FrameworkContentTest PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
