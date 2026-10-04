cmake_minimum_required(VERSION 3.20)
# Version shown in the exe's file properties. Releases pass the tag, e.g.
#   cmake -DFACESTUDIO_VERSION=1.2.3 ...        (a "-rc1" style suffix may be given via FACESTUDIO_VERSION_LABEL)
set(FACESTUDIO_VERSION "1.0.0" CACHE STRING "Numeric major.minor.patch for the Windows version resource")
set(FACESTUDIO_VERSION_LABEL "" CACHE STRING "Display version, e.g. 1.2.3-rc1 (defaults to FACESTUDIO_VERSION)")
if(NOT FACESTUDIO_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "FACESTUDIO_VERSION must look like 1.2.3 (got '${FACESTUDIO_VERSION}')")
endif()
project(TomTomFaceStudio VERSION ${FACESTUDIO_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FACESTUDIO_BUILD_TESTS "Build the unit tests and the ttface-tool CLI" ON)
option(FACESTUDIO_BUILD_APP "Build the Win32 GUI application (Windows only)" ON)

# ---------------------------------------------------------------------------
# Platform-neutral core: JSON, ZIP/deflate, PNG, package formats, gallery,
# rule engine, renderer and the USB device client. No third-party code.
# ---------------------------------------------------------------------------
add_library(facestudio_core STATIC
  src/core/json.cpp
  src/core/codec.cpp
  src/core/deflate.cpp
  src/core/zip.cpp
  src/core/image.cpp
  src/core/png.cpp
  src/core/font.cpp
  src/core/model.cpp
  src/core/render.cpp
  src/core/canvas.cpp
  src/core/fileio.cpp
  src/core/package.cpp
  src/core/gallery.cpp
  src/core/device_client.cpp
  src/core/notify.cpp
  src/core/capture.cpp
  src/core/lrc.cpp
  src/core/webhook.cpp)
target_include_directories(facestudio_core PUBLIC src/core)

if(MSVC)
  target_compile_options(facestudio_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
  target_compile_definitions(facestudio_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(facestudio_core PUBLIC -Wall -Wextra)
endif()
find_package(Threads REQUIRED)
target_link_libraries(facestudio_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(facestudio_core PUBLIC ws2_32 bcrypt)
endif()

# Small, dependency-free binaries: static runtime, dead-code elimination.
function(facestudio_shrink target)
  if(MSVC)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # /MT, no VC++ redistributable
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/O1> $<$<CONFIG:Release>:/GL> /Gy /Gw)
    target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
  else()
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-Os> -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    if(WIN32)
      target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
    endif()
  endif()
endfunction()
if(MSVC)
  set_property(TARGET facestudio_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(facestudio_core PRIVATE $<$<CONFIG:Release>:/O1> $<$<CONFIG:Release>:/GL> /Gy /Gw)
else()
  target_compile_options(facestudio_core PRIVATE $<$<CONFIG:Release>:-Os> -ffunction-sections -fdata-sections)
endif()

# ---------------------------------------------------------------------------
# GUI application (Win32 + GDI + WIC; system DLLs only)
# ---------------------------------------------------------------------------
if(WIN32 AND FACESTUDIO_BUILD_APP)
  string(REPLACE "." "," FS_VERSION_COMMA "${FACESTUDIO_VERSION}.0")
  set(FS_VERSION_FULL "${FACESTUDIO_VERSION}.0")
  if(FACESTUDIO_VERSION_LABEL)
    set(FS_VERSION_LABEL "${FACESTUDIO_VERSION_LABEL}")
  else()
    set(FS_VERSION_LABEL "${FACESTUDIO_VERSION}")
  endif()
  set(FS_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/app/app.manifest")
  configure_file(src/app/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
  add_executable(TomTomFaceStudio WIN32
    src/app/main.cpp
    src/app/spotify.cpp
    src/app/lrclib.cpp
    src/app/mirror.cpp
    src/app/extdisplay.cpp
    src/display/frame_converter.cpp
    src/display/frame_transport.cpp
    ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
  target_include_directories(TomTomFaceStudio PRIVATE
    ../watchface-research/project/src/opentom_skel/bin)
  target_link_libraries(TomTomFaceStudio PRIVATE facestudio_core
    user32 gdi32 comctl32 comdlg32 shell32 ole32 windowscodecs uuid winhttp)
  target_compile_definitions(TomTomFaceStudio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  facestudio_shrink(TomTomFaceStudio)
  if(MSVC)
    target_link_options(TomTomFaceStudio PRIVATE /MANIFEST:NO)  # manifest is embedded via app.rc
  endif()

  if(MSVC)
    add_executable(TomTomDisplayControl windows-idd/TomTomDisplayControl.cpp)
    target_link_libraries(TomTomDisplayControl PRIVATE cfgmgr32 swdevice)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests + CLI (used by the parity script that cross-checks against Python)
# ---------------------------------------------------------------------------
if(FACESTUDIO_BUILD_TESTS)
  enable_testing()
  add_executable(facestudio_tests tests/test_core.cpp tests/test_device.cpp)
  target_link_libraries(facestudio_tests PRIVATE facestudio_core)
  if(NOT MSVC AND NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(facestudio_tests PRIVATE Threads::Threads)
  endif()
  add_test(NAME facestudio_tests COMMAND facestudio_tests)
  facestudio_shrink(facestudio_tests)

  add_executable(ttface_tool tests/ttface_tool.cpp)
  target_link_libraries(ttface_tool PRIVATE facestudio_core)
  facestudio_shrink(ttface_tool)

  add_executable(display_protocol_tests
    ../watchface-research/project/tests/test_display_protocol.c
    ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c)
  set_target_properties(display_protocol_tests PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(display_protocol_tests PRIVATE /W4)
  else()
    target_compile_options(display_protocol_tests PRIVATE
      -Wall -Wextra -pedantic -Werror)
  endif()
  add_test(NAME display_protocol_tests COMMAND display_protocol_tests)

  add_executable(display_frame_converter_tests
    tests/test_frame_converter.cpp
    src/display/frame_converter.cpp)
  if(MSVC)
    target_compile_options(display_frame_converter_tests PRIVATE /W4)
  else()
    target_compile_options(display_frame_converter_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME display_frame_converter_tests
    COMMAND display_frame_converter_tests)

  add_executable(display_frame_transport_tests
    tests/test_frame_transport.cpp
    src/display/frame_transport.cpp
    ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c)
  find_package(Threads REQUIRED)
  target_link_libraries(display_frame_transport_tests PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(display_frame_transport_tests PRIVATE ws2_32)
  endif()
  if(MSVC)
    target_compile_options(display_frame_transport_tests PRIVATE /W4)
  else()
    target_compile_options(display_frame_transport_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME display_frame_transport_tests
    COMMAND display_frame_transport_tests)

  # --- windows-idd / display control (display-driver agent) ---------------------------------------
  # Portable: status line, pipe protocol, single-owner rule, keepalive decision, INF matching,
  # FrameTransport pause/counter.
  add_executable(idd_contract_tests
    windows-idd/tests/test_idd_ipc.cpp
    src/display/frame_transport.cpp
    ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c)
  target_link_libraries(idd_contract_tests PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(idd_contract_tests PRIVATE ws2_32)
  endif()
  if(MSVC)
    target_compile_options(idd_contract_tests PRIVATE /W4)
  else()
    target_compile_options(idd_contract_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME idd_contract_tests COMMAND idd_contract_tests)

  # FrameTransport against a fake receiver (absent / hung / dropping / stale connection). The fake listens
  # on 127.0.0.1:18745, so this test must not run concurrently with anything else using that port.
  # TT_DISPLAY_TRANSPORT_TEST_LOOPBACK is defined for THIS target only: it makes this target's private
  # copy of frame_transport.cpp connect to loopback. It is never set for the app or the driver.
  add_executable(idd_transport_robustness_tests
    windows-idd/tests/test_transport_robustness.cpp
    src/display/frame_transport.cpp
    ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c)
  target_compile_definitions(idd_transport_robustness_tests PRIVATE
    TT_DISPLAY_TRANSPORT_TEST_LOOPBACK=1)
  target_link_libraries(idd_transport_robustness_tests PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(idd_transport_robustness_tests PRIVATE ws2_32)
  endif()
  if(MSVC)
    target_compile_options(idd_transport_robustness_tests PRIVATE /W4)
  else()
    target_compile_options(idd_transport_robustness_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME idd_transport_robustness_tests COMMAND idd_transport_robustness_tests)
  set_tests_properties(idd_transport_robustness_tests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

  # The driver's gate + control pipe, without the WDK: real named pipe and real Global\ lease event.
  if(WIN32)
    add_executable(idd_control_channel_tests
      windows-idd/tests/test_control_channel.cpp
      windows-idd/ControlChannel.cpp
      src/display/frame_transport.cpp
      ../watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.c)
    target_link_libraries(idd_control_channel_tests PRIVATE ws2_32)
    if(MSVC)
      target_compile_options(idd_control_channel_tests PRIVATE /W4)
    endif()
    add_test(NAME idd_control_channel_tests COMMAND idd_control_channel_tests)
    set_tests_properties(idd_control_channel_tests PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
  endif()
endif()
