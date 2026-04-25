cmake_minimum_required(VERSION 3.20)
project(sysycc_cmake_ninja_multifile_smoke C)

add_library(demo STATIC add.c mul.c)
