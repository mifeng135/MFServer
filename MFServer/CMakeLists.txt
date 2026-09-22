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

set(ALL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFAnnotationHandle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFConnectionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFFileWatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFIoPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaProfiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaProtoCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaServiceManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaShareData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFNetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFRedisClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFRedisConnectPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFShareTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFSqlConnectPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFTcpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFTcpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFUdpChannel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFUdpChannelManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFUdpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFValue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFWebServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaExtend.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFLuaSeri.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFMsgPack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MFpb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/cjson/fpconv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/cjson/lua_cjson.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/cjson/strbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/dmon/dmon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/kcp/ikcp.c
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Create executable
add_executable(MFServer ${ALL_SOURCES})

# Compiler options
target_compile_options(MFServer PRIVATE -Wno-unused-parameter)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
    target_compile_options(MFServer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-template-body>)
endif()

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

    find_library(CORE_FOUNDATION_LIB CoreFoundation)
    find_library(CORE_SERVICES_LIB CoreServices)

    set(_pq_hints ${CMAKE_CURRENT_SOURCE_DIR}/lib /opt/homebrew/opt/libpq /usr/local/opt/libpq)
    find_path(LIBPQ_INCLUDE_DIR libpq-fe.h HINTS /opt/homebrew/opt/libpq /usr/local/opt/libpq PATH_SUFFIXES include)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libpq.dylib")
        set(LIBPQ_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/lib/libpq.dylib")
    else()
        find_library(LIBPQ_LIBRARY pq HINTS ${_pq_hints} PATH_SUFFIXES lib)
    endif()
    if(NOT LIBPQ_LIBRARY)
        message(FATAL_ERROR "libpq not found. copy libpq.dylib into lib/ or brew install libpq")
    endif()

    target_include_directories(MFServer PRIVATE
        ${ZLIB_INCLUDE_DIRS}
        ${LIBPQ_INCLUDE_DIR}
    )
    # macOS drogon 默认关 OpenSSL / Brotli，只需链 libpq
    target_link_libraries(MFServer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/liblua.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libdrogon.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libtrantor.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjemalloc.2.dylib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjsoncpp.a
        ${LIBPQ_LIBRARY}
        ${ZLIB_LIBRARIES}
        ${CORE_FOUNDATION_LIB}
        ${CORE_SERVICES_LIB}
    )
elseif(WIN32)
    if(NOT MINGW)
        message(FATAL_ERROR "Windows 下只支持 MinGW-w64（MSYS2 MINGW64 环境）")
    endif()

    if(NOT MF_WIN_PREFIX)
        if(DEFINED ENV{MINGW_PREFIX})
            set(MF_WIN_PREFIX "$ENV{MINGW_PREFIX}")
        else()
            set(MF_WIN_PREFIX "D:/msys64/mingw64")
        endif()
    endif()
    message(STATUS "MinGW 依赖前缀: ${MF_WIN_PREFIX}")

    set(MF_WIN_LIBS "")
    foreach(_name lua drogon trantor)
        find_library(MF_LIB_${_name} NAMES ${_name} HINTS ${MF_WIN_PREFIX}/lib)
        if(NOT MF_LIB_${_name})
            message(FATAL_ERROR "未找到 ${_name}，请先在 MSYS2 MINGW64 shell 里执行 3rdback/build-mingw.sh")
        endif()
        list(APPEND MF_WIN_LIBS ${MF_LIB_${_name}})
    endforeach()

    file(GLOB _pg_dirs ${MF_WIN_PREFIX}/opt/pg-*)
    find_library(LIBPQ_LIBRARY NAMES pq HINTS ${_pg_dirs} ${MF_WIN_PREFIX} PATH_SUFFIXES lib)
    if(NOT LIBPQ_LIBRARY)
        message(FATAL_ERROR "未找到 libpq，请执行: pacman -S mingw-w64-x86_64-postgresql-16")
    endif()

    target_compile_options(MFServer PRIVATE -Wa,-mbig-obj)
    target_link_libraries(MFServer PRIVATE
        ${MF_WIN_LIBS}
        ${LIBPQ_LIBRARY}
        jsoncpp
        brotlienc
        brotlidec
        brotlicommon
        ssl
        crypto
        z
        cares
        atomic
        ws2_32
        rpcrt4
        crypt32
        iphlpapi
    )
else()
    target_link_libraries(MFServer PRIVATE
        liblua.so
        libdrogon.so
        libtrantor.so
        libjemalloc.so
        jsoncpp.so
        brotlienc
        brotlidec
        brotlicommon
        ssl
        crypto
        pq
        z
    )
endif()



if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(MFServer PRIVATE atomic)
endif()
