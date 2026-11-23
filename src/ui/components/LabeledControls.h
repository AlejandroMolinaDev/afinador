#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/LookAndFeel.h"

namespace novapitch
{

/** Rotary knob with a caption above and the value (with units) below,
    attached to an APVTS parameter. */
class LabeledKnob final : public juce::Component
{
public:
    LabeledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                 const juce::String& caption, const juce::String& tooltip)
    {
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, Palette::textMuted);
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, Palette::textPrimary);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        if (auto* param = state.getParameter (paramId))
        {
            // Show the parameter's own text (units, rounding) and accept typed values.
            slider.textFromValueFunction = [param] (double v) {
                return param->getText (param->convertTo0to1 (static_cast<float> (v)), 32);
            };
            slider.valueFromTextFunction = [param] (const juce::String& t) {
                return static_cast<double> (param->convertFrom0to1 (param->getValueForText (t)));
            };
            slider.setDoubleClickReturnValue (true, static_cast<double> (param->convertFrom0to1 (param->getDefaultValue())));
            slider.updateText();
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r);
    }

    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Label label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** ComboBox with a caption, attached to an APVTS choice parameter. */
class LabeledCombo final : public juce::Component
{
public:
    LabeledCombo (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                  const juce::String& caption, const juce::String& tooltip)
    {
        label.setText (caption, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, Palette::textMuted);
        addAndMakeVisible (label);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            combo.addItemList (choice->choices, 1);
        combo.setTooltip (tooltip);
        addAndMakeVisible (combo);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        combo.setBounds (r.removeFromTop (26));
    }

    juce::ComboBox& getCombo() noexcept { return combo; }

private:
    juce::Label label;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** On/off switch (drawn by NovaLookAndFeel::drawToggleButton) attached to an
    APVTS bool parameter. */
class LabeledToggle final : public juce::Component
{
public:
    LabeledToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
                   const juce::String& caption, const juce::String& tooltip)
    {
        button.setButtonText (caption);
        button.setTooltip (tooltip);
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::ToggleButton& getButton() noexcept { return button; }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

} // namespace novapitch
