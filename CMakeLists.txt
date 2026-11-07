cmake_minimum_required(VERSION 3.25)

project(VintageVisual VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VV_BUILD_PLUGIN "Build the JUCE VST3 plugin and its tests" ON)

# --- Core: measurement and utilities WITHOUT JUCE (shared by plugin and tests) ---
add_library(VintageVisualCore STATIC
    Source/Core/LoudnessMeter.cpp
    Source/Core/TruePeakDetector.cpp
    Source/Core/SpectrumAnalyzer.cpp
    Source/Core/LevelTracker.cpp
    Source/Core/DualSpectrum.cpp
    Source/Core/Pitch.cpp)

target_include_directories(VintageVisualCore PUBLIC Source/Core)

if(MSVC)
    target_compile_options(VintageVisualCore PRIVATE /W4)
else()
    target_compile_options(VintageVisualCore PRIVATE -Wall -Wextra)
endif()

enable_testing()

find_package(Threads REQUIRED)
add_executable(CoreTests Tests/CoreTests.cpp)
target_link_libraries(CoreTests PRIVATE VintageVisualCore Threads::Threads)
add_test(NAME CoreTests COMMAND CoreTests)

if(NOT VV_BUILD_PLUGIN)
    return()
endif()

# --- JUCE (exact tag, reproducible builds) ---
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.3
    GIT_SHALLOW    TRUE
    GIT_CONFIG     core.longpaths=true)
FetchContent_MakeAvailable(JUCE)

set(VV_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/MeterView.cpp
    Source/Panels.cpp
    Source/LevelPanels.cpp
    Source/AnalysisPanels.cpp
    Source/GenerativePanels.cpp
    Source/FloatingWindow.cpp
    Source/LookAndFeel.cpp)

set(VV_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# NOTE: plugin codes must be unique per plugin. Replace before publishing.
juce_add_plugin(VintageVisual
    COMPANY_NAME "Vintage Visual"
    PRODUCT_NAME "Vintage Visual"
    PLUGIN_MANUFACTURER_CODE Vvis
    PLUGIN_CODE Vvm1
    FORMATS VST3
    VST3_CATEGORIES Fx Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VintageVisual PRIVATE ${VV_PLUGIN_SOURCES})
target_compile_definitions(VintageVisual PUBLIC ${VV_JUCE_DEFINITIONS})
target_link_libraries(VintageVisual
    PRIVATE
        VintageVisualCore
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Plugin tests: processor + editor off-screen, no DAW needed ---
add_executable(PluginTests Tests/PluginTests.cpp ${VV_PLUGIN_SOURCES})
target_compile_definitions(PluginTests PRIVATE ${VV_JUCE_DEFINITIONS})
target_link_libraries(PluginTests
    PRIVATE
        VintageVisualCore
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
add_test(NAME PluginTests COMMAND PluginTests)
