cmake_minimum_required(VERSION 3.7)
project(deps VERSION 0.2.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_EXTENSIONS OFF)

SET(ZLIB_COMPAT ON)
SET(ZLIB_ENABLE_TESTS OFF)
add_subdirectory(zlib-ng)

add_subdirectory(spdlog)