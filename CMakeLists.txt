cmake_minimum_required(VERSION 3.21)
project(auxlab2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(AUXLAB2_ENABLE_QT_DEPLOYMENT "Deploy Qt runtime pieces during install/package steps" ON)
option(AUXLAB2_ENABLE_CPACK "Enable CPack package generation" ON)
option(AUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES "Enable DEB/RPM generators in addition to TGZ on Linux" OFF)
option(AUXLAB2_ENABLE_WINDOWS_NSIS "Enable NSIS installer generation on Windows" OFF)

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

if(APPLE)
  include("${Qt6_DIR}/../Qt6Core/Qt6QDarwinMicrophonePermissionPluginConfig.cmake" OPTIONAL)
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

find_program(AUXLAB2_MACDEPLOYQT_EXECUTABLE
  NAMES macdeployqt
  HINTS
    "${QT_ROOT}/bin"
    "/opt/homebrew/opt/qt/bin"
    "/usr/local/opt/qt/bin"
)
find_program(AUXLAB2_CODESIGN_EXECUTABLE
  NAMES codesign
  HINTS
    "/usr/bin"
)
find_program(AUXLAB2_WINDEPLOYQT_EXECUTABLE
  NAMES windeployqt
  HINTS
    "${QT_ROOT}/bin"
)

set(AUXLAB2_MACOS_QT_PLATFORM_PLUGIN_CANDIDATES
  "/opt/homebrew/Cellar/qtbase/6.10.1/share/qt/plugins/platforms/libqcocoa.dylib"
  "/opt/homebrew/opt/qtbase/share/qt/plugins/platforms/libqcocoa.dylib"
  "/usr/local/opt/qt/share/qt/plugins/platforms/libqcocoa.dylib"
)
set(AUXLAB2_MACOS_QT_MULTIMEDIA_PLUGIN_CANDIDATES
  "/opt/homebrew/Cellar/qtmultimedia/6.10.1/share/qt/plugins/multimedia/libdarwinmediaplugin.dylib"
  "/opt/homebrew/opt/qtmultimedia/share/qt/plugins/multimedia/libdarwinmediaplugin.dylib"
  "/usr/local/opt/qtmultimedia/share/qt/plugins/multimedia/libdarwinmediaplugin.dylib"
)

set(AUXLAB2_MACOS_QT_PLATFORM_PLUGIN "")
foreach(_candidate IN LISTS AUXLAB2_MACOS_QT_PLATFORM_PLUGIN_CANDIDATES)
  if(EXISTS "${_candidate}")
    set(AUXLAB2_MACOS_QT_PLATFORM_PLUGIN "${_candidate}")
    break()
  endif()
endforeach()

set(AUXLAB2_MACOS_QT_MULTIMEDIA_PLUGIN "")
foreach(_candidate IN LISTS AUXLAB2_MACOS_QT_MULTIMEDIA_PLUGIN_CANDIDATES)
  if(EXISTS "${_candidate}")
    set(AUXLAB2_MACOS_QT_MULTIMEDIA_PLUGIN "${_candidate}")
    break()
  endif()
endforeach()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../aux_engine ${CMAKE_BINARY_DIR}/auxe_build)

option(AUXLAB2_WIN32_GUI "Build Windows GUI subsystem app (no console)" ON)

