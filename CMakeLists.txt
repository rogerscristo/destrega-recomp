cmake_minimum_required(VERSION 3.20)
project(destrega_recomp C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GEN_DIR ${CMAKE_SOURCE_DIR}/gen)

# Regenerate C from the disc image whenever the recompiler or analysis changes.
file(GLOB RECOMP_PY ${CMAKE_SOURCE_DIR}/recomp/*.py)
file(GLOB ANALYSIS_JSON ${CMAKE_SOURCE_DIR}/analysis/*.json ${CMAKE_SOURCE_DIR}/analysis/overlays/*.json)
if(NOT EXISTS ${GEN_DIR}/files.cmake)
  execute_process(COMMAND ${Python3_EXECUTABLE} recomp/recomp.py --out gen
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
include(${GEN_DIR}/files.cmake)
add_custom_command(
  OUTPUT ${GEN_SOURCES} ${GEN_DIR}/gen_protos.h
  COMMAND ${Python3_EXECUTABLE} recomp/recomp.py --out gen
  DEPENDS ${RECOMP_PY} ${ANALYSIS_JSON} ${CMAKE_SOURCE_DIR}/analysis/extra_seeds.txt
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Recompiling MIPS code to C")

add_library(gen STATIC ${GEN_SOURCES})
target_include_directories(gen PRIVATE runtime gen)
# Generated code is huge and goto-heavy; -O1 keeps build times sane.
target_compile_options(gen PRIVATE -O1 -w -fno-strict-aliasing)

add_executable(destrega
  runtime/main.c runtime/rt.c runtime/bus.c runtime/bios.c runtime/interp.c
  runtime/cdrom.c runtime/disc.c runtime/sio.c runtime/gpu.c runtime/gte.c runtime/spu.c runtime/xa.c runtime/mdec.c)
target_include_directories(destrega PRIVATE runtime gen)
target_compile_options(destrega PRIVATE -Wall -Wno-unused-function -fno-strict-aliasing)
target_link_libraries(destrega PRIVATE gen SDL3::SDL3 ZLIB::ZLIB m)
