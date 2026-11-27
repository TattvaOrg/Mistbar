cmake_minimum_required(VERSION 3.20)
project(Mistbar VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MISTBAR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(RSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
pkg_check_modules(WAYLAND_EGL IMPORTED_TARGET wayland-egl egl gl)
pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

function(mistbar_add_wayland_protocol XML_PATH OUT_BASENAME)
    set(HDR "${PROTO_GEN_DIR}/${OUT_BASENAME}-client-protocol.h")
    set(SRC "${PROTO_GEN_DIR}/${OUT_BASENAME}-protocol.c")
    add_custom_command(
        OUTPUT "${HDR}" "${SRC}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${XML_PATH}" "${HDR}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${XML_PATH}" "${SRC}"
        DEPENDS "${XML_PATH}"
        COMMENT "Generating Wayland protocol code for ${OUT_BASENAME}"
    )
    list(APPEND MISTBAR_PROTO_SOURCES "${SRC}")
    list(APPEND MISTBAR_PROTO_HEADERS "${HDR}")
    set(MISTBAR_PROTO_SOURCES "${MISTBAR_PROTO_SOURCES}" PARENT_SCOPE)
    set(MISTBAR_PROTO_HEADERS "${MISTBAR_PROTO_HEADERS}" PARENT_SCOPE)
endfunction()

mistbar_add_wayland_protocol("${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-shell.xml" "xdg-shell")
mistbar_add_wayland_protocol("${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-decoration-unstable-v1.xml" "xdg-decoration-unstable-v1")
mistbar_add_wayland_protocol("${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml" "wlr-layer-shell-unstable-v1")
mistbar_add_wayland_protocol("${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-foreign-toplevel-management-unstable-v1.xml" "wlr-foreign-toplevel-management-unstable-v1")
mistbar_add_wayland_protocol("${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml" "wlr-screencopy-unstable-v1")

add_library(mistbar_protocols STATIC ${MISTBAR_PROTO_SOURCES} ${MISTBAR_PROTO_HEADERS})
target_include_directories(mistbar_protocols PUBLIC "${PROTO_GEN_DIR}")
target_link_libraries(mistbar_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)

set(MISTBAR_CXX_FLAGS -Wall -Wextra -Wpedantic)
if(MISTBAR_WARNINGS_AS_ERRORS)
    list(APPEND MISTBAR_CXX_FLAGS -Werror)
endif()

add_library(mistbar_core STATIC
    src/common/logger.cpp
    src/common/xdg.cpp
    src/common/json.cpp
    src/common/config.cpp
    src/common/ipc.cpp
    src/common/svg_loader.cpp
    src/common/wayland_client.cpp
    src/common/sysinfo.cpp
    src/animations/bezier/bezier.cpp
    src/animations/physics/spring.cpp
    src/animations/transitions/transition.cpp
    src/themes/theme.cpp
    src/glass/blur/gaussian_blur.cpp
    src/glass/blur/gl_blur.cpp
    src/glass/blur/mizzle_renderer.cpp
    src/glass/specular/specular.cpp
    src/glass/translucent/glass_panel.cpp
    src/window/buttons/traffic_lights.cpp
    src/window/shadows/window_shadow.cpp
    src/window/titlebar/window_frame.cpp
)

target_compile_options(mistbar_core PRIVATE ${MISTBAR_CXX_FLAGS})
target_include_directories(mistbar_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROTO_GEN_DIR}"
)
target_compile_definitions(mistbar_core PUBLIC
    MISTBAR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    MISTBAR_INSTALL_PREFIX="${CMAKE_INSTALL_PREFIX}"
    MISTBAR_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(mistbar_core PUBLIC
    mistbar_protocols
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::XKBCOMMON
    PkgConfig::CAIRO
    PkgConfig::PANGOCAIRO
    PkgConfig::GDK_PIXBUF
    PkgConfig::RSVG
    PkgConfig::DBUS
)

if(SYSTEMD_FOUND)
    target_compile_definitions(mistbar_core PUBLIC MISTBAR_HAS_SYSTEMD=1)
    target_link_libraries(mistbar_core PUBLIC PkgConfig::SYSTEMD)
else()
    target_compile_definitions(mistbar_core PUBLIC MISTBAR_HAS_SYSTEMD=0)
endif()

if(WAYLAND_EGL_FOUND)
    target_compile_definitions(mistbar_core PUBLIC MISTBAR_HAS_OPENGL=1)
    target_link_libraries(mistbar_core PUBLIC PkgConfig::WAYLAND_EGL)
else()
    target_compile_definitions(mistbar_core PUBLIC MISTBAR_HAS_OPENGL=0)
endif()

add_executable(mistbar-daemon
    src/bar/bar_app.cpp
    src/bar/bar_main.cpp
)
target_compile_options(mistbar-daemon PRIVATE ${MISTBAR_CXX_FLAGS})
target_link_libraries(mistbar-daemon PRIVATE mistbar_core)

add_executable(mistbar-settings
    src/settings/settings_main.cpp
)
target_compile_options(mistbar-settings PRIVATE ${MISTBAR_CXX_FLAGS})
target_link_libraries(mistbar-settings PRIVATE mistbar_core)

add_executable(mistbar_tests
    tests/mistbar_test.cpp
)
target_compile_options(mistbar_tests PRIVATE ${MISTBAR_CXX_FLAGS})
target_link_libraries(mistbar_tests PRIVATE mistbar_core)

enable_testing()
add_test(NAME mistbar_unit_tests COMMAND mistbar_tests)

install(TARGETS mistbar-daemon mistbar-settings RUNTIME DESTINATION bin)
install(DIRECTORY icons DESTINATION share/mistbar)
install(DIRECTORY shaders DESTINATION share/mistbar)
install(FILES data/mistbar.desktop DESTINATION share/applications)
install(FILES icons/mistbar-logo.svg DESTINATION share/icons/hicolor/scalable/apps RENAME mistbar.svg)
