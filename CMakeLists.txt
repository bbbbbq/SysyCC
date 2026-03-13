cmake_minimum_required(VERSION 3.16)

project(SysyCC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FLEX REQUIRED)

set(GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend)

flex_target(SysyLexer
    src/frontend/lexer.l
    ${GENERATED_DIR}/lexer_scanner.cpp
    DEFINES_FILE ${GENERATED_DIR}/lexer_scanner.hpp
    OPTIONS --warn --noline
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/frontend/lexer_driver.cpp
    ${FLEX_SysyLexer_OUTPUTS}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend
)
