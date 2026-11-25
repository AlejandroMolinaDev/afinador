# Unit tests: compile the same NovaPitchCore sources as the plugin, without the
# plugin-format wrappers, and run them with Catch2 through CTest.

add_executable(NovaPitchTests
    unit/PassthroughTests.cpp
    unit/StateTests.cpp
    unit/PitchDetectorTests.cpp
    unit/NoteMapperCurveTests.cpp
    unit/PsolaShifterTests.cpp
    unit/CorrectionTests.cpp
    unit/RealtimeSafetyTests.cpp
    unit/VibratoTests.cpp
    unit/PresetMidiTests.cpp
    unit/HarmonyMidiTests.cpp
    unit/GraphTests.cpp
    unit/KeySyncTests.cpp
    unit/TimeEditTests.cpp
    unit/TransitionTests.cpp
    unit/AuditTests.cpp
    unit/FootprintTests.cpp
    unit/DetuneAbTests.cpp
    unit/CompatibilityTests.cpp
    unit/TestHelpers.h
    signals/SignalGenerators.h
)

target_compile_definitions(NovaPitchTests PRIVATE
    NOVAPITCH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    NOVAPITCH_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_include_directories(NovaPitchTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(NovaPitchTests PRIVATE
    NovaPitchCore
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

novapitch_apply_compiler_settings(NovaPitchTests)
if(NOT MSVC)
    # Exact float comparisons are intended in tests. Source-level options come after
    # the JUCE warning flags, so they win.
    get_target_property(_testSources NovaPitchTests SOURCES)
    set_source_files_properties(${_testSources} PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/libs/Catch2/extras")
include(Catch)
catch_discover_tests(NovaPitchTests DISCOVERY_MODE PRE_TEST)

# UI tests: the real editor in a desktop window, driven through its ComponentPeer
# like a mouse and keyboard would (see tests/ui/UiHarness.h). Linux needs a
# display: run ctest under `xvfb-run -a`; without one the UI tests are skipped.
add_executable(NovaPitchUiTests
    ui/UiMain.cpp
    ui/LayoutTests.cpp
    ui/ControlTests.cpp
    ui/NotesHarmonyTests.cpp
    ui/GraphEditorTests.cpp
    ui/LifetimeTests.cpp
    ui/UiHarness.h
    ui/UiTestAccess.h
    ui/LayoutChecks.h
    support/XvfbSupport.h
    unit/TestHelpers.h
    signals/SignalGenerators.h
)

target_compile_definitions(NovaPitchUiTests PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0
    JUCE_MODAL_LOOPS_PERMITTED=1   # runDispatchLoopUntil (the plugin's own code never runs modal loops)
    NOVAPITCH_UI_SHOTS_DIR="${CMAKE_BINARY_DIR}/ui-shots"
)

target_include_directories(NovaPitchUiTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(NovaPitchUiTests PRIVATE
    NovaPitchCore
    Catch2::Catch2
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

novapitch_apply_compiler_settings(NovaPitchUiTests)
if(NOT MSVC)
    get_target_property(_uiTestSources NovaPitchUiTests SOURCES)
    set_source_files_properties(${_uiTestSources} PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()

# Skipped (no display) is reported by Catch2 with exit code 4.
# RUN_SERIAL: windows of parallel test processes would cover each other and take
# the focus away (which closes pop-up menus, as switching apps does).
catch_discover_tests(NovaPitchUiTests DISCOVERY_MODE PRE_TEST TEST_PREFIX "ui: "
                     PROPERTIES SKIP_RETURN_CODE 4 LABELS ui RUN_SERIAL TRUE)
