cmake_minimum_required(VERSION 3.10)
project(new-match)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")
#set(CMAKE_C_COMPILER "/home/wef/Desktop/shutong/llvm-source-build/build/bin/clang")
#set(CMAKE_CXX_COMPILER "/home/wef/Desktop/shutong/llvm-source-build/build/bin/clang++")

set(LIBIPT_INCLUDE "" CACHE PATH "")
set(LIBIPT_LIBDIR "" CACHE PATH "")

include_directories(include)
add_subdirectory(src)

add_executable(decode src/scheduler/main.cpp)
target_link_libraries(decode
  matcher_lib
  decoder_lib
  structure_lib
  ipt
)