#include "ui/components/VibratoDisplay.h"

#include "ui/LookAndFeel.h"

#include <cmath>

namespace novapitch
{

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
constexpr int kHoldFrames = 200; // keep the last reading ~0.5 s
} // namespace

VibratoDisplay::VibratoDisplay() : accent (Palette::defaultAccent) {}

void VibratoDisplay::pushFrame (bool voiced, float vibratoCents, float rateHz, float depthCents)
{
    trace[static_cast<size_t> (head)] = voiced && std::isfinite (vibratoCents) ? vibratoCents : 0.0f;
    head = (head + 1) % kPoints;

    if (rateHz > 0.0f)
    {
        rate = rateHz;
        depth = depthCents;
        framesSinceDetected = 0;
    }
    else
    {
        ++framesSinceDetected;
    }
}

void VibratoDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Palette::panelRaised);
    g.fillRoundedRectangle (area, 8.0f);

    area = area.reduced (10.0f, 8.0f);
    g.setColour (Palette::textMuted);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (u8 ("VIBRATO DETECTADO"), area.removeFromTop (14.0f), juce::Justification::centredLeft);

    const bool showing = framesSinceDetected < kHoldFrames;
    g.setColour (showing ? Palette::textPrimary : Palette::textMuted);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    const auto readout = showing ? juce::String (rate, 1) + u8 (" Hz  ·  ±") + juce::String (juce::roundToInt (depth)) + u8 (" ¢")
                                 : u8 ("—");
    g.drawText (readout, area.removeFromTop (24.0f), juce::Justification::centredLeft);

    // Oscillation (newest on the right), scaled to the larger of ±60 c or the depth.
    area.removeFromTop (4.0f);
    const float range = std::max (60.0f, depth * 1.3f);
    const float midY = area.getCentreY();
    g.setColour (Palette::grid);
    g.drawHorizontalLine (juce::roundToInt (midY), area.getX(), area.getRight());

    juce::Path p;
    for (int i = 0; i < kPoints; ++i)
    {
        const float v = trace[static_cast<size_t> ((head + i) % kPoints)];
        const float x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (kPoints - 1);
        const float y = midY - juce::jlimit (-1.0f, 1.0f, v / range) * area.getHeight() * 0.5f;
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    g.setColour (showing ? accent : Palette::textMuted.withAlpha (0.5f));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace novapitch
