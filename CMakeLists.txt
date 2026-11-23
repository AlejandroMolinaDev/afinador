cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# Platform settings that must be known before project()
# ---------------------------------------------------------------------------
# macOS: Universal Binary so the same bundle loads in FL Studio running natively
# on Apple Silicon and in FL Studio running on Intel / under Rosetta.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# Windows: static MSVC runtime (/MT) so the plugin loads in FL Studio on machines
# without the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NovaPitch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(NOVAPITCH_BUILD_TESTS        "Build the Catch2 unit tests"                                   ON)
option(NOVAPITCH_COPY_AFTER_BUILD   "Copy plugins to the system plugin folders after each build"    OFF)
option(NOVAPITCH_ENABLE_CLAP        "Build the CLAP format (clap-juce-extensions)"                  ON)
option(NOVAPITCH_ENABLE_AAX         "Build AAX (requires the AAX SDK + Avid developer account)"     OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerSettings)

# ---------------------------------------------------------------------------
# Dependencies (git submodules, pinned by commit)
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "Submodules missing. Run: git submodule update --init --recursive")
endif()

add_subdirectory(libs/JUCE)
if(NOVAPITCH_ENABLE_CLAP)
    add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(NOVAPITCH_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NOVAPITCH_FORMATS AU)
endif()
if(NOVAPITCH_ENABLE_AAX)
    list(APPEND NOVAPITCH_FORMATS AAX)
endif()

# IMPORTANT (FL Studio): the identifiers below (manufacturer/plugin codes, bundle
# id, CLAP id), the bus layout, the MIDI input flag and the AU type are stored in
# every saved FL Studio project. NEVER change them after a public release or
# existing projects will no longer find the plugin. See CLAUDE.md.
juce_add_plugin(NovaPitch
    PRODUCT_NAME                "NovaPitch"
    COMPANY_NAME                "NovaPitch Audio"
    COMPANY_WEBSITE             "https://example.com/novapitch"
    BUNDLE_ID                   "com.novapitch.novapitch"
    PLUGIN_MANUFACTURER_CODE    Nvpt
    PLUGIN_CODE                 Npc1
    DESCRIPTION                 "Real-time monophonic pitch correction"
    FORMATS                     ${NOVAPITCH_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE     # MIDI Target / scale learn / chord harmonies (phases 3-7)
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect   # 'aumf': AU effect that can receive MIDI
    AAX_CATEGORY                AAX_ePlugInCategory_PitchShift
    COPY_PLUGIN_AFTER_BUILD     ${NOVAPITCH_COPY_AFTER_BUILD}
    MICROPHONE_PERMISSION_ENABLED TRUE   # Standalone on macOS needs mic access
    MICROPHONE_PERMISSION_TEXT  "NovaPitch needs audio input to correct your voice."
    HARDENED_RUNTIME_ENABLED    TRUE
    HARDENED_RUNTIME_OPTIONS    com.apple.security.device.audio-input
)

# Pure DSP (no GUI): src/dsp. Used by the plugin, the unit tests and tools/bench.
add_library(NovaPitchDsp INTERFACE)

target_sources(NovaPitchDsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Biquad.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/InputProfile.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/PitchUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/PitchDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/PitchDetector.h
)
target_include_directories(NovaPitchDsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(NovaPitchDsp INTERFACE juce::juce_dsp)

# All plugin code lives in an INTERFACE library so the unit tests compile exactly
# the same sources as the plugin (without the plugin-format wrappers).
add_library(NovaPitchCore INTERFACE)

target_sources(NovaPitchCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PluginProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Parameters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/PitchFrameFifo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PluginEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LookAndFeel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/LookAndFeel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/TuningMeter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/TuningMeter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PitchTrace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/components/PitchTrace.h
)

target_include_directories(NovaPitchCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(NovaPitchCore
    INTERFACE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Don't expose ~2080 "MIDI CC" pseudo-parameters in VST3 (they would clutter
        # FL Studio's parameter / automation browser). MIDI notes still arrive as
        # VST3 note events; CC control in FL uses "Link to controller".
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_WIN_PER_MONITOR_DPI_AWARE=1   # crisp UI on HiDPI Windows; verify in FL (docs/TESTING_FL.md)
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        NOVAPITCH_VERSION_STRING="${PROJECT_VERSION}"
)

target_link_libraries(NovaPitchCore
    INTERFACE
        NovaPitchDsp
        juce::juce_audio_utils
)

target_link_libraries(NovaPitch
    PRIVATE
        NovaPitchCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

novapitch_apply_compiler_settings(NovaPitch)

if(NOVAPITCH_ENABLE_CLAP)
    clap_juce_extensions_plugin(TARGET NovaPitch
        CLAP_ID       "com.novapitch.novapitch"
        CLAP_FEATURES audio-effect pitch-correction mono stereo
        CLAP_MANUAL_URL  "https://example.com/novapitch/manual"
        CLAP_SUPPORT_URL "https://example.com/novapitch/support"
        DO_COPY          ${NOVAPITCH_COPY_AFTER_BUILD})
endif()

# ---------------------------------------------------------------------------
# Developer / CI tools: host test, benchmark, UI snapshots
# ---------------------------------------------------------------------------
option(NOVAPITCH_BUILD_TOOLS "Build tools/ (host test, benchmark, UI snapshot)" ON)
if(NOVAPITCH_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NOVAPITCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(libs/Catch2 EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
