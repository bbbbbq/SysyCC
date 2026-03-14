cmake_minimum_required(VERSION 3.16)

project(SysyCC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(GRAMMER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/grammer)

bison_target(
    SysyParser
    ${GRAMMER_DIR}/parser.y
    ${GRAMMER_DIR}/parser.cpp
    DEFINES_FILE ${GRAMMER_DIR}/parser.tab.h
)

flex_target(
    SysyLexer
    ${GRAMMER_DIR}/lexer.l
    ${GRAMMER_DIR}/lexer.cpp
    OPTIONS --warn
)

add_flex_bison_dependency(SysyLexer SysyParser)

add_executable(${PROJECT_NAME}
    ${BISON_SysyParser_OUTPUTS}
    ${FLEX_SysyLexer_OUTPUTS}
    src/cli/cli.cpp
    src/compiler/complier.cpp
    src/compiler/pass/pass.cpp
    src/frontend/driver/lexer_driver.cpp
    src/frontend/driver/parser_driver.cpp
    src/frontend/driver/parser_runtime.cpp
    src/main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
