#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace novapitch
{

/** Real-time view of the detected vibrato: rate and depth readout plus the
    last ~1.5 s of the vibrato oscillation (cents). Message thread only. */
class VibratoDisplay final : public juce::Component
{
public:
    VibratoDisplay();

    /** Called for every analysis frame drained from the FIFO. */
    void pushFrame (bool voiced, float vibratoCents, float rateHz, float depthCents);
    void setAccent (juce::Colour c) { accent = c; }

    void paint (juce::Graphics&) override;

private:
    static constexpr int kPoints = 600; // ~1.6 s at 375 frames/s

    std::array<float, kPoints> trace {};
    int head = 0;
    float rate = 0.0f, depth = 0.0f;
    int framesSinceDetected = 100000;
    juce::Colour accent;
};

} // namespace novapitch
