cmake_minimum_required(VERSION 3.10)
project(Snapper)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(VERBOSE "Display verbose output." OFF)
if (VERBOSE)
  add_definitions(-DVERBOSE)
endif()

# Main Executables
add_executable(snapper snapper/main.cc)
add_executable(snappertests tests/main.cc)

# Snapper Library (libsnap)
add_library(snap
  lib/snapper.cc
  lib/archive.cc
  lib/backlink.cc)

# Linking
target_include_directories(snap PUBLIC ../include)

target_link_libraries(snapper PUBLIC snap)

target_link_libraries(snappertests PUBLIC snap)
