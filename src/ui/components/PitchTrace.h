#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace novapitch
{

/** Scrolling plot of the detected pitch over the last few seconds (piano-roll
    style: semitone grid, C notes labelled). Message thread only. */
class PitchTrace final : public juce::Component
{
public:
    explicit PitchTrace (double windowSeconds = 5.0);

    /** midi < 0 marks an unvoiced frame. Times must increase; a jump back clears the plot.
        outputMidi: corrected pitch (drawn in the accent colour), < 0 if none. */
    void pushFrame (double timeSeconds, float midi, float outputMidi = -1.0f);
    void setAccent (juce::Colour c) { accent = c; }

    void paint (juce::Graphics&) override;

private:
    struct Point { double t; float midi; float out; };

    double window;
    std::vector<Point> points; // ring buffer
    size_t head = 0, count = 0;
    double latestTime = 0.0;
    float centreMidi = 60.0f;
    juce::Colour accent;
};

} // namespace novapitch
