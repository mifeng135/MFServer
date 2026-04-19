cmake_minimum_required(VERSION 3.12)
project(MFServer VERSION 1.0.0 LANGUAGES CXX C)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "===========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")

# Compiler-specific options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra -fno-omit-frame-pointer)
    add_compile_definitions(DEBUG)
else()
    add_compile_options(-O2 -DNDEBUG)
endif()

# Collect source files
# Note: file(GLOB) is used for convenience, but explicit file listing is preferred for production
file(GLOB_RECURSE CORE_CPP_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
)
file(GLOB CJSON_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/cjson/*.c"
)

file(GLOB DMON_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/dmon/*.c"
)

file(GLOB CORE_C_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.c"
)

set(ALL_SOURCES
    ${CORE_CPP_SOURCES}
    ${CJSON_SOURCES}
    ${DMON_SOURCES}
    ${CORE_C_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/3rd/kcp/ikcp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Create executable
add_executable(MFServer ${ALL_SOURCES})

# Compiler options
target_compile_options(MFServer PRIVATE -Wno-unused-parameter)

# Include directories (consolidated)
target_include_directories(MFServer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/cjson
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/kcp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/lua
)
# Link libraries
if(APPLE)
    find_package(ZLIB REQUIRED)
    target_include_directories(MFServer PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(MFServer PRIVATE ${ZLIB_LIBRARIES})

    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    target_link_libraries(MFServer PRIVATE ${CORE_FOUNDATION_LIB})

    find_library(CORE_SERVICES_LIB CoreServices)
    target_link_libraries(MFServer PRIVATE ${CORE_SERVICES_LIB})

    target_link_libraries(MFServer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/liblua.a
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/libdrogon.a
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/libtrantor.a
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjemalloc.2.dylib
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjsoncpp.a
    )
else()
    target_link_libraries(MFServer PRIVATE liblua.so libdrogon.so libtrantor.so libjemalloc.so jsoncpp.so)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(MFServer PRIVATE atomic)
endif()
