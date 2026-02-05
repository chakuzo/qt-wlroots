cmake_minimum_required(VERSION 3.20)
project(wlroots-qt-compositor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find packages
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Widgets)
find_package(PkgConfig REQUIRED)

# Arch Linux uses versioned wlroots packages
pkg_check_modules(WLROOTS REQUIRED wlroots-0.19)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(PIXMAN REQUIRED pixman-1)

# Find wayland-scanner
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Protocol directories
set(WAYLAND_PROTOCOLS_DIR "/usr/share/wayland-protocols")
set(PROTOCOL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY ${PROTOCOL_OUTPUT_DIR})

# Function to generate protocol code
function(generate_wayland_protocol PROTOCOL_FILE PROTOCOL_NAME)
    # wlroots expects "xdg-shell-protocol.h" NOT "xdg-shell-server-protocol.h"
    set(PROTOCOL_HEADER "${PROTOCOL_OUTPUT_DIR}/${PROTOCOL_NAME}-protocol.h")
    set(CODE_FILE "${PROTOCOL_OUTPUT_DIR}/${PROTOCOL_NAME}-protocol.c")
    
    # Generate the header that wlroots expects
    add_custom_command(
        OUTPUT ${PROTOCOL_HEADER}
        COMMAND ${WAYLAND_SCANNER} server-header ${PROTOCOL_FILE} ${PROTOCOL_HEADER}
        DEPENDS ${PROTOCOL_FILE}
        VERBATIM
    )
    
    add_custom_command(
        OUTPUT ${CODE_FILE}
        COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_FILE} ${CODE_FILE}
        DEPENDS ${PROTOCOL_FILE}
        VERBATIM
    )
    
    set(${PROTOCOL_NAME}_HEADERS ${PROTOCOL_HEADER} PARENT_SCOPE)
    set(${PROTOCOL_NAME}_CODE ${CODE_FILE} PARENT_SCOPE)
endfunction()

# Generate required protocols
generate_wayland_protocol(
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml"
    "xdg-shell"
)

# Collect all generated files
set(PROTOCOL_SOURCES
    ${xdg-shell_CODE}
)

set(PROTOCOL_HEADERS
    ${xdg-shell_HEADERS}
)

# Custom target to ensure protocol headers are generated first
add_custom_target(generate_protocols DEPENDS ${PROTOCOL_HEADERS})

# C sources - wlroots wrapper
set(C_SOURCES
    src/compositor_core.c
    src/render_backend.c
    src/xdg_shell_handler.c
    src/seat_handler.c
    src/output_handler.c
)

# C++ sources - Qt integration
set(CXX_SOURCES
    src/main.cpp
    src/compositor_wrapper.cpp
    src/embedded_view.cpp
)

# Headers
set(HEADERS
    include/compositor_core.h
    include/render_backend.h
    include/xdg_shell_handler.h
    include/seat_handler.h
    include/output_handler.h
    include/compositor_wrapper.h
    include/embedded_view.h
)

# Qt Resources
set(QT_RESOURCES
    resources.qrc
)

# CRITICAL: Set C language for .c files explicitly
set_source_files_properties(${C_SOURCES} ${PROTOCOL_SOURCES} PROPERTIES LANGUAGE C)

add_executable(${PROJECT_NAME}
    ${C_SOURCES}
    ${CXX_SOURCES}
    ${HEADERS}
    ${PROTOCOL_SOURCES}
    ${PROTOCOL_HEADERS}
    ${QT_RESOURCES}
)

# Ensure protocols are generated before compiling
add_dependencies(${PROJECT_NAME} generate_protocols)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTOCOL_OUTPUT_DIR}
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Widgets
    ${WLROOTS_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${PIXMAN_LIBRARIES}
)

target_link_directories(${PROJECT_NAME} PRIVATE
    ${WLROOTS_LIBRARY_DIRS}
    ${WAYLAND_SERVER_LIBRARY_DIRS}
    ${WAYLAND_CLIENT_LIBRARY_DIRS}
    ${XKBCOMMON_LIBRARY_DIRS}
    ${PIXMAN_LIBRARY_DIRS}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${WLROOTS_CFLAGS_OTHER}
    -DWLR_USE_UNSTABLE
)

# Install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
