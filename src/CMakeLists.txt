cmake_minimum_required(VERSION 3.10)
project(Snapper)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(snapper snapper/main.cc)

add_library(snap snapper/snapper.cc)

target_link_libraries(snapper PUBLIC snap)
target_include_directories(snapper PUBLIC "${PROJECT_SOURCE_DIR}/snapper/include")
