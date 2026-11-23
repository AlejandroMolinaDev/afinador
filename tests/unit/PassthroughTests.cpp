// Host-contract tests of the processor: layouts, latency, dry path, robustness.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"

#include <cstring>

using namespace novapitch;
using namespace novapitch::test;

namespace
{
/** Processes `input` in blocks of random size between 0 and maxBlock samples
    (FL Studio delivers irregular block sizes, including empty ones). */
juce::AudioBuffer<float> processInRandomBlocks (NovaPitchProcessor& p, const juce::AudioBuffer<float>& input,
                                                int maxBlock, uint32_t seed)
{
    juce::AudioBuffer<float> io (input);
    juce::MidiBuffer midi;
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> sizeDist (0, maxBlock);

    int pos = 0;
    while (pos < io.getNumSamples())
    {
        const int n = juce::jmin (sizeDist (rng), io.getNumSamples() - pos);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), pos, n);
        p.processBlock (view, midi);
        pos += n;
    }
    return io;
}

/** out[ch] == in[ch] delayed by `delay` samples, bit-exact. */
bool equalsDelayed (const juce::AudioBuffer<float>& in, int chIn, const juce::AudioBuffer<float>& out, int chOut, int delay)
{
    const int n = in.getNumSamples() - delay;
    return n > 0 && std::memcmp (out.getReadPointer (chOut, delay), in.getReadPointer (chIn),
                                 sizeof (float) * static_cast<size_t> (n)) == 0;
}
} // namespace

TEST_CASE ("Dry path (mix 0) is the input delayed by the reported latency, any block size", "[processor]")
{
    JuceFixture fixture;
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    const int mode = GENERATE (0, 1, 2, 3);
    constexpr int maxBlock = 4096;

    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    setParam (*p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (*p, ParamIDs::mix, 0.0f);
    p->prepareToPlay (sampleRate, maxBlock);

    const int latency = p->getLatencySamples();
    CHECK (latency == NovaPitchProcessor::latencyForMode (static_cast<QualityMode> (mode), sampleRate));

    juce::AudioBuffer<float> input (2, static_cast<int> (sampleRate)); // 1 second
    fillWithNoise (input, 2, 1234u);
    const auto output = processInRandomBlocks (*p, input, maxBlock, 42u);

    INFO ("sr=" << sampleRate << " mode=" << mode << " latency=" << latency);
    CHECK (equalsDelayed (input, 0, output, 0, latency));
    CHECK (equalsDelayed (input, 1, output, 1, latency));
}

TEST_CASE ("Latency per mode: Live ~3 ms, Tracking ~10 ms", "[processor][latency]")
{
    CHECK (NovaPitchProcessor::latencyForMode (QualityMode::Live, 48000.0) == 144);
    CHECK (NovaPitchProcessor::latencyForMode (QualityMode::Vintage, 48000.0) == 144);
    CHECK (NovaPitchProcessor::latencyForMode (QualityMode::Tracking, 48000.0) == 480);
    CHECK (NovaPitchProcessor::latencyForMode (QualityMode::HQ, 48000.0) == 480);
    CHECK (NovaPitchProcessor::latencyForMode (QualityMode::Live, 44100.0) == 132);
}

TEST_CASE ("Mono to stereo duplicates the processed mono channel", "[processor]")
{
    JuceFixture fixture;
    auto p = makeProcessor (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    setParam (*p, ParamIDs::mix, 0.0f);
    p->prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> input (2, 48000);
    fillWithNoise (input, 2, 7u); // channel 1 holds garbage the plugin must overwrite

    const auto output = processInRandomBlocks (*p, input, 512, 3u);
    CHECK (equalsDelayed (input, 0, output, 0, p->getLatencySamples()));
    CHECK (std::memcmp (output.getReadPointer (0), output.getReadPointer (1), sizeof (float) * 48000) == 0);
}

TEST_CASE ("Supported bus layouts", "[layout]")
{
    JuceFixture fixture;
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    CHECK (makeProcessor (stereo, stereo) != nullptr);
    CHECK (makeProcessor (mono, mono) != nullptr);
    CHECK (makeProcessor (mono, stereo) != nullptr);

    CHECK (makeProcessor (stereo, mono) == nullptr);
    CHECK (makeProcessor (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1()) == nullptr);
    CHECK (makeProcessor (juce::AudioChannelSet::disabled(), stereo) == nullptr);
}

TEST_CASE ("Default layout is stereo in / stereo out (FL Studio mixer insert)", "[layout]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    CHECK (p.getTotalNumInputChannels() == 2);
    CHECK (p.getTotalNumOutputChannels() == 2);
    CHECK (p.acceptsMidi());
    CHECK_FALSE (p.producesMidi());
}

TEST_CASE ("Re-preparing at a new sample rate keeps working", "[robustness]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    setParam (p, ParamIDs::mix, 0.0f);
    juce::MidiBuffer midi;

    for (const double sr : { 44100.0, 192000.0, 48000.0, 96000.0 })
    {
        p.prepareToPlay (sr, 4096);
        CHECK (juce::exactlyEqual (p.getDiagnostics().sampleRate.load(), sr));

        juce::AudioBuffer<float> buffer (2, 4096);
        fillWithNoise (buffer, 2, 11u);
        const juce::AudioBuffer<float> copy (buffer);
        p.processBlock (buffer, midi);
        CHECK (equalsDelayed (copy, 0, buffer, 0, p.getLatencySamples()));

        p.reset();
        p.releaseResources();
    }
}

TEST_CASE ("Empty blocks, MIDI events and NaN input are handled", "[robustness]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> empty (2, 0);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

    p.processBlock (empty, midi);
    CHECK (p.getDiagnostics().midiEventsReceived.load() == 2);
    CHECK (p.getDiagnostics().lastBlockSize.load() == 0);

    juce::AudioBuffer<float> bad (2, 512);
    fillWithNoise (bad, 2, 5u);
    bad.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
    bad.setSample (1, 20, std::numeric_limits<float>::infinity());
    juce::MidiBuffer none;
    for (int i = 0; i < 200; ++i)
        p.processBlock (bad, none);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE (std::isfinite (bad.getSample (ch, i)));
}
