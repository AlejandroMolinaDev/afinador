#include "ui/LookAndFeel.h"

namespace novapitch
{

NovaLookAndFeel::NovaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::Label::textColourId, Palette::textPrimary);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panel);
    setColour (juce::TooltipWindow::textColourId, Palette::textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, Palette::panelOutline);
    setAccent (Palette::defaultAccent);
}

void NovaLookAndFeel::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::TextButton::buttonOnColourId, accent);
}

} // namespace novapitch
