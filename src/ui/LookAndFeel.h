#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace novapitch
{

/** Original NovaPitch palette: dark graphite background, cool panels and a
    user-configurable accent colour (warm coral by default). */
namespace Palette
{
inline const juce::Colour background   { 0xff101317 };
inline const juce::Colour panel        { 0xff181d23 };
inline const juce::Colour panelRaised  { 0xff1f252d };
inline const juce::Colour panelOutline { 0xff2a323c };
inline const juce::Colour grid         { 0xff232a33 };
inline const juce::Colour textPrimary  { 0xffe8ecf1 };
inline const juce::Colour textMuted    { 0xff8a95a3 };
inline const juce::Colour ok           { 0xff4cc38a };
inline const juce::Colour warn         { 0xffe8b34a };
inline const juce::Colour defaultAccent{ 0xffff6b4a };
} // namespace Palette

class NovaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NovaLookAndFeel();

    void setAccent (juce::Colour newAccent);
    juce::Colour getAccent() const noexcept { return accent; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

private:
    juce::Colour accent { Palette::defaultAccent };
};

} // namespace novapitch
