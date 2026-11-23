#include "ui/components/PitchTrace.h"

#include "dsp/PitchUtils.h"
#include "ui/LookAndFeel.h"

#include <cmath>

namespace novapitch
{

namespace
{
constexpr float kVisibleSemitones = 24.0f;
constexpr double kMaxGapSeconds = 0.06;
} // namespace

PitchTrace::PitchTrace (double windowSeconds)
    : window (windowSeconds), points (8192), accent (Palette::defaultAccent)
{
}

void PitchTrace::pushFrame (double t, float midi)
{
    if (count > 0 && t + 0.25 < latestTime)
        count = 0; // stream restarted (prepareToPlay / sample-rate change)

    points[head] = { t, midi };
    head = (head + 1) % points.size();
    count = std::min (count + 1, points.size());
    latestTime = t;

    // Follow the singer slowly so the plot stays centred.
    if (midi >= 0.0f)
        centreMidi += 0.02f * (midi - centreMidi);
}

void PitchTrace::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (Palette::panelOutline);
    g.drawRoundedRectangle (area.reduced (0.5f), 10.0f, 1.0f);

    auto plot = area.reduced (10.0f, 8.0f);
    plot.removeFromLeft (30.0f); // note labels
    const float lowMidi = centreMidi - kVisibleSemitones * 0.5f;
    const float highMidi = centreMidi + kVisibleSemitones * 0.5f;
    auto yFor = [&] (float m) { return juce::jmap (m, lowMidi, highMidi, plot.getBottom(), plot.getY()); };
    auto xFor = [&] (double t) {
        return static_cast<float> (juce::jmap (t, latestTime - window, latestTime,
                                               static_cast<double> (plot.getX()), static_cast<double> (plot.getRight())));
    };

    // Semitone grid
    g.setFont (juce::FontOptions (10.0f));
    for (int n = static_cast<int> (std::ceil (lowMidi)); n <= static_cast<int> (std::floor (highMidi)); ++n)
    {
        const float y = yFor (static_cast<float> (n));
        const bool isC = ((n % 12) + 12) % 12 == 0;
        g.setColour (isC ? Palette::panelOutline.brighter (0.2f) : Palette::grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        if (isC)
        {
            g.setColour (Palette::textMuted);
            g.drawText (juce::String (dsp::midiNoteName (n)), juce::Rectangle<float> (area.getX() + 6.0f, y - 7.0f, 30.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }

    if (count < 2)
        return;

    juce::Path path;
    bool penDown = false;
    double prevT = 0.0;
    const size_t start = (head + points.size() - count) % points.size();

    for (size_t i = 0; i < count; ++i)
    {
        const auto& p = points[(start + i) % points.size()];
        if (p.t < latestTime - window)
            continue;

        if (p.midi < 0.0f || (penDown && p.t - prevT > kMaxGapSeconds))
        {
            penDown = false;
            if (p.midi < 0.0f)
                continue;
        }

        const juce::Point<float> xy { xFor (p.t), yFor (juce::jlimit (lowMidi - 1.0f, highMidi + 1.0f, p.midi)) };
        if (! penDown)
            path.startNewSubPath (xy);
        else
            path.lineTo (xy);
        penDown = true;
        prevT = p.t;
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}

} // namespace novapitch
