cmake_minimum_required(VERSION 3.14)
project(remind VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── warnings ──────────────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── LTO for Release ───────────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# ── nlohmann/json via FetchContent ────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Copy the downloaded json.hpp into the source tree so #include works.
set(JSON_HPP_SRC "${nlohmann_json_SOURCE_DIR}/json.hpp")
set(JSON_HPP_DST "${CMAKE_CURRENT_SOURCE_DIR}/src/common/json.hpp")

add_custom_command(
    OUTPUT  "${JSON_HPP_DST}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${JSON_HPP_SRC}" "${JSON_HPP_DST}"
    DEPENDS "${JSON_HPP_SRC}"
    COMMENT "Copying nlohmann/json.hpp"
)
add_custom_target(copy_json ALL DEPENDS "${JSON_HPP_DST}")

find_package(CURL REQUIRED)

# ── common object library ─────────────────────────────────────────────────────
add_library(remind_common STATIC
    src/common/socket.cpp
    src/common/paths.cpp
    src/common/credentials.cpp
    src/common/sync_state.cpp
    src/common/http_client.cpp
    src/common/store.cpp
)
target_include_directories(remind_common PUBLIC src/common)
target_link_libraries(remind_common PUBLIC CURL::libcurl)
add_dependencies(remind_common copy_json)

# ── reminderd ─────────────────────────────────────────────────────────────────
add_executable(reminderd
    src/daemon/main.cpp
    src/daemon/sync_worker.cpp
)
target_include_directories(reminderd PRIVATE src/common)
target_link_libraries(reminderd PRIVATE remind_common)
add_dependencies(reminderd copy_json)

# ── remind (CLI) ──────────────────────────────────────────────────────────────
add_executable(remind
    src/cli/main.cpp
)
target_include_directories(remind PRIVATE src/common)
target_link_libraries(remind PRIVATE remind_common)
add_dependencies(remind copy_json)

# ── remind-notify (macOS Swift notification helper) ───────────────────────────
if(APPLE)
    find_program(SWIFTC swiftc)
    if(SWIFTC)
        set(NOTIFY_APP "${CMAKE_BINARY_DIR}/Remindr.app")
        set(NOTIFY_EXE "${NOTIFY_APP}/Contents/MacOS/remind-notify")

        set(ICONSET_DIR "${CMAKE_BINARY_DIR}/Remindr.iconset")
        set(ICNS_FILE   "${NOTIFY_APP}/Contents/Resources/Remindr.icns")

        add_custom_command(
            OUTPUT "${NOTIFY_EXE}" "${ICNS_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${NOTIFY_APP}/Contents/MacOS"
            COMMAND "${SWIFTC}"
                    -o "${NOTIFY_EXE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/main.swift"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/Info.plist"
                    "${NOTIFY_APP}/Contents/Info.plist"
            # Generate app icon: compile the generator, render iconset, convert with iconutil.
            COMMAND "${SWIFTC}" -framework Cocoa
                    -o "${CMAKE_BINARY_DIR}/make_icon"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/make_icon.swift"
            COMMAND "${CMAKE_BINARY_DIR}/make_icon" "${ICONSET_DIR}"
            COMMAND iconutil -c icns -o "${ICNS_FILE}" "${ICONSET_DIR}"
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/main.swift"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/Info.plist"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/notify/make_icon.swift"
            COMMENT "Building Remindr notification helper"
        )
        add_custom_target(remind_notify ALL DEPENDS "${NOTIFY_EXE}" "${ICNS_FILE}")
    else()
        message(STATUS "swiftc not found — notification helper not built (osascript fallback active)")
    endif()
endif()

# ── install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS remind reminderd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE AND SWIFTC)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/Remindr.app"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        USE_SOURCE_PERMISSIONS
    )
endif()
