cmake_minimum_required(VERSION 3.21)
project(auxlab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

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
  src/SignalTableWindow.h
  src/SignalTableWindow.cpp
  src/UdfDebugWindow.h
  src/UdfDebugWindow.cpp
)

target_include_directories(auxlab2 PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(auxlab2 PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  auxe
)

if(MSVC)
  target_compile_definitions(auxlab2 PRIVATE _HAS_STD_BYTE=0)
endif()
