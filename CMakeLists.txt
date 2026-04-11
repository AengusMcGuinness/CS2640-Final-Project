cmake_minimum_required(VERSION 3.16)

project(cs2640_final_project VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

find_package(Threads REQUIRED)

add_library(project_core
    src/kvstore/kv_store.cpp
    src/net/event_loop.cpp
    src/net/socket_utils.cpp
    src/protocol/text_protocol.cpp
)

target_include_directories(project_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(project_core
    PUBLIC
        Threads::Threads
)

add_executable(kv_server
    src/server/main.cpp
)

target_link_libraries(kv_server
    PRIVATE
        project_core
)

add_executable(kv_client
    src/client/main.cpp
)

target_link_libraries(kv_client
    PRIVATE
        project_core
)

add_executable(kvstore_tests
    tests/kv_store_test.cpp
)

target_link_libraries(kvstore_tests
    PRIVATE
        project_core
)

add_executable(protocol_tests
    tests/protocol_test.cpp
)

target_link_libraries(protocol_tests
    PRIVATE
        project_core
)

add_executable(kv_store_concurrency_test
    tests/kv_store_concurrency_test.cpp
)

target_link_libraries(kv_store_concurrency_test
    PRIVATE
        project_core
)

add_executable(kv_benchmark
    benchmarks/kv_benchmark.cpp
)

target_link_libraries(kv_benchmark
    PRIVATE
        project_core
)

if(BUILD_TESTING)
    add_test(NAME kvstore_tests COMMAND kvstore_tests)
    add_test(NAME protocol_tests COMMAND protocol_tests)
    add_test(NAME kv_store_concurrency_test COMMAND kv_store_concurrency_test)
endif()
