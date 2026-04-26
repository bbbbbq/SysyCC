cmake_minimum_required(VERSION 3.20)
project(sysycc_north_star_real_project C)

add_library(northstar STATIC
    src/array.c
    src/stats.c
    src/sort.c
    src/search.c
    src/hash.c
    src/graph.c
    src/pipeline.c
)
target_include_directories(northstar PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(northstar PUBLIC NORTHSTAR_SCALE=3)
