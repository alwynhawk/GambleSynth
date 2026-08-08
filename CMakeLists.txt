cmake_minimum_required(VERSION 3.22)
project(GambleSynth VERSION 0.9.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(GambleSynth
    PRODUCT_NAME   "GambleSynth"
    COMPANY_NAME   "HWCDealer"
    BUNDLE_ID      "com.hwcdealer.gamblesynth"
    DESCRIPTION    "A slot machine and Ogun inspired Synthesizer to get random seeded synth patches."
    IS_SYNTH       TRUE
    NEEDS_MIDI_INPUT  TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE    # the seed box needs typed input
    COPY_PLUGIN_AFTER_BUILD FALSE
    # Lives outside assets/ on purpose: everything in there is globbed into
    # BinaryData for the cabinet skin, and the icon is not part of the artwork.
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/icon/gamblesynth.png"
    FORMATS        Standalone VST3
    PLUGIN_MANUFACTURER_CODE Hwcd       # 4 chars, first one uppercase
    PLUGIN_CODE    Gsyn
    VST3_CATEGORIES Instrument Synth)

# ---- Skin artwork -------------------------------------------------------
# Drop PNGs into assets/ and they get compiled into the binary. Absent, the
# plugin falls back to the plain drawn UI, so the build never depends on art.
file(GLOB SKIN_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.png")
if(SKIN_ASSETS)
    juce_add_binary_data(GambleSynthAssets SOURCES ${SKIN_ASSETS})
    set_target_properties(GambleSynthAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    message(STATUS "GambleSynth: embedding skin artwork")
else()
    message(STATUS "GambleSynth: no assets/*.png - using the drawn UI")
endif()

juce_generate_juce_header(GambleSynth)

target_sources(GambleSynth PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(GambleSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

if(SKIN_ASSETS)
    target_link_libraries(GambleSynth PRIVATE GambleSynthAssets)
    target_compile_definitions(GambleSynth PUBLIC GAMBLESYNTH_HAS_ASSETS=1)
endif()

target_link_libraries(GambleSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Headless render test (no DAW / no audio device needed) ----
juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
juce_generate_juce_header(RenderTest)
target_sources(RenderTest PRIVATE
    tools/RenderTest.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)
target_compile_definitions(RenderTest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
if(SKIN_ASSETS)
    target_link_libraries(RenderTest PRIVATE GambleSynthAssets)
    target_compile_definitions(RenderTest PUBLIC GAMBLESYNTH_HAS_ASSETS=1)
endif()

target_link_libraries(RenderTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
