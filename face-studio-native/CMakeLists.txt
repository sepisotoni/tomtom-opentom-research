cmake_minimum_required(VERSION 3.20)
project(TomTomFaceStudio VERSION 1.0.0 LANGUAGES CXX)

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
  src/core/device_client.cpp)
target_include_directories(facestudio_core PUBLIC src/core)

if(MSVC)
  target_compile_options(facestudio_core PUBLIC /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
  target_compile_definitions(facestudio_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(facestudio_core PUBLIC -Wall -Wextra)
endif()
if(WIN32)
  target_link_libraries(facestudio_core PUBLIC ws2_32)
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
  add_executable(TomTomFaceStudio WIN32
    src/app/main.cpp
    src/app/app.rc)
  target_link_libraries(TomTomFaceStudio PRIVATE facestudio_core
    user32 gdi32 comctl32 comdlg32 shell32 ole32 windowscodecs uuid)
  target_compile_definitions(TomTomFaceStudio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
  facestudio_shrink(TomTomFaceStudio)
  if(MSVC)
    target_link_options(TomTomFaceStudio PRIVATE /MANIFEST:NO)  # manifest is embedded via app.rc
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
endif()
