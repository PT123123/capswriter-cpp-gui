cmake_minimum_required(VERSION 3.16)
project(capswriter_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to the extracted sherpa-onnx prebuilt Windows package, which contains:
#   include/sherpa-onnx/c-api/c-api.h
#   lib/sherpa-onnx-c-api.lib
#   bin/sherpa-onnx-c-api.dll, onnxruntime.dll, ...
set(SHERPA_ONNX_ROOT "C:/sherpa-onnx" CACHE PATH
    "Root directory of the sherpa-onnx prebuilt package")

# Build the Qt tray frontend (capswriter-gui) in addition to the console
# client. Requires a Qt installation; point CMAKE_PREFIX_PATH at e.g.
# C:/Qt/6.8.3/msvc2022_64. Silently skipped when Qt6/Qt5 is not found.
option(CAPSWRITER_BUILD_GUI "Build the Qt tray GUI frontend" ON)

# ---------------------------------------------------------------------------
# Core library: everything except the two frontend mains.
# ---------------------------------------------------------------------------
add_library(capswriter_core STATIC
  src/config.cpp
  src/util.cpp
  src/audio_recorder.cpp
  src/asr_engine.cpp
  src/hotkey_listener.cpp
  src/text_injector.cpp
  src/text_transform.cpp
  src/dictation.cpp
  src/udp_io.cpp
  src/window_util.cpp
  src/instance_guard.cpp
)

target_include_directories(capswriter_core PUBLIC
  "${SHERPA_ONNX_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_library(SHERPA_ONNX_C_API_LIB
  NAMES sherpa-onnx-c-api
  PATHS "${SHERPA_ONNX_ROOT}/lib" "${SHERPA_ONNX_ROOT}/lib64" "${SHERPA_ONNX_ROOT}/bin"
  NO_DEFAULT_PATH
)
if(NOT SHERPA_ONNX_C_API_LIB)
  message(FATAL_ERROR
    "sherpa-onnx-c-api.lib not found under ${SHERPA_ONNX_ROOT}/lib. "
    "Download a sherpa-onnx Windows x64 shared release and set -DSHERPA_ONNX_ROOT=...")
endif()

target_link_libraries(capswriter_core PUBLIC
  ${SHERPA_ONNX_C_API_LIB} ole32 user32 ws2_32 uuid uiautomationcore)

if(MSVC)
  target_compile_definitions(capswriter_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
  target_compile_options(capswriter_core PUBLIC /utf-8 /W3)
endif()

# ---------------------------------------------------------------------------
# Console frontend (unchanged behaviour).
# ---------------------------------------------------------------------------
add_executable(capswriter src/main.cpp src/version.rc)
target_link_libraries(capswriter PRIVATE capswriter_core)

# ---------------------------------------------------------------------------
# Qt tray frontend.
# ---------------------------------------------------------------------------
if(CAPSWRITER_BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(Qt6_FOUND)
    set(CAPSWRITER_QT_LIB Qt6::Widgets)
  else()
    find_package(Qt5 5.15 QUIET COMPONENTS Widgets)
    if(Qt5_FOUND)
      set(CAPSWRITER_QT_LIB Qt5::Widgets)
    endif()
  endif()

  if(CAPSWRITER_QT_LIB)
    set(CMAKE_AUTOMOC ON)
    add_executable(capswriter-gui WIN32
      src/version.rc
      gui/main.cpp
      gui/main_window.h
      gui/main_window.cpp
      gui/hint_toast.h
      gui/hint_toast.cpp
      gui/settings_dialog.h
      gui/settings_dialog.cpp
      gui/tray_controller.h
      gui/tray_controller.cpp
      gui/app_icons.h
      gui/app_icons.cpp
    )
    # shell32:SetCurrentProcessExplicitAppUserModelID(任务栏图标分组)。
    target_link_libraries(capswriter-gui PRIVATE
      capswriter_core ${CAPSWRITER_QT_LIB} shell32)
    if(MSVC)
      target_compile_options(capswriter-gui PRIVATE /W3 /utf-8)
    endif()

    # Deploy the Qt runtime DLLs next to the exe when windeployqt is found.
    if(Qt6_FOUND AND TARGET Qt6::windeployqt)
      add_custom_command(TARGET capswriter-gui POST_BUILD
        COMMAND Qt6::windeployqt "$<TARGET_FILE:capswriter-gui>" --no-translations --no-system-d3d-compiler --no-opengl-sw
        COMMENT "Running windeployqt for capswriter-gui"
      )
    endif()
  else()
    message(STATUS "Qt not found (set CMAKE_PREFIX_PATH to your Qt kit); "
                   "skipping the capswriter-gui frontend")
  endif()
endif()

# ---------------------------------------------------------------------------
# Ship the runtime DLLs (sherpa-onnx-c-api.dll, onnxruntime.dll, ...) next
# to the exes. Recent releases place the DLLs under lib/, older under bin/.
# ---------------------------------------------------------------------------
file(GLOB SHERPA_DLLS "${SHERPA_ONNX_ROOT}/bin/*.dll" "${SHERPA_ONNX_ROOT}/lib/*.dll")
if(SHERPA_DLLS)
  foreach(target capswriter capswriter-gui)
    if(TARGET ${target})
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHERPA_DLLS} "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying sherpa-onnx runtime DLLs for ${target}"
      )
    endif()
  endforeach()
endif()
