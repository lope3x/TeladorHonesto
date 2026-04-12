cmake_minimum_required(VERSION 3.10)
project(TeladorHonesto)

set(CMAKE_CXX_STANDARD 20)

add_executable(TeladorHonesto WIN32 main.cpp)

target_link_libraries(TeladorHonesto comctl32 winmm)