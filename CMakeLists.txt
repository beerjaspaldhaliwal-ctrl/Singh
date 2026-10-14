cmake_minimum_required(VERSION 3.22)

project(SinghAIMixer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary" FORCE)
    endif()
endif()

option(SAM_BUILD_PLUGIN "Build the VST3 / AU plugin (downloads JUCE)" ON)
option(SAM_BUILD_TESTS "Build the DSP unit tests" ON)

# ------------------------------------------------------------------ DSP core (no JUCE)
add_library(sam_dsp STATIC
    Source/dsp/Analyzer.cpp
    Source/dsp/ChannelStrip.cpp
    Source/dsp/MixEngine.cpp)
target_include_directories(sam_dsp PUBLIC Source/dsp)
if(MSVC)
    target_compile_options(sam_dsp PRIVATE /W4 /fp:fast)
else()
    target_compile_options(sam_dsp PRIVATE -Wall -Wextra -O2)
endif()

if(SAM_BUILD_TESTS)
    enable_testing()
    add_executable(sam_dsp_tests tests/dsp_tests.cpp)
    target_link_libraries(sam_dsp_tests PRIVATE sam_dsp)
    add_test(NAME dsp_tests COMMAND sam_dsp_tests)
endif()

# ------------------------------------------------------------------ plugin
if(SAM_BUILD_PLUGIN)
    if(DEFINED JUCE_DIR)
        add_subdirectory(${JUCE_DIR} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(SAM_FORMATS VST3)
    if(APPLE)
        list(APPEND SAM_FORMATS AU)
    endif()
    list(APPEND SAM_FORMATS Standalone)

    juce_add_plugin(SinghAIMixer
        COMPANY_NAME "Singh Audio"
        COMPANY_WEBSITE "https://github.com/beerjaspaldhaliwal-ctrl/singh"
        PLUGIN_MANUFACTURER_CODE Sngh
        PLUGIN_CODE SAIM
        PRODUCT_NAME "Singh AI Mixer"
        VERSION ${PROJECT_VERSION}
        FORMATS ${SAM_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Dynamics EQ Mastering
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(SinghAIMixer PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(SinghAIMixer PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(SinghAIMixer
        PRIVATE
            sam_dsp
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
