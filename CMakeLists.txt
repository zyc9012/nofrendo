cmake_minimum_required(VERSION 3.0)

project(nofrendo)

if (NOT MSVC)
  set(CMAKE_C_FLAGS "-O3 -Wno-unused-result")
  set(CMAKE_CXX_FLAGS "-O3 -Wno-unused-result --std=c++11")
  if (NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "-static -s")
  endif()
endif()

if (MSVC AND WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:CONSOLE")
endif()

include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/cpu)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/libsnss)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/mappers)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/nes)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/sdl)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/sndhrdw)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src)

file(GLOB_RECURSE SRCS RELATIVE ${CMAKE_SOURCE_DIR} "src/*.c")

add_executable(nofrendo ${SRCS})

find_package(SDL REQUIRED)
include_directories(${SDL_INCLUDE_DIRS})
target_link_libraries(nofrendo SDL::SDL)
