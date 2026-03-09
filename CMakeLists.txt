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
if(WIN32)
   if(DEFINE_QT_ROOT)
      list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
   endif()
endif()

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

option(AUXLAB2_WIN32_GUI "Build Windows GUI subsystem app (no console)" ON)

set(AUXLAB2_SOURCES
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
  src/StructMembersWindow.h
  src/StructMembersWindow.cpp
  src/CellMembersWindow.h
  src/CellMembersWindow.cpp
  src/TextObjectWindow.h
  src/TextObjectWindow.cpp
  src/BinaryObjectWindow.h
  src/BinaryObjectWindow.cpp
  src/UdfDebugWindow.h
  src/UdfDebugWindow.cpp
)

if(WIN32 AND AUXLAB2_WIN32_GUI)
    add_executable(auxlab2 WIN32 ${AUXLAB2_SOURCES})
else()
    add_executable(auxlab2 ${AUXLAB2_SOURCES})
endif()

set_target_properties(auxlab2 PROPERTIES
  VERSION "${AUXLAB2_VERSION}"
)

if(APPLE)
  set_target_properties(auxlab2 PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "auxlab2"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.auxlab2.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${AUXLAB2_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AUXLAB2_VERSION}"
  )

  set(AUXLAB2_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab2.icns")
  set(AUXLAB2_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.png")
  if(EXISTS "${AUXLAB2_ICON_ICNS}")
    target_sources(auxlab2 PRIVATE "${AUXLAB2_ICON_ICNS}")
    set_source_files_properties("${AUXLAB2_ICON_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(auxlab2 PROPERTIES MACOSX_BUNDLE_ICON_FILE "auxlab2.icns")
  elseif(EXISTS "${AUXLAB2_ICON_PNG}")
    target_sources(auxlab2 PRIVATE "${AUXLAB2_ICON_PNG}")
    set_source_files_properties("${AUXLAB2_ICON_PNG}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(auxlab2 PROPERTIES MACOSX_BUNDLE_ICON_FILE "auxlab-icon.png")
    message(WARNING
      "Using resources/icons/auxlab-icon.png as bundle icon. "
      "If Finder still shows default icon, generate resources/icons/auxlab2.icns."
    )
  else()
    message(WARNING
      "No app icon found at resources/icons/auxlab2.icns or resources/icons/auxlab-icon.png. "
      "Build will still produce auxlab2.app, but with default icon."
    )
  endif()

  # Keep a top-level launcher in the build dir aligned with the real bundle executable.
  # This avoids stale ./build/auxlab2 pointing at an older binary.
  add_custom_command(TARGET auxlab2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_BINARY_DIR}/auxlab2"
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "$<TARGET_FILE:auxlab2>" "${CMAKE_BINARY_DIR}/auxlab2"
    VERBATIM
  )
endif()

target_include_directories(auxlab2 PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(auxlab2 PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  auxe
)

install(TARGETS auxlab2
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)

install(FILES $<TARGET_FILE:auxe>
  DESTINATION bin
)
