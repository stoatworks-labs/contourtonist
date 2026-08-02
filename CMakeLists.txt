cmake_minimum_required(VERSION 3.22)

# --------------------------------------------------------------------------------------
# These two must be set BEFORE project(), not after.
#
# project() runs the compiler checks that bake in the target architecture. Setting
# CMAKE_OSX_ARCHITECTURES afterwards is accepted without complaint, appears in the cache,
# is echoed back by every `cmake -L`, and produces an arm64-only binary anyway. The build
# log gives no hint. `lipo -archs` on the built binary is the only thing that will tell
# you, so check that and not the log.
# --------------------------------------------------------------------------------------
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")

project(Contourtonist VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC's <cmath> only declares M_PI when this is defined, and it must be defined
# before <cmath> is first included — Biquad.h pulls it in transitively, so a
# #define at the top of Biquad.cpp/Weighting.cpp would already be too late.
#
# Directory scope, not target scope: those two DSP sources are compiled twice,
# once into the JUCE plugin target and once into the contourtonist_core static
# library the tests link. Defining it on the plugin target alone leaves the
# static library failing to compile, which is exactly what happened.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# --------------------------------------------------------------------------------------
# Build identity for crash reports. A compiled binary cannot read its own git revision at
# runtime, and a report carrying only a version number cannot tell a released build from
# one three commits past the tag. `-dirty` means uncommitted changes were built.
# --------------------------------------------------------------------------------------
find_package(Git QUIET)
set(CONTOURTONIST_GIT_REV "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE CONTOURTONIST_GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE CONTOURTONIST_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT CONTOURTONIST_GIT_REV)
        set(CONTOURTONIST_GIT_REV "unknown")
    elseif(CONTOURTONIST_GIT_DIRTY)
        set(CONTOURTONIST_GIT_REV "${CONTOURTONIST_GIT_REV}-dirty")
    endif()
endif()

juce_add_plugin(Contourtonist
    COMPANY_NAME "Allan Sargeant"
    BUNDLE_ID "com.allansargeant.contourtonist"
    PLUGIN_MANUFACTURER_CODE Alsg
    PLUGIN_CODE Ctn1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Contourtonist"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES "Fx" "EQ"
    AU_MAIN_TYPE "kAudioUnitType_Effect"
    # The standalone owns the measurement microphone, so it must be able to open an
    # input and to ask for permission to do so.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Contourtonist measures room level from a calibrated measurement microphone."
)

juce_generate_juce_header(Contourtonist)

# --------------------------------------------------------------------------------------
# The DSP and parsing code depends on nothing but the standard library, on purpose: it
# is built again as a plain library below so the test suite can run without JUCE, an
# audio device or a plugin host. Keep it that way — if something here starts needing
# juce_core, it belongs in a different directory.
# --------------------------------------------------------------------------------------
set(CONTOURTONIST_CORE_SOURCES
    Source/DSP/EqualLoudness.cpp
    Source/DSP/Biquad.cpp
    Source/DSP/ContourFilterBank.cpp
    Source/DSP/LoudnessController.cpp
    Source/DSP/Weighting.cpp
    Source/DSP/ShortTermLeq.cpp
    Source/Level/MeterProtocol.cpp
)

target_sources(Contourtonist
    PRIVATE
        ${CONTOURTONIST_CORE_SOURCES}
        Source/Diag/Diag.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PluginParameters.cpp
        Source/Level/NetworkLevel.cpp
        Source/GUI/CurveDisplay.cpp
)

# The standalone target only: a custom JUCEApplication that declines to open an audio
# input by default. See Source/StandaloneApp.cpp for why that matters.
if(TARGET Contourtonist_Standalone)
    target_sources(Contourtonist_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(Contourtonist_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_compile_definitions(Contourtonist
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        DIAG_GIT_REV="${CONTOURTONIST_GIT_REV}"
)

target_link_libraries(Contourtonist
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --------------------------------------------------------------------------------------
# Tests. Plain executables over the dependency-free core; `ctest` runs the lot.
# --------------------------------------------------------------------------------------
enable_testing()

add_library(contourtonist_core STATIC ${CONTOURTONIST_CORE_SOURCES})
target_include_directories(contourtonist_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

foreach(test IN ITEMS equal_loudness controller filter_bank weighting leq meter_protocol)
    add_executable(test_${test} tests/test_${test}.cpp)
    target_link_libraries(test_${test} PRIVATE contourtonist_core)
    add_test(NAME ${test} COMMAND test_${test})
endforeach()

# The network test is separate because it is the one test that needs JUCE — it drives
# real sockets over loopback rather than mocking them, since the thing worth testing is
# precisely whether the transport works.
juce_add_console_app(test_network PRODUCT_NAME "test_network")
target_sources(test_network PRIVATE
    tests/test_network.cpp
    Source/Level/NetworkLevel.cpp
    Source/Level/MeterProtocol.cpp
)
target_compile_definitions(test_network PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(test_network
    PRIVATE juce::juce_core juce::juce_events
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME network COMMAND test_network)
