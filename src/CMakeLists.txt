cmake_minimum_required(VERSION 3.10)
project(Snapper)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(VERBOSE "Display verbose output." OFF)
if (VERBOSE)
  add_definitions(-DVERBOSE)
endif()

if(NOT DEFINED ENV{TESTS})
    set(ENV{TESTS} 1000)
endif()
add_definitions(-DTESTS=$ENV{TESTS})

# Main Executables
add_executable(snapper snapper/main.cc)
add_executable(snappertests test/snapper/main.cc)

# Snapper Library (libsnap)
add_library(snap
  lib/snapper.cc
  lib/archive.cc
  lib/backlink.cc
  lib/utils.cc)

# Linking
target_include_directories(snap PUBLIC ../include)

target_link_libraries(snapper PUBLIC snap)

target_link_libraries(snappertests PUBLIC snap)
