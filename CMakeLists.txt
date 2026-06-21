# ShellBar v1.9.0 — A command-bar terminal emulator built on libghostty-vt
# Copyright (c) 2026 Xavier Araque <xavieraraque@gmail.com>
# MIT License

cmake_minimum_required(VERSION 3.19)
project(shellbar C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ADW REQUIRED libadwaita-1)
pkg_check_modules(PANGO REQUIRED pango)
pkg_check_modules(CAIRO REQUIRED cairo)

set(ZIG_VERSION_REQUIRED "0.15.2")
set(ZIG_DIR "${CMAKE_BINARY_DIR}/zig-x86_64-linux-${ZIG_VERSION_REQUIRED}")
set(ZIG_EXECUTABLE "${ZIG_DIR}/zig" CACHE FILEPATH "Path to Zig compiler")
set(ZIG_NEED_DOWNLOAD TRUE)

find_program(SYSTEM_ZIG zig)
if(SYSTEM_ZIG)
    execute_process(
        COMMAND "${SYSTEM_ZIG}" version
        OUTPUT_VARIABLE SYSTEM_ZIG_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(SYSTEM_ZIG_VERSION STREQUAL ZIG_VERSION_REQUIRED)
        set(ZIG_EXECUTABLE "${SYSTEM_ZIG}" CACHE FILEPATH "Path to Zig compiler" FORCE)
        set(ZIG_NEED_DOWNLOAD FALSE)
        message(STATUS "Using system Zig ${ZIG_VERSION_REQUIRED}")
    endif()
endif()

if(ZIG_NEED_DOWNLOAD)
    if(NOT EXISTS "${ZIG_DIR}/zig")
        set(ZIG_TARBALL "zig-x86_64-linux-${ZIG_VERSION_REQUIRED}.tar.xz")
        set(ZIG_URL "https://ziglang.org/download/${ZIG_VERSION_REQUIRED}/${ZIG_TARBALL}")
        set(ZIG_ARCHIVE "${CMAKE_BINARY_DIR}/${ZIG_TARBALL}")

        message(STATUS "Downloading Zig ${ZIG_VERSION_REQUIRED} (ghostty requires this exact version)...")
        file(DOWNLOAD "${ZIG_URL}" "${ZIG_ARCHIVE}"
             STATUS ZIG_DOWNLOAD_STATUS SHOW_PROGRESS)

        list(GET ZIG_DOWNLOAD_STATUS 0 ZIG_DOWNLOAD_EXIT_CODE)
        if(NOT ZIG_DOWNLOAD_EXIT_CODE EQUAL 0)
            list(GET ZIG_DOWNLOAD_STATUS 1 ZIG_DOWNLOAD_ERROR)
            message(FATAL_ERROR "Failed to download Zig: ${ZIG_DOWNLOAD_ERROR}")
        endif()

        message(STATUS "Extracting Zig ${ZIG_VERSION_REQUIRED}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${ZIG_ARCHIVE}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            RESULT_VARIABLE ZIG_EXTRACT_RESULT)

        if(NOT ZIG_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract Zig archive")
        endif()

        file(REMOVE "${ZIG_ARCHIVE}")
    endif()
    set(ZIG_EXECUTABLE "${ZIG_DIR}/zig" CACHE FILEPATH "Path to Zig compiler" FORCE)
    message(STATUS "Using downloaded Zig ${ZIG_VERSION_REQUIRED}")
endif()

include(FetchContent)
FetchContent_Declare(ghostty
  GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
  GIT_TAG b0f8276658fbcc75318d2125d40146074a3fc505
)
FetchContent_MakeAvailable(ghostty)

add_executable(shellbar
  shellbar.c
  sb_window.c
  sb_terminal.c
  sb_toolbar.c
  sb_config.c
  sb_preferences_dialog.c
  sb_theme.c
  sb_indexer.c
)

file(GLOB ICON_FILES CONFIGURE_DEPENDS assets/icon-*.png assets/*.svg assets/logo.png)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shellbar-resources.c
  COMMAND glib-compile-resources
    --target=${CMAKE_CURRENT_BINARY_DIR}/shellbar-resources.c
    --generate-source
    ${CMAKE_CURRENT_SOURCE_DIR}/shellbar.gresource.xml
  DEPENDS shellbar.gresource.xml
          ${ICON_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Compiling GResource: shellbar.gresource.xml"
)
target_sources(shellbar PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shellbar-resources.c)
target_include_directories(shellbar PRIVATE
  ${ghostty_SOURCE_DIR}/include
  ${GTK4_INCLUDE_DIRS}
  ${ADW_INCLUDE_DIRS}
  ${PANGO_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
)
target_link_libraries(shellbar PRIVATE
  ghostty-vt
  ${GTK4_LIBRARIES}
  ${ADW_LIBRARIES}
  ${PANGO_LIBRARIES}
  ${CAIRO_LIBRARIES}
)
target_compile_options(shellbar PRIVATE
  ${GTK4_CFLAGS_OTHER}
  ${ADW_CFLAGS_OTHER}
)

if(UNIX AND NOT APPLE)
  target_link_libraries(shellbar PRIVATE util)
endif()
