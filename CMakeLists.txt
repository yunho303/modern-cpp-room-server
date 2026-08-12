cmake_minimum_required(VERSION 3.25)

project(
    ModernCppRoomServer
    VERSION 0.1.0
    DESCRIPTION "A measured, authoritative room server built with modern C++"
    LANGUAGES CXX)

option(MCRS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

include(FetchContent)

FetchContent_Declare(
    asio
    URL https://github.com/chriskohlhoff/asio/archive/366dfc44640182cb21c1ebf7efb658a6bec13f5a.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR mcrs-no-cmake-subdirectory
)
FetchContent_MakeAvailable(asio)

find_package(Threads REQUIRED)

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

add_library(mcrs_asio INTERFACE)
target_include_directories(mcrs_asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(mcrs_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(mcrs_asio INTERFACE Threads::Threads)

if(WIN32)
    target_compile_definitions(mcrs_asio INTERFACE _WIN32_WINNT=0x0A00)
endif()

add_library(
    mcrs_protocol
    src/protocol/packet_codec.cpp
    include/mcrs/protocol/packet_codec.hpp
)
target_include_directories(mcrs_protocol PUBLIC include)
target_link_libraries(mcrs_protocol PUBLIC mcrs_project_options)

add_library(
    mcrs_network
    src/network/receive_buffer.cpp
    src/network/server.cpp
    src/network/session.cpp
    include/mcrs/network/receive_buffer.hpp
    include/mcrs/network/server.hpp
    include/mcrs/network/session.hpp
)
target_include_directories(mcrs_network PUBLIC include)
target_link_libraries(mcrs_network PUBLIC mcrs_asio mcrs_project_options PRIVATE mcrs_protocol)

add_executable(mcrs_server src/main.cpp)
target_link_libraries(mcrs_server PRIVATE mcrs_network mcrs_protocol)

include(CTest)

if(BUILD_TESTING)
    add_executable(mcrs_protocol_tests tests/packet_codec_tests.cpp)
    target_link_libraries(mcrs_protocol_tests PRIVATE mcrs_protocol)
    add_test(NAME protocol.codec COMMAND mcrs_protocol_tests)

    add_executable(mcrs_receive_buffer_tests tests/receive_buffer_tests.cpp)
    target_link_libraries(mcrs_receive_buffer_tests PRIVATE mcrs_network mcrs_protocol)
    add_test(NAME network.receive_buffer COMMAND mcrs_receive_buffer_tests)

    add_executable(mcrs_session_tests tests/session_tests.cpp)
    target_link_libraries(mcrs_session_tests PRIVATE mcrs_network mcrs_protocol)
    add_test(NAME network.session COMMAND mcrs_session_tests)
    set_tests_properties(network.session PROPERTIES TIMEOUT 10)
endif()
