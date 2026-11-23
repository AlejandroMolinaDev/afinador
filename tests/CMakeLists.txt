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
    unit/TestHelpers.h
    signals/SignalGenerators.h
)

target_compile_definitions(NovaPitchTests PRIVATE
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
