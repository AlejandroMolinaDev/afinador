# Unit tests: compile the same NovaPitchCore sources as the plugin, without the
# plugin-format wrappers, and run them with Catch2 through CTest.

add_executable(NovaPitchTests
    unit/PassthroughTests.cpp
    unit/StateTests.cpp
    unit/PitchDetectorTests.cpp
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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/libs/Catch2/extras")
include(Catch)
catch_discover_tests(NovaPitchTests DISCOVERY_MODE PRE_TEST)
