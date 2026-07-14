cmake_minimum_required(VERSION 3.20)
project(libcrunch)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(libcrunch src/libcrunch.cpp src/frame.cpp src/decode.cpp)

target_compile_features(libcrunch PUBLIC cxx_std_17)
target_include_directories(libcrunch PUBLIC include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(libcrunch PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
foreach(suite bitstream frame xxhash decode)
  add_executable(${suite}_tests tests/${suite}_tests.cpp)
  target_link_libraries(${suite}_tests PRIVATE libcrunch)
endforeach()
add_test(NAME bitstream COMMAND bitstream_tests)
add_test(NAME frame COMMAND frame_tests ${CMAKE_SOURCE_DIR}/tests/data)
add_test(NAME xxhash COMMAND xxhash_tests ${CMAKE_SOURCE_DIR}/tests/data)
add_test(NAME decode COMMAND decode_tests ${CMAKE_SOURCE_DIR}/tests/data)
