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

file(STRINGS ../VERSION NXDRPC_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)

include_directories(../sysmodule/source ../common/include ../app/source)

add_executable(nxdrpc_tests
  main.cpp
  json_writer_test.cpp
  settings_test.cpp
  protocol_test.cpp
  title_name_test.cpp
  http_test.cpp
  relay_url_test.cpp
  power_state_test.cpp
  report_test.cpp
  relay_client_test.cpp
  json_extract_test.cpp
  link_client_test.cpp
  ipc_dispatch_test.cpp
  settings_saver_test.cpp
  retry_test.cpp
  version_test.cpp
  client_fake_test.cpp
  app_presentation_test.cpp
  discord_ids_test.cpp
  avatar_cache_test.cpp
  ../sysmodule/source/core/json_writer.cpp
  ../sysmodule/source/core/settings.cpp
  ../sysmodule/source/core/title_name.cpp
  ../sysmodule/source/core/http.cpp
  ../sysmodule/source/core/relay_url.cpp
  ../sysmodule/source/core/report.cpp
  ../sysmodule/source/core/relay_client.cpp
  ../sysmodule/source/core/ipc_convert.cpp
  ../sysmodule/source/core/json_extract.cpp
  ../sysmodule/source/core/link_client.cpp
  ../sysmodule/source/core/shared_state.cpp
  ../sysmodule/source/core/ipc_dispatch.cpp
  ../sysmodule/source/core/settings_saver.cpp
  ../common/source/client_fake.cpp
  ../common/source/shared/discord_ids.cpp
  ../common/source/shared/net_log.cpp
  ../app/source/presentation.cpp
  ../app/source/avatar_cache.cpp
)
target_compile_options(nxdrpc_tests PRIVATE -Wall -Wextra)
target_compile_definitions(nxdrpc_tests PRIVATE
  NXDRPC_VERSION="${NXDRPC_VERSION}"
  NXDRPC_TEST_VERSION="${NXDRPC_VERSION}")
target_link_libraries(nxdrpc_tests PRIVATE doctest::doctest)

enable_testing()
add_test(NAME nxdrpc_tests COMMAND nxdrpc_tests)
