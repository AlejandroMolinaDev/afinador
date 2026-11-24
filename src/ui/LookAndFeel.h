#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

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
    /** 15 px; 14 px for buttons marked compact (setCompact), e.g. dense toolbars. */
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    /** Counts the pop-up menu windows opened with this look (diagnostics / UI tests). */
    void preparePopupMenuWindow (juce::Component&) override { ++menusOpened; }
    int getNumMenusOpened() const noexcept { return menusOpened; }

private:
    juce::Colour accent { Palette::defaultAccent };
    int menusOpened = 0;
};

/** Marks a button to use the compact (14 px) font. */
inline void setCompact (juce::Button& b) { b.getProperties().set ("compactFont", true); }

/** Width a TextButton needs to show its text without squashing (same text area
    as LookAndFeel_V2::drawButtonText), plus `margin` px of bounds reduction.
    Fonts differ between Windows, macOS and Linux: lay toolbars out with this,
    never with widths measured on one system. */
inline int widthToFitText (juce::TextButton& b, int height, int margin = 4, int minWidth = 28)
{
    const auto font = b.getLookAndFeel().getTextButtonFont (b, height);
    const int indent = juce::jmin (juce::roundToInt (font.getHeight() * 0.6f), 2 + height / 4);
    const int text = static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (font, b.getButtonText())));
    return juce::jmax (minWidth, text + 2 * indent + margin + 2);
}

} // namespace novapitch
