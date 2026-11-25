cmake_minimum_required(VERSION 3.16)

# TinyUVC is header-only (see src/) and is distributed as an Arduino
# library. This CMake build runs it on the desktop (Linux/macOS/Windows):
# there is no USB device controller there, so UVCHostSimulator plays the
# USB host and the TinyUSB core - the device side (descriptors, class
# driver, probe/commit, payload streaming) is the same code that runs on
# the microcontroller.
#
#   cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
#
# Other CMake projects can use add_subdirectory() + target_link_libraries(
# ... TinyUVC). See docs/Desktop.md.
project(TinyUVC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TINYUVC_IS_TOP_LEVEL TRUE)
else()
    set(TINYUVC_IS_TOP_LEVEL FALSE)
endif()

option(TINYUVC_BUILD_TESTS "Build and register the desktop tests (tests/)" ${TINYUVC_IS_TOP_LEVEL})
option(TINYUVC_BUILD_EXAMPLES "Build the desktop examples (examples/desktop/)" ${TINYUVC_IS_TOP_LEVEL})
option(TINYUVC_WITH_TINYCAMERA "Build the TinyCamera integration (TinyUVCCamera.h)" ON)
option(TINYUVC_WITH_AUDIO_TOOLS "Build the arduino-audio-tools integration" ON)
option(TINYUVC_SANITIZE "Build tests and examples with address/undefined sanitizers" OFF)

include(FetchContent)

# Header-only library target
add_library(TinyUVC INTERFACE)
add_library(TinyUVC::TinyUVC ALIAS TinyUVC)
target_include_directories(TinyUVC
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>)
target_compile_features(TinyUVC INTERFACE cxx_std_17)
target_compile_definitions(TinyUVC INTERFACE TINYUVC_DESKTOP)

# Local checkouts of the dependencies. Default: the sibling folder ../<name>
# (the Arduino libraries folder layout) if there is one, else the library is
# downloaded.
set(TINYUVC_TINYCAMERA_DIR "" CACHE PATH "Local TinyCamera checkout")
set(TINYUVC_AUDIO_TOOLS_DIR "" CACHE PATH "Local arduino-audio-tools checkout")

function(tinyuvc_dependency name dir repo)
    if(TARGET ${name})
        return()
    endif()
    if(NOT dir AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${name}/CMakeLists.txt")
        set(dir "${CMAKE_CURRENT_SOURCE_DIR}/../${name}")
    endif()
    if(dir)
        message(STATUS "TinyUVC: using ${dir}")
        add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/deps/${name}")
    else()
        message(STATUS "TinyUVC: fetching ${name}")
        FetchContent_Declare(${name} GIT_REPOSITORY ${repo} GIT_TAG main
                             GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()

if(TINYUVC_WITH_TINYCAMERA)
    set(TINYCAMERA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(TINYCAMERA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINYJPEG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TinyJPEG/CMakeLists.txt" AND
       NOT FETCHCONTENT_SOURCE_DIR_TINYJPEG)
        set(FETCHCONTENT_SOURCE_DIR_TINYJPEG "${CMAKE_CURRENT_SOURCE_DIR}/../TinyJPEG"
            CACHE PATH "" FORCE)
    endif()
    tinyuvc_dependency(TinyCamera "${TINYUVC_TINYCAMERA_DIR}"
                       https://github.com/pschatzmann/TinyCamera.git)
endif()

if(TINYUVC_WITH_AUDIO_TOOLS AND NOT TARGET arduino-audio-tools)
    # audio-tools' own minimal Arduino emulation (IS_MIN_DESKTOP) is enough
    set(ADD_ARDUINO_EMULATOR OFF CACHE BOOL "" FORCE)
    set(ADD_PORTAUDIO OFF CACHE BOOL "" FORCE)
    tinyuvc_dependency(arduino-audio-tools "${TINYUVC_AUDIO_TOOLS_DIR}"
                       https://github.com/pschatzmann/arduino-audio-tools.git)
endif()

if(TINYUVC_SANITIZE)
    set(TINYUVC_SANITIZE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Adds a desktop program that uses TinyUVC
function(tinyuvc_program name)
    cmake_parse_arguments(P "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${P_SOURCES})
    target_link_libraries(${name} PRIVATE TinyUVC ${P_LIBS})
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -Wall -Wextra ${TINYUVC_SANITIZE_FLAGS})
        target_link_options(${name} PRIVATE ${TINYUVC_SANITIZE_FLAGS})
    endif()
endfunction()

if(TINYUVC_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()

if(TINYUVC_BUILD_EXAMPLES)
    add_subdirectory(examples/desktop)
endif()
