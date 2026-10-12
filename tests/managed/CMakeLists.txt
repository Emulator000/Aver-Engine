# The managed suites that were run only by hand: Aver.Graph.Tests (Aver Node compiler, nodes, host) and
# Aver.DataLoc.Tests (data tables, localisation). Both exit with their failure count, the same contract as
# GasManagedTests (tests/gas). Graph.Tests resolves fixtures from its own folder, so it runs there.
# Not registered without a .NET SDK on PATH (a skip, never a failure).
find_program(AVER_DOTNET dotnet)
if(AVER_DOTNET)
  add_test(NAME GraphManagedTests
    COMMAND "${AVER_DOTNET}" run --configuration Release --nologo
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/scripting/csharp/Aver.Graph.Tests")
  add_test(NAME DataLocManagedTests
    COMMAND "${AVER_DOTNET}" run --project "${CMAKE_SOURCE_DIR}/scripting/csharp/Aver.DataLoc.Tests/Aver.DataLoc.Tests.csproj"
            --configuration Release --nologo
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(GraphManagedTests DataLocManagedTests PROPERTIES
    TIMEOUT 900
    ENVIRONMENT "DOTNET_CLI_TELEMETRY_OPTOUT=1;DOTNET_NOLOGO=1"
    FAIL_REGULAR_EXPRESSION "FAIL: |FAIL  ")
  # Each `dotnet run` builds Aver.Framework into the same obj/ (GasManagedTests too, tests/gas): one at a time.
  set_tests_properties(GraphManagedTests DataLocManagedTests PROPERTIES RESOURCE_LOCK aver_managed_build)
else()
  message(STATUS "Managed suites skipped: no dotnet on PATH")
endif()
