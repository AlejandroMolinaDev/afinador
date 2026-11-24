#include "ui/LookAndFeel.h"

namespace novapitch
{

NovaLookAndFeel::NovaLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour (juce::Label::textColourId, Palette::textPrimary);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::panelRaised);
    setColour (juce::TooltipWindow::textColourId, Palette::textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, Palette::panelOutline);

    setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
    setColour (juce::ComboBox::outlineColourId, Palette::panelOutline);
    setColour (juce::ComboBox::textColourId, Palette::textPrimary);
    setColour (juce::ComboBox::arrowColourId, Palette::textMuted);
    setColour (juce::PopupMenu::backgroundColourId, Palette::panelRaised);
    setColour (juce::PopupMenu::textColourId, Palette::textPrimary);
    setColour (juce::PopupMenu::highlightedTextColourId, Palette::background);

    setColour (juce::Slider::textBoxTextColourId, Palette::textPrimary);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderOutlineColourId, Palette::grid);

    setAccent (Palette::defaultAccent);
}

void NovaLookAndFeel::setAccent (juce::Colour newAccent)
{
    accent = newAccent;
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
}

void NovaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = juce::jmax (3.0f, radius * 0.12f);
    const float arcRadius = radius - trackWidth * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::grid);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    const auto fill = slider.isEnabled() ? accent : Palette::textMuted;
    g.setColour (fill);
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body + indicator
    const float bodyRadius = arcRadius - trackWidth * 1.6f;
    g.setColour (Palette::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    g.setColour (Palette::panelOutline);
    g.drawEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (bodyRadius * 0.78f, angle);
    const auto from = centre.getPointOnCircumference (bodyRadius * 0.35f, angle);
    g.setColour (Palette::textPrimary);
    g.drawLine ({ from, tip }, juce::jmax (2.0f, trackWidth * 0.6f));
}

void NovaLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    const float arrowW = 8.0f, arrowH = 5.0f;
    const float ax = static_cast<float> (width) - 16.0f, ay = static_cast<float> (height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (ax - arrowW * 0.5f, ay - arrowH * 0.5f, ax + arrowW * 0.5f, ay - arrowH * 0.5f, ax, ay + arrowH * 0.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font NovaLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (14.0f));
}

juce::Font NovaLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const float size = button.getProperties().contains ("compactFont") ? 14.0f : 15.0f;
    return withDefaultMetrics (juce::FontOptions (juce::jmin (size, static_cast<float> (buttonHeight) * 0.6f)));
}

juce::Font NovaLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (14.0f));
}

void NovaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // Pill switch on the left, caption on the right.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto track = juce::Rectangle<float> (34.0f, 18.0f).withCentre ({ bounds.getX() + 18.0f, bounds.getCentreY() });
    const bool on = button.getToggleState();
    const auto trackColour = on ? accent : Palette::grid;

    g.setColour (button.isEnabled() ? trackColour : Palette::grid);
    g.fillRoundedRectangle (track, 9.0f);
    if (highlighted)
    {
        g.setColour (Palette::textPrimary.withAlpha (0.25f));
        g.drawRoundedRectangle (track, 9.0f, 1.0f);
    }

    const float d = 14.0f;
    const float cx = on ? track.getRight() - 2.0f - d * 0.5f : track.getX() + 2.0f + d * 0.5f;
    g.setColour (Palette::textPrimary);
    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx, track.getCentreY() }));

    g.setColour (button.isEnabled() ? Palette::textPrimary : Palette::textMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (track.getRight() + 10.0f - bounds.getX()),
                juce::Justification::centredLeft);
}

} // namespace novapitch
