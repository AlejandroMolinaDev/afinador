# Developer / CI tools (not shipped).

# Host test: loads the built plugin binary like a DAW and runs FL-like scenarios.
juce_add_console_app(NovaPitchHostTest PRODUCT_NAME "NovaPitchHostTest")
target_sources(NovaPitchHostTest PRIVATE hosttest/HostTest.cpp)
target_include_directories(NovaPitchHostTest PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(NovaPitchHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<PLATFORM_ID:Darwin>,1,0>
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(NovaPitchHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if(APPLE)
    # AU hosting (generic editor view) needs CoreAudioKit, which JUCE doesn't add to console apps.
    target_link_libraries(NovaPitchHostTest PRIVATE "-framework CoreAudioKit")
endif()
novapitch_apply_compiler_settings(NovaPitchHostTest)

# Shared settings for tools that compile the plugin sources directly.
function(novapitch_add_core_tool target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(${target} PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_STANDALONE_APPLICATION=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(${target} PRIVATE
        NovaPitchCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    novapitch_apply_compiler_settings(${target})
endfunction()

novapitch_add_core_tool(NovaPitchBench bench/PitchBench.cpp)
novapitch_add_core_tool(NovaPitchSnapshot snapshot/UiSnapshot.cpp)
novapitch_add_core_tool(NovaPitchRender render/Render.cpp)
