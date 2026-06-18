cmake_minimum_required(VERSION 3.14)
project(remind VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── warnings ──────────────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

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

# ── common object library ─────────────────────────────────────────────────────
add_library(remind_common STATIC
    src/common/socket.cpp
)
target_include_directories(remind_common PUBLIC src/common)
add_dependencies(remind_common copy_json)

# ── reminderd ─────────────────────────────────────────────────────────────────
add_executable(reminderd
    src/daemon/main.cpp
)
target_include_directories(reminderd PRIVATE src/common)
target_link_libraries(reminderd PRIVATE remind_common)
add_dependencies(reminderd copy_json)

# Optional: link libnotify on Linux if available.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBNOTIFY QUIET libnotify)
        if(LIBNOTIFY_FOUND)
            target_compile_definitions(reminderd PRIVATE HAVE_LIBNOTIFY)
            target_include_directories(reminderd PRIVATE ${LIBNOTIFY_INCLUDE_DIRS})
            target_link_libraries(reminderd PRIVATE ${LIBNOTIFY_LIBRARIES})
        endif()
    endif()
endif()

# ── remind (CLI) ──────────────────────────────────────────────────────────────
add_executable(remind
    src/cli/main.cpp
)
target_include_directories(remind PRIVATE src/common)
target_link_libraries(remind PRIVATE remind_common)
add_dependencies(remind copy_json)

# ── install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS remind reminderd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
