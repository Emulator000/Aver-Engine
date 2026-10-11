# Gameplay Ability System, managed core (scripting/csharp/Aver.Framework.Tests): tags, attributes, effects,
# abilities, the .ocgas parser, the save payload and the replication seam, all against a fake IGasHost so no
# engine DLL is needed. Run by hand with `dotnet run --project scripting/csharp/Aver.Framework.Tests`; this
# wraps the same command so ctest sees it. When no .NET SDK is on PATH the test is not registered (a skip, never
# a failure). The real-CLR half lives in tests/save (GasPersistTest); the native half in tests/save (SaveWorldTest).
find_program(AVER_DOTNET dotnet)
if(AVER_DOTNET)
  add_test(NAME GasManagedTests
    COMMAND "${AVER_DOTNET}" run --project "${CMAKE_SOURCE_DIR}/scripting/csharp/Aver.Framework.Tests/Aver.Framework.Tests.csproj"
            --configuration Release --nologo
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(GasManagedTests PROPERTIES
    TIMEOUT 600
    ENVIRONMENT "DOTNET_CLI_TELEMETRY_OPTOUT=1;DOTNET_NOLOGO=1"
    FAIL_REGULAR_EXPRESSION "FAIL: ")
else()
  message(STATUS "GasManagedTests skipped: no dotnet on PATH")
endif()
