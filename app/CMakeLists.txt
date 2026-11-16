cmake_minimum_required(VERSION 3.25)

# Borealis vendors tweeny, whose CMakeLists predates CMake 4.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(nx-discord-rpc C CXX)

set(APP_TITLE "NX Discord RPC")
set(APP_AUTHOR "0xsharkboy")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION APP_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ROMFS_DIR ${CMAKE_CURRENT_BINARY_DIR}/resources)

# Desktop builds read resources from <build dir>/resources wherever they are started from.
set(BRLS_RESOURCES_DIR ${CMAKE_CURRENT_BINARY_DIR})
add_subdirectory(${BOREALIS_LIBRARY} borealis)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(APPEND APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/qrcodegen/qrcodegen.cpp
    ${REPO_ROOT}/common/source/shared/discord_ids.cpp)
if (PLATFORM_SWITCH)
    list(APPEND APP_SOURCES
        ${REPO_ROOT}/common/source/client_switch.cpp
        ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
else ()
    list(APPEND APP_SOURCES ${REPO_ROOT}/common/source/client_fake.cpp)
endif ()

program_target(${PROJECT_NAME} "${APP_SOURCES}")
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${REPO_ROOT}/common/include)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>
    -ffunction-sections -fdata-sections)
if (PLATFORM_SWITCH)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB})
# Silence Borealis header warnings in app sources. Its bundled tinyxml2 directory stays a plain -I
# so it still shadows the devkitPro portlibs copy.
get_target_property(BOREALIS_INCLUDES borealis INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${BOREALIS_INCLUDES})
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/qrcodegen)
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION_STRING="${APP_VERSION}" NXDRPC_VERSION="${APP_VERSION}")

set(BRLS_RES ${BOREALIS_DIR}/resources)
set(STAGE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ROMFS_DIR}/i18n/en-US ${ROMFS_DIR}/img
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BRLS_RES}/material ${ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BRLS_RES}/img/sys ${ROMFS_DIR}/img/sys
    COMMAND ${CMAKE_COMMAND} -E copy ${BRLS_RES}/i18n/en-US/hints.json ${ROMFS_DIR}/i18n/en-US/hints.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n ${ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/console.png ${ROMFS_DIR}/img/console.png
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/console_idle.png ${ROMFS_DIR}/img/console_idle.png
    COMMAND ${CMAKE_COMMAND} -E copy ${APP_ICON} ${ROMFS_DIR}/img/app_icon.jpg)
if (PLATFORM_DESKTOP)
    list(APPEND STAGE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BRLS_RES}/font ${ROMFS_DIR}/font
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BRLS_RES}/icon ${ROMFS_DIR}/icon)
endif ()
add_custom_target(${PROJECT_NAME}.resources ${STAGE_COMMANDS} VERBATIM)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.resources)

if (PLATFORM_SWITCH)
    if (USE_DEKO3D)
        gen_dksh(${ROMFS_DIR}/shaders)
        foreach (shader fill_vsh fill_aa_fsh fill_fsh)
            if (NOT EXISTS ${ROMFS_DIR}/shaders/${shader}.dksh)
                message(FATAL_ERROR "uam did not produce ${ROMFS_DIR}/shaders/${shader}.dksh")
            endif ()
        endforeach ()
    endif ()
    add_custom_target(${PROJECT_NAME}.nro ALL
        DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create ${APP_TITLE} ${APP_AUTHOR} ${APP_VERSION} ${PROJECT_NAME}.nacp
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
            --icon=${APP_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${ROMFS_DIR}
        VERBATIM)
endif ()
