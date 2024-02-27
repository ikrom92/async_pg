cmake_minimum_required(VERSION 3.10)
set(CMAKE_DEBUG_POSTFIX d)

project(async_pg)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread")
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(libpq/include)
link_directories(libpq/lib)

file(GLOB SOURCES "src/*.cpp" "src/**/*.cpp")

add_executable(async_pg ${SOURCES})
set_target_properties(async_pg PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
target_link_libraries(async_pg "-lstdc++fs" "-lpq")
