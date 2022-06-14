CMAKE_MINIMUM_REQUIRED(VERSION 3.7)
PROJECT (szkarc VERSION 1.6.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_EXTENSIONS OFF)

FIND_PACKAGE(Threads REQUIRED)

SET(ZLIB_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/zlib-ng;${CMAKE_SOURCE_DIR}/deps/build/zlib-ng")
IF (WIN32)
SET(ZLIB_LIBRARY_DEBUG "${CMAKE_SOURCE_DIR}/deps/build/zlib-ng/Debug/zlibstaticd.lib")
SET(ZLIB_LIBRARY_RELEASE "${CMAKE_SOURCE_DIR}/deps/build/zlib-ng/Release/zlibstatic.lib")
ELSE()
SET(ZLIB_LIBRARY_DEBUG "${CMAKE_SOURCE_DIR}/deps/build/zlib-ng/libz.a")
SET(ZLIB_LIBRARY_RELEASE "${CMAKE_SOURCE_DIR}/deps/build/zlib-ng/libz.a")
ENDIF()

IF (WIN32)
SET (CMAKE_CXX_FLAGS "/utf-8") # for indicators, which contains utf-8 characters.
ENDIF()

SET(MZ_FETCH_LIBS OFF CACHE BOOL "NO FETCHING")
SET(ZLIB_COMPAT ON CACHE BOOL "COMPAT")
SET(MZ_COMPAT OFF CACHE BOOL "Disable")
#SET(MZ_ZLIB OFF CACHE BOOL "Disable")
SET(MZ_BZIP2 OFF CACHE BOOL "Disable")
SET(MZ_LZMA OFF CACHE BOOL "Disable")
SET(MZ_ZSTD OFF CACHE BOOL "Disable")
SET(MZ_PKCRYPT OFF CACHE BOOL "Disable")
SET(MZ_WZAES OFF CACHE BOOL "Disable")
SET(MZ_OPENSSL OFF CACHE BOOL "Disable")
SET(MZ_LIBBSD OFF CACHE BOOL "Disable")
SET(MZ_ICONV OFF CACHE BOOL "Disable")
SET(MZ_SIGNING OFF CACHE BOOL "Disable")
add_subdirectory(deps/minizip-ng)

include_directories(deps/tclap/include)
include_directories(deps/indicators/include)

configure_file(config.h.in config.h @ONLY)
include_directories("${PROJECT_BINARY_DIR}") # for config.h
add_library(szkarc szkarc.h szkarc.cpp)
TARGET_LINK_LIBRARIES(szkarc minizip)

ADD_EXECUTABLE(zipdirs main.cpp)
TARGET_LINK_LIBRARIES(zipdirs szkarc minizip Threads::Threads)
ADD_EXECUTABLE(unzipdirs unzipdirs.cpp)
TARGET_LINK_LIBRARIES(unzipdirs szkarc minizip Threads::Threads)
ADD_EXECUTABLE(deldirs deldirs.cpp)
TARGET_LINK_LIBRARIES(deldirs szkarc)

enable_testing()

add_test(NAME test_deldirs COMMAND $<TARGET_FILE:deldirs> WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/tests)
