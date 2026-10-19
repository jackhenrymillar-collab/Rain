cmake_minimum_required(VERSION 3.22)

# Universal binary and minimum macOS version must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(Rain VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RAIN_BUILD_PLUGIN "Build the JUCE plugin (VST3, AU, Standalone)" ON)
option(RAIN_BUILD_TESTS "Build the DSP unit tests" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Mutable Instruments Beads DSP and the parts of stmlib it needs (MIT License).
# Kept as close to upstream as possible; see third_party/mutable/README.md.
# ---------------------------------------------------------------------------
add_library(mutable_beads STATIC
    third_party/mutable/beads/dsp/processor.cc
    third_party/mutable/beads/dsp/mu_law.cc
    third_party/mutable/beads/dsp/delay_clock.cc
    third_party/mutable/beads/dsp/delay_controller.cc
    third_party/mutable/beads/dsp/trigger_generator.cc
    third_party/mutable/beads/resources.cc
    third_party/mutable/stmlib/utils/random.cc
    third_party/mutable/stmlib/dsp/units.cc)
target_include_directories(mutable_beads SYSTEM PUBLIC third_party/mutable)
# TEST selects stmlib's portable code paths instead of ARM assembly.
# BEADS_MIN_ATTACK_SECONDS: Rain gives grains a 1.5 ms minimum attack (the module
# uses 0.5 ms), which stops square and short grains popping on low notes.
target_compile_definitions(mutable_beads PUBLIC TEST BEADS_MIN_ATTACK_SECONDS=0.0015f)
target_compile_features(mutable_beads PUBLIC cxx_std_20)
set_target_properties(mutable_beads PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(mutable_beads PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Rain DSP library: plain C++, no JUCE, so it can be tested on any platform.
# ---------------------------------------------------------------------------
add_library(rain_dsp STATIC
    source/dsp/InputGain.cpp
    source/dsp/RainEngine.cpp)
target_include_directories(rain_dsp PUBLIC source)
target_link_libraries(rain_dsp PUBLIC mutable_beads)
set_target_properties(rain_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(rain_dsp PRIVATE /W4)
else()
    target_compile_options(rain_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(RAIN_BUILD_PLUGIN)
    set(RAIN_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout (otherwise downloaded)")
    if(RAIN_JUCE_DIR)
        add_subdirectory(${RAIN_JUCE_DIR} juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.10
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(Rain
        PRODUCT_NAME "Rain"
        COMPANY_NAME "Overcast Audio"
        BUNDLE_ID "com.overcastaudio.rain"
        PLUGIN_MANUFACTURER_CODE Ovca
        PLUGIN_CODE Ran1
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Delay
        COPY_PLUGIN_AFTER_BUILD ${APPLE})

    target_sources(Rain PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp
        source/Presets.cpp
        source/ui/Components.cpp
        source/ui/GrainDisplay.cpp
        source/ui/PresetBar.cpp
        source/ui/Theme.cpp)

    # Cormorant Garamond and Jost, SIL Open Font License (see resources/fonts).
    juce_add_binary_data(RainFonts SOURCES
        resources/fonts/CormorantGaramond-Medium.ttf
        resources/fonts/CormorantGaramond-SemiBold.ttf
        resources/fonts/Jost-Regular.ttf
        resources/fonts/Jost-Medium.ttf)
    set_target_properties(RainFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

    target_compile_definitions(Rain PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Rain
        PRIVATE
            rain_dsp
            RainFonts
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Headless panel renderer, for screenshots (off by default).
    option(RAIN_BUILD_SNAPSHOT "Build the panel snapshot tool" OFF)
    if(RAIN_BUILD_SNAPSHOT)
        add_executable(rain_snapshot tools/Snapshot.cpp)
        target_include_directories(rain_snapshot PRIVATE source)
        target_link_libraries(rain_snapshot PRIVATE Rain rain_dsp juce::juce_gui_extra)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RAIN_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(rain_tests
        tests/DspTests.cpp
        tests/EngineTests.cpp
        tests/MotionTests.cpp)
    target_link_libraries(rain_tests PRIVATE rain_dsp Catch2::Catch2WithMain)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(rain_tests)
endif()
