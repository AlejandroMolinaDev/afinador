#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"

#include <cstring>

using namespace novapitch;
using namespace novapitch::test;

namespace
{
/** Processes `input` through the plugin in blocks of random size between 0 and
    maxBlock samples (FL Studio delivers irregular block sizes, including empty
    ones) and returns the processed copy. */
juce::AudioBuffer<float> processInRandomBlocks (NovaPitchProcessor& p,
                                                const juce::AudioBuffer<float>& input,
                                                int maxBlock,
                                                uint32_t seed)
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

/** Bit-exact channel comparison. */
bool channelsEqual (const juce::AudioBuffer<float>& a, int chA, const juce::AudioBuffer<float>& b, int chB)
{
    return a.getNumSamples() == b.getNumSamples()
        && std::memcmp (a.getReadPointer (chA), b.getReadPointer (chB),
                        sizeof (float) * static_cast<size_t> (a.getNumSamples())) == 0;
}
} // namespace

TEST_CASE ("Stereo passthrough is bit-exact with irregular block sizes", "[passthrough]")
{
    JuceFixture fixture;
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    constexpr int maxBlock = 4096;

    auto p = makeProcessor (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    p->prepareToPlay (sampleRate, maxBlock);

    juce::AudioBuffer<float> input (2, static_cast<int> (sampleRate)); // 1 second
    fillWithNoise (input, 2, 1234u);

    const auto output = processInRandomBlocks (*p, input, maxBlock, 42u);

    CHECK (channelsEqual (input, 0, output, 0));
    CHECK (channelsEqual (input, 1, output, 1));
    CHECK (p->getDiagnostics().maxBlockSize.load() <= maxBlock);
    CHECK (p->getDiagnostics().minBlockSize.load() >= 1);
}

TEST_CASE ("Mono to stereo duplicates the input", "[passthrough]")
{
    JuceFixture fixture;
    auto p = makeProcessor (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo());
    REQUIRE (p != nullptr);
    p->prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> input (2, 48000);
    fillWithNoise (input, 2, 7u); // channel 1 holds garbage the plugin must overwrite

    const auto output = processInRandomBlocks (*p, input, 512, 3u);
    CHECK (channelsEqual (input, 0, output, 0));
    CHECK (channelsEqual (output, 0, output, 1));
}

TEST_CASE ("Mono to mono passthrough", "[passthrough]")
{
    JuceFixture fixture;
    auto p = makeProcessor (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono());
    REQUIRE (p != nullptr);
    p->prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> input (1, 44100);
    fillWithNoise (input, 1, 99u);
    const auto output = processInRandomBlocks (*p, input, 256, 5u);
    CHECK (channelsEqual (input, 0, output, 0));
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

TEST_CASE ("Reported latency is zero in phase 1", "[latency]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    p.prepareToPlay (48000.0, 128);
    CHECK (p.getLatencySamples() == 0);
}

TEST_CASE ("Re-preparing at a new sample rate keeps working", "[robustness]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    juce::MidiBuffer midi;

    for (const double sr : { 44100.0, 192000.0, 48000.0, 96000.0 })
    {
        p.prepareToPlay (sr, 1024);
        CHECK (juce::exactlyEqual (p.getDiagnostics().sampleRate.load(), sr));

        juce::AudioBuffer<float> buffer (2, 1024);
        fillWithNoise (buffer, 2, 11u);
        const juce::AudioBuffer<float> copy (buffer);
        p.processBlock (buffer, midi);
        CHECK (channelsEqual (copy, 0, buffer, 0));

        p.reset();
        p.releaseResources();
    }
}

TEST_CASE ("Empty blocks and MIDI events are handled", "[robustness]")
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
}
