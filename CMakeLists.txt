cmake_minimum_required(VERSION 3.25)

project(
    ModernCppRoomServer
    VERSION 0.1.0
    DESCRIPTION "A measured, authoritative room server built with modern C++"
    LANGUAGES CXX)

option(MCRS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(mcrs_project_options INTERFACE)
target_compile_features(mcrs_project_options INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(
        mcrs_project_options
        INTERFACE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<BOOL:${MCRS_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(
        mcrs_project_options
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            $<$<BOOL:${MCRS_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(mcrs_protocol src/protocol/packet_codec.cpp)
target_include_directories(mcrs_protocol PUBLIC include)
target_link_libraries(mcrs_protocol PUBLIC mcrs_project_options)

add_executable(mcrs_server src/main.cpp)
target_link_libraries(mcrs_server PRIVATE mcrs_protocol)

include(CTest)

if(BUILD_TESTING)
    add_executable(mcrs_protocol_tests tests/packet_codec_tests.cpp)
    target_link_libraries(mcrs_protocol_tests PRIVATE mcrs_protocol)
    add_test(NAME protocol.codec COMMAND mcrs_protocol_tests)
endif()
