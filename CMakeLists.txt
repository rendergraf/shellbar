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

set(ZIG_EXECUTABLE ${CMAKE_SOURCE_DIR}/build/zig-x86_64-linux-0.15.2/zig CACHE FILEPATH "Path to Zig compiler")

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
