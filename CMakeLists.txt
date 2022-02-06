cmake_minimum_required(VERSION 3.17)

project(Raster)

set(EXE_NAME Raster)

find_package(OpenMP)

set(HEADER_FILES
  include/json_func.inl
  include/renderer/render.h
  include/renderer/loader.h
  include/renderer/scene.h
  include/shaders/shader.h
)

set(SRC_FILES
  src/renderer/render.cpp
  src/renderer/loader.cpp
  src/renderer/scene.cpp
  src/main.cpp
)

set (BUILD_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/CLI11
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/cgltf
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/glm
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/ghc
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/nlohmann
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/mikktspace
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/VRMC
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/mikktspace)

add_executable(${EXE_NAME} ${SRC_FILES} ${HEADER_FILES})
set_property(TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11)
set_property(TARGET ${EXE_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(${EXE_NAME} PRIVATE ${BUILD_INCLUDES})
target_precompile_headers(${EXE_NAME} PRIVATE include/pch.h)
target_link_libraries(${EXE_NAME} PRIVATE mikktspace)

if(OpenMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set_directory_properties(PROPERTIES VS_STARTUP_PROJECT ${EXE_NAME})
else()
    target_compile_options(${EXE_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS ${EXE_NAME} RUNTIME DESTINATION bin)
