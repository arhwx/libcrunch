cmake_minimum_required(VERSION 3.20)
project(libcrunch)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(libcrunch src/libcrunch.cpp)
target_include_directories(libcrunch PRIVATE include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(libcrunch PRIVATE -Wall -Wextra -Wpedantic)
endif()
