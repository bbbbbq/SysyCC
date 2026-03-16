cmake_minimum_required(VERSION 3.16)

project(SysyCC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(LEXER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer)
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser)

bison_target(
    SysyParser
    ${PARSER_DIR}/parser.y
    ${PARSER_DIR}/parser_generated.cpp
    DEFINES_FILE ${PARSER_DIR}/parser.tab.h
)

flex_target(
    SysyLexer
    ${LEXER_DIR}/lexer.l
    ${LEXER_DIR}/lexer_scanner.cpp
    OPTIONS --warn
)

add_flex_bison_dependency(SysyLexer SysyParser)

add_executable(${PROJECT_NAME}
    ${BISON_SysyParser_OUTPUTS}
    ${FLEX_SysyLexer_OUTPUTS}
    src/cli/cli.cpp
    src/compiler/complier.cpp
    src/compiler/pass/pass.cpp
    src/frontend/lexer/lexer.cpp
    src/frontend/parser/parser.cpp
    src/frontend/parser/parser_runtime.cpp
    src/frontend/preprocess/preprocess.cpp
    src/main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
