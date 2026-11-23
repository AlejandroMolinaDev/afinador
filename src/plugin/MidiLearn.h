#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace novapitch
{

/** MIDI Learn: map MIDI CC numbers (any channel) to plugin parameters.

    - Message thread: startLearning (paramIndex) → the next CC that arrives on
      the audio thread is bound to that parameter (one CC per parameter).
    - Audio thread: processMidi() learns and stores the latest value of each
      mapped CC in an atomic (lock-free, no allocation).
    - Message thread: dispatchPending() (processor timer) applies the values
      with setValueNotifyingHost, so the host sees normal automation.
    - Mappings are saved in the plugin state by parameter ID (stable).

    Note (FL Studio + VST3): FL does not send CC to VST3 effects unless the
    plugin exposes CC pseudo-parameters (disabled on purpose); use FL's
    "Link to controller" there. CLAP, AU and Standalone receive CC directly. */
class MidiLearn
{
public:
    static constexpr int kNumCc = 128;
    static constexpr const char* kStateTag = "MidiMap";

    explicit MidiLearn (juce::AudioProcessor&);

    // Message thread ---------------------------------------------------------
    void startLearning (int paramIndex) noexcept { learning.store (paramIndex); }
    void cancelLearning() noexcept { learning.store (-1); }
    int getLearningParam() const noexcept { return learning.load(); }

    int getCcForParam (int paramIndex) const noexcept;
    void clearMapping (int paramIndex) noexcept;
    void clearAll() noexcept;

    /** Applies pending CC values. Returns true if a mapping changed since the last call. */
    bool dispatchPending();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    // Audio thread -------------------------------------------------------------
    void processMidi (const juce::MidiBuffer&) noexcept;

private:
    juce::AudioProcessor& processor;
    std::array<std::atomic<int>, kNumCc> ccToParam;
    std::array<std::atomic<float>, kNumCc> pending; // < 0 = nothing pending
    std::atomic<int> learning { -1 };
    std::atomic<bool> mappingChanged { false };
};

} // namespace novapitch
