#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/LookAndFeel.h"

namespace novapitch
{

class NovaPitchProcessor;

/** Phase 1 placeholder editor. Shows host/format diagnostics so the plugin can
    be verified inside FL Studio (Windows + macOS) before any DSP exists. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (NovaPitchProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 720;
    static constexpr int kBaseHeight = 420;

private:
    void timerCallback() override;
    juce::StringArray buildDiagnosticLines() const;

    NovaPitchProcessor& processor;
    NovaLookAndFeel lookAndFeel;

    juce::String hostDescription;
    bool hostIsFlStudio = false;
    juce::StringArray lines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace novapitch
