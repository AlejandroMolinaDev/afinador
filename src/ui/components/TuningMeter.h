#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace novapitch
{

/** Real-time tuning meter: detected note, frequency and a cents scale
    (-50..+50) with a smoothed needle. Message thread only. */
class TuningMeter final : public juce::Component
{
public:
    TuningMeter();

    /** Updates the display (called at the UI frame rate). hz <= 0 means unvoiced. */
    void setPitch (float hz, float confidence, float a4Hz);
    void setAccent (juce::Colour c) { accent = c; repaint(); }

    void paint (juce::Graphics&) override;

private:
    juce::Colour accent;
    bool voiced = false;
    float hz = 0.0f;
    float confidence = 0.0f;
    int noteNumber = -1;
    float targetCents = 0.0f;
    float displayedCents = 0.0f;
    int framesSinceVoiced = 1000;
};

} // namespace novapitch
