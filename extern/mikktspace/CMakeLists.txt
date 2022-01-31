cmake_minimum_required(VERSION 3.17)
project(renderer LANGUAGES C)

set(SOURCES
  mikktspace.h
  mikktspace.c
)

set(TARGET mikktspace)
add_library(${TARGET} ${SOURCES})

set_target_properties(${TARGET} PROPERTIES C_STANDARD 90)
set_target_properties(${TARGET} PROPERTIES C_EXTENSIONS OFF)
set_target_properties(${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
