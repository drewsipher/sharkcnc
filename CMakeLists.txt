cmake_minimum_required(VERSION 3.22)
project(sharkcnc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SHARKCNC_BUILD_APP "Build the Qt6 desktop application" ON)
option(SHARKCNC_BUILD_TESTS "Build unit tests" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# Clipper2: polygon boolean/offset engine for the CAM core
set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_USINGZ OFF CACHE BOOL "" FORCE)
FetchContent_Declare(clipper2
  GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
  GIT_TAG        Clipper2_1.5.4
  SOURCE_SUBDIR  CPP)
FetchContent_MakeAvailable(clipper2)

add_subdirectory(core)
add_subdirectory(tools)

if(SHARKCNC_BUILD_APP)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(Qt6_FOUND)
    add_subdirectory(app)
  else()
    message(WARNING "Qt6 not found - skipping desktop app (core/tools still build)")
  endif()
endif()

if(SHARKCNC_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2)
  FetchContent_MakeAvailable(catch2)
  add_subdirectory(tests)
endif()
