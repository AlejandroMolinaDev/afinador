#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "plugin/PluginProcessor.h"

#include <memory>
#include <random>

namespace novapitch::test
{

/** Keeps the JUCE message manager alive for the duration of a test
    (AudioProcessorValueTreeState relies on it). */
struct JuceFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
};

inline std::unique_ptr<NovaPitchProcessor> makeProcessor (const juce::AudioChannelSet& in,
                                                         const juce::AudioChannelSet& out)
{
    auto p = std::make_unique<NovaPitchProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    if (! p->setBusesLayout (layout))
        return nullptr;
    return p;
}

/** Sets a parameter by its real (denormalised) value, as host automation would. */
inline void setParam (NovaPitchProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getValueTreeState().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** Host transport for tests: FL-like play head with a sample position. */
struct TestPlayHead final : juce::AudioPlayHead
{
    int64_t timeInSamples = 0;
    double sampleRate = 48000.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (timeInSamples);
        info.setTimeInSeconds (static_cast<double> (timeInSamples) / sampleRate);
        info.setBpm (120.0);
        return info;
    }
};

inline void fillWithNoise (juce::AudioBuffer<float>& buffer, int numChannels, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, dist (rng));
}

} // namespace novapitch::test
