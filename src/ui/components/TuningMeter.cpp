#include "ui/components/TuningMeter.h"

#include "dsp/PitchUtils.h"
#include "ui/LookAndFeel.h"

#include <cmath>

namespace novapitch
{

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
constexpr float kInTuneCents = 5.0f;
constexpr int kHoldFrames = 12; // keep the last note visible ~0.4 s after voicing ends
} // namespace

TuningMeter::TuningMeter() : accent (Palette::defaultAccent)
{
    setOpaque (false);
}

void TuningMeter::setPitch (float newHz, float newConfidence, float a4Hz)
{
    confidence = newConfidence;

    if (newHz > 0.0f && std::isfinite (newHz))
    {
        const float midi = dsp::hzToMidi (newHz, a4Hz);
        const int nearest = static_cast<int> (std::lround (midi));
        const float cents = dsp::centsFromNearestNote (midi);

        // Jump the needle when the note changes; smooth it otherwise.
        if (nearest != noteNumber || framesSinceVoiced > kHoldFrames)
            displayedCents = cents;

        noteNumber = nearest;
        targetCents = cents;
        hz = newHz;
        voiced = true;
        framesSinceVoiced = 0;
    }
    else
    {
        ++framesSinceVoiced;
        voiced = framesSinceVoiced <= kHoldFrames && noteNumber >= 0;
    }

    displayedCents += 0.45f * (targetCents - displayedCents);
    repaint();
}

void TuningMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (Palette::panelOutline);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    area = area.reduced (18.0f, 14.0f);
    const bool active = voiced && framesSinceVoiced == 0;
    const bool inTune = active && std::abs (displayedCents) <= kInTuneCents;
    const auto valueColour = ! voiced ? Palette::textMuted : (inTune ? Palette::ok : Palette::textPrimary);

    // Caption ---------------------------------------------------------------
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (Palette::textMuted);
    g.drawText (u8 ("NOTA DETECTADA"), area.removeFromTop (16.0f), juce::Justification::centredLeft);

    // Note + Hz + cents -----------------------------------------------------
    auto top = area.removeFromTop (area.getHeight() * 0.62f);
    auto noteArea = top.removeFromLeft (top.getWidth() * 0.5f);

    const auto noteText = voiced ? juce::String (dsp::midiNoteName (noteNumber)) : u8 ("—");
    g.setColour (voiced ? (active ? Palette::textPrimary : Palette::textMuted) : Palette::textMuted);
    g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
    g.drawText (noteText, noteArea.removeFromTop (noteArea.getHeight() * 0.75f), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (15.0f));
    g.setColour (Palette::textMuted);
    g.drawText (voiced ? juce::String (hz, 1) + " Hz" : u8 ("sin señal afinable"), noteArea, juce::Justification::centredLeft);

    const auto sign = displayedCents > 0.05f ? "+" : (displayedCents < -0.05f ? u8 ("−") : juce::String());
    const auto centsText = voiced ? sign + juce::String (std::abs (displayedCents), 1) + u8 (" ¢") : u8 ("—");
    g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
    g.setColour (valueColour);
    g.drawText (centsText, top.removeFromTop (top.getHeight() * 0.75f), juce::Justification::centredRight);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (inTune ? Palette::ok : Palette::textMuted);
    const auto status = ! voiced ? juce::String() : (inTune ? u8 ("afinado") : (displayedCents > 0 ? u8 ("alto") : u8 ("bajo")));
    g.drawText (status, top, juce::Justification::centredRight);

    // Cents scale -----------------------------------------------------------
    area.removeFromTop (8.0f);
    auto scale = area.removeFromTop (34.0f);
    const float x0 = scale.getX(), x1 = scale.getRight();
    auto xFor = [&] (float cents) { return juce::jmap (juce::jlimit (-50.0f, 50.0f, cents), -50.0f, 50.0f, x0, x1); };
    const float midY = scale.getCentreY();

    g.setColour (Palette::grid);
    g.fillRoundedRectangle (scale.withHeight (6.0f).withCentre ({ scale.getCentreX(), midY }), 3.0f);

    const auto zone = juce::Rectangle<float> (xFor (-kInTuneCents), midY - 3.0f, xFor (kInTuneCents) - xFor (-kInTuneCents), 6.0f);
    g.setColour (Palette::ok.withAlpha (0.55f));
    g.fillRoundedRectangle (zone, 3.0f);

    g.setFont (juce::FontOptions (11.0f));
    for (const float c : { -50.0f, -25.0f, 0.0f, 25.0f, 50.0f })
    {
        const float x = xFor (c);
        g.setColour (Palette::textMuted.withAlpha (0.6f));
        g.drawLine (x, midY + 6.0f, x, midY + 10.0f, 1.0f);
        const auto label = c > 0 ? "+" + juce::String (static_cast<int> (c)) : juce::String (static_cast<int> (c)).replace ("-", u8 ("−"));
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, midY + 10.0f, 40.0f, 14.0f), juce::Justification::centred);
    }

    if (voiced)
    {
        const float x = xFor (displayedCents);
        const auto needleColour = inTune ? Palette::ok : accent;
        g.setColour (needleColour.withAlpha (active ? 1.0f : 0.4f));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 22.0f).withCentre ({ x, midY }), 2.0f);
    }

    // Confidence ------------------------------------------------------------
    area.removeFromTop (14.0f); // room for the scale labels
    auto confRow = area.removeFromTop (14.0f);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (Palette::textMuted);
    g.drawText (u8 ("Confianza"), confRow.removeFromLeft (70.0f), juce::Justification::centredLeft);
    auto bar = confRow.withSizeKeepingCentre (confRow.getWidth(), 4.0f);
    g.setColour (Palette::grid);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (Palette::textMuted.withAlpha (0.9f));
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, confidence)), 2.0f);
}

} // namespace novapitch
