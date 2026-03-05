cmake_minimum_required(VERSION 3.21)
project(auxlab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" AUXLAB2_VERSION_RAW)
string(STRIP "${AUXLAB2_VERSION_RAW}" AUXLAB2_VERSION)
if(AUXLAB2_VERSION STREQUAL "")
  set(AUXLAB2_VERSION "0.0.0")
endif()

file(READ "${CMAKE_CURRENT_LIST_DIR}/../aux_engine/VERSION" AUXE_VERSION_RAW)
string(STRIP "${AUXE_VERSION_RAW}" AUXE_VERSION)
if(AUXE_VERSION STREQUAL "")
  set(AUXE_VERSION "0.0.0")
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_VARIABLE AUXLAB2_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE AUXLAB2_GIT_HASH_RESULT
  ERROR_QUIET
)
if(NOT AUXLAB2_GIT_HASH_RESULT EQUAL 0 OR AUXLAB2_GIT_HASH STREQUAL "")
  set(AUXLAB2_GIT_HASH "unknown")
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../aux_engine"
  OUTPUT_VARIABLE AUXE_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE AUXE_GIT_HASH_RESULT
  ERROR_QUIET
)
if(NOT AUXE_GIT_HASH_RESULT EQUAL 0 OR AUXE_GIT_HASH STREQUAL "")
  set(AUXE_GIT_HASH "unknown")
endif()

string(TIMESTAMP AUXLAB2_BUILD_DATETIME "%Y-%m-%d %H:%M:%S")
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/src/BuildInfo.h.in"
  "${CMAKE_BINARY_DIR}/generated/BuildInfo.h"
  @ONLY
)

if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH
    "/opt/homebrew/opt/qt"
    "/usr/local/opt/qt"
    "/opt/homebrew/opt/fftw"
    "/usr/local/opt/fftw"
    "/opt/homebrew/opt/libsamplerate"
    "/usr/local/opt/libsamplerate"
  )
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Multimedia)
if(NOT Qt6_FOUND)
  message(FATAL_ERROR
    "Qt6 (Widgets, Multimedia) not found.\n"
    "Install Qt6 and re-run configure with one of:\n"
    "  cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt\n"
    "  cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local/opt/qt\n"
    "If not installed (Homebrew): brew install qt"
  )
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../aux_engine ${CMAKE_BINARY_DIR}/auxe_build)

add_executable(auxlab2
  src/main.cpp
  src/AuxEngineFacade.h
  src/AuxEngineFacade.cpp
  src/CommandConsole.h
  src/CommandConsole.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/SignalGraphWindow.h
  src/SignalGraphWindow.cpp
  src/DebugCodeEditor.h
  src/DebugCodeEditor.cpp
  src/SignalTableWindow.h
  src/SignalTableWindow.cpp
  src/TextObjectWindow.h
  src/TextObjectWindow.cpp
  src/BinaryObjectWindow.h
  src/BinaryObjectWindow.cpp
  src/UdfDebugWindow.h
  src/UdfDebugWindow.cpp
)

set_target_properties(auxlab2 PROPERTIES
  VERSION "${AUXLAB2_VERSION}"
)

target_include_directories(auxlab2 PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(auxlab2 PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  auxe
)

if(MSVC)
  target_compile_definitions(auxlab2 PRIVATE _HAS_STD_BYTE=0)
endif()
