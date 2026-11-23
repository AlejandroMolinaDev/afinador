#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PitchFrameFifo.h"
#include "ui/LookAndFeel.h"
#include "ui/components/PitchTrace.h"
#include "ui/components/TuningMeter.h"

#include <array>

namespace novapitch
{

class NovaPitchProcessor;

/** Main editor. All content lives in a fixed-size (kBaseWidth x kBaseHeight)
    component that is scaled with an AffineTransform, so the UI stays crisp on
    HiDPI / Retina and resizes 75 %–200 % with a fixed aspect ratio. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit PluginEditor (NovaPitchProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 760;
    static constexpr int kBaseHeight = 460;

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content (PluginEditor& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        PluginEditor& owner;
    };

    void timerCallback() override;
    juce::String buildFooterText() const;
    void updateProfileRangeLabel();

    NovaPitchProcessor& processor;
    NovaLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    Content content { *this };
    TuningMeter meter;
    PitchTrace trace;

    juce::Label profileLabel, profileRangeLabel, sensitivityLabel;
    juce::ComboBox profileBox;
    juce::Slider sensitivitySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> profileAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttachment;

    juce::String hostDescription;
    bool hostIsFlStudio = false;
    juce::String footerText;

    std::array<PitchFrame, 512> frameScratch {};
    PitchFrame lastFrame;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace novapitch
