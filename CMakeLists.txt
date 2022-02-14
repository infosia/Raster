cmake_minimum_required(VERSION 3.17)

project(Raster)

set(EXE_NAME Raster)

option(RASTER_BUILD_EXECUTABLE "Build the project as an executable, rather than a library." ON)

find_package(OpenMP)

set(HEADER_FILES
  include/observer.h
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

if(RASTER_BUILD_EXECUTABLE)
  add_executable(${EXE_NAME} ${SRC_FILES} ${HEADER_FILES} src/main.cpp)
else()
  add_library(${PROJECT_NAME} ${SRC_FILES} ${HEADER_FILES})
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(${PROJECT_NAME} PRIVATE ${BUILD_INCLUDES})
target_precompile_headers(${PROJECT_NAME} PRIVATE include/pch.h)
target_link_libraries(${PROJECT_NAME} PRIVATE mikktspace)

if(OpenMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set_directory_properties(PROPERTIES VS_STARTUP_PROJECT ${PROJECT_NAME})
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
