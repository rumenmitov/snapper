cmake_minimum_required(VERSION 3.10)
project(Snapper)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


add_executable(snapper snapper/main.cc)
add_executable(snappertests snapper/tests.cc)

add_library(snap snapper/snapper.cc)
add_library(snaptests snapper/snapper.cc)

target_link_libraries(snapper PUBLIC snap)
target_include_directories(snapper PUBLIC "${PROJECT_SOURCE_DIR}/snapper/include")

target_link_libraries(snappertests PUBLIC snap)
target_include_directories(snappertests PUBLIC "${PROJECT_SOURCE_DIR}/snapper/include")

