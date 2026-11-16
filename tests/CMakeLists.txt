cmake_minimum_required(VERSION 3.20)
project(nxdrpc_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # doctest v2.4.11 predates CMake 4
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

include_directories(../sysmodule/source)

add_executable(nxdrpc_tests
  main.cpp
  json_writer_test.cpp
  settings_test.cpp
  title_name_test.cpp
  http_test.cpp
  relay_url_test.cpp
  power_state_test.cpp
  ../sysmodule/source/core/json_writer.cpp
  ../sysmodule/source/core/settings.cpp
  ../sysmodule/source/core/title_name.cpp
  ../sysmodule/source/core/http.cpp
  ../sysmodule/source/core/relay_url.cpp
)
target_compile_options(nxdrpc_tests PRIVATE -Wall -Wextra)
target_link_libraries(nxdrpc_tests PRIVATE doctest::doctest)

enable_testing()
add_test(NAME nxdrpc_tests COMMAND nxdrpc_tests)