set(AUXLAB2_SOURCES
  src/main.cpp
  src/AuxEngineFacade.h
  src/AuxEngineFacade.cpp
  src/GraphicsObjects.h
  src/GraphicsObjects.cpp
  src/GraphicsManager.h
  src/GraphicsManager.cpp
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

if(WIN32)
  set(AUXLAB2_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.ico")
  set(AUXLAB2_WINDOWS_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/auxlab2.rc")
  if(EXISTS "${AUXLAB2_ICON_ICO}" AND EXISTS "${AUXLAB2_WINDOWS_RC}")
    list(APPEND AUXLAB2_SOURCES "${AUXLAB2_WINDOWS_RC}")
  else()
    message(WARNING
      "Windows app icon resources are missing. Expected resources/icons/auxlab-icon.ico "
      "and resources/windows/auxlab2.rc."
    )
  endif()
endif()

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
    INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/../lib"
    BUILD_WITH_INSTALL_RPATH OFF
  )
elseif(UNIX AND NOT WIN32)
  set_target_properties(auxlab2 PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN"
    BUILD_WITH_INSTALL_RPATH OFF
  )
endif()

if(APPLE)
  set_target_properties(auxlab2 PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "auxlab2"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.auxlab2.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${AUXLAB2_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AUXLAB2_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in"
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

if(APPLE AND TARGET Qt6::QDarwinMicrophonePermissionPlugin)
  set_target_properties(auxlab2 PROPERTIES
    _qt_has_QDarwinMicrophonePermissionPlugin_usage_description TRUE
  )
  target_link_libraries(auxlab2 PRIVATE
    Qt6::QDarwinMicrophonePermissionPlugin
    Qt6::QDarwinMicrophonePermissionPlugin_init
  )
endif()

install(TARGETS auxlab2
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/TUTORIAL_GUI_AND_DEBUGGING.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/RELEASE.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/auxlab2"
)

if(WIN32)
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.ico"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/auxlab2"
    OPTIONAL
  )
endif()

if(UNIX AND NOT APPLE)
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
    RENAME "auxlab2.png"
    OPTIONAL
  )
endif()

set(AUXLAB2_DEPLOY_SEARCH_DIRS
  "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
  "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
  "${QT_ROOT}"
  "${Qt6_DIR}"
  "/opt/homebrew/lib"
  "/opt/homebrew"
  "/opt/homebrew/opt/qtbase/lib"
  "/opt/homebrew/opt/qtmultimedia/lib"
  "/opt/homebrew/opt/qtpdf/lib"
  "/opt/homebrew/opt/qtsvg/lib"
  "/opt/homebrew/opt/qtvirtualkeyboard/lib"
  "/opt/homebrew/opt/brotli/lib"
  "/opt/homebrew/opt/zstd/lib"
  "/opt/homebrew/opt/md4c/lib"
  "/opt/homebrew/opt/graphite2/lib"
  "/opt/homebrew/opt/dbus/lib"
  "/usr/local/lib"
  "/usr/local"
)
if(DEFINED ENV{VCPKG_ROOT})
  list(APPEND AUXLAB2_DEPLOY_SEARCH_DIRS
    "$ENV{VCPKG_ROOT}/installed/x64-windows/bin"
    "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin"
    "$ENV{VCPKG_ROOT}/installed/x64-linux/lib"
    "$ENV{VCPKG_ROOT}/installed/x64-osx/lib"
  )
endif()
list(REMOVE_DUPLICATES AUXLAB2_DEPLOY_SEARCH_DIRS)
if(AUXLAB2_ENABLE_QT_DEPLOYMENT)
  if(APPLE AND (NOT AUXLAB2_MACDEPLOYQT_EXECUTABLE OR NOT AUXLAB2_MACOS_QT_PLATFORM_PLUGIN))
    message(WARNING
      "macOS deployment needs macdeployqt and libqcocoa.dylib. Packaging will be incomplete until both are available."
    )
  elseif(WIN32 AND NOT AUXLAB2_WINDEPLOYQT_EXECUTABLE)
    message(WARNING
      "windeployqt not found. Windows packaging will rely on runtime DLL scanning only and may "
      "need manual Qt plugin deployment on release machines."
    )
  endif()
endif()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Auxlab2RuntimeDeploy.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Auxlab2RuntimeDeploy.cmake"
  @ONLY
)

install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/Auxlab2RuntimeDeploy.cmake")

if(WIN32)
  add_custom_command(TARGET auxlab2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:auxlab2> $<TARGET_FILE_DIR:auxlab2>
    COMMAND_EXPAND_LISTS
  )
endif()

if(AUXLAB2_ENABLE_CPACK)
  set(CPACK_PACKAGE_NAME "auxlab2")
  set(CPACK_PACKAGE_VENDOR "auxlab")
  set(CPACK_PACKAGE_CONTACT "release@auxlab.invalid")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt GUI workbench and debugger for AUX Engine")
  set(CPACK_PACKAGE_VERSION "${AUXLAB2_VERSION}")
  set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

  if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    if(AUXLAB2_ENABLE_WINDOWS_NSIS)
      list(APPEND CPACK_GENERATOR "NSIS")
    endif()
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "auxlab2")
    set(CPACK_NSIS_DISPLAY_NAME "auxlab2")
    set(CPACK_NSIS_PACKAGE_NAME "auxlab2")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/auxlab-icon.ico")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_EXECUTABLES "auxlab2;auxlab2")
  elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
  else()
    set(CPACK_GENERATOR "TGZ")
    if(AUXLAB2_ENABLE_NATIVE_LINUX_PACKAGES)
      list(APPEND CPACK_GENERATOR "DEB" "RPM")
    endif()
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
  endif()

  include(CPack)
endif()
