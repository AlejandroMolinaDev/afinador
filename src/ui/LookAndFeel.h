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
inline const juce::Colour panelOutline { 0xff252c35 };
inline const juce::Colour textPrimary  { 0xffe8ecf1 };
inline const juce::Colour textMuted    { 0xff8a95a3 };
inline const juce::Colour ok           { 0xff4cc38a };
inline const juce::Colour defaultAccent{ 0xffff6b4a };
} // namespace Palette

class NovaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NovaLookAndFeel();

    void setAccent (juce::Colour newAccent);
    juce::Colour getAccent() const noexcept { return accent; }

private:
    juce::Colour accent { Palette::defaultAccent };
};

} // namespace novapitch
