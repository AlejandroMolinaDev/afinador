// Real-time safety: processBlock must not allocate (FL Studio's audio thread).
// The global operator new is replaced for the test executable and counts the
// allocations made on the current thread while a guard is active.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "TestHelpers.h"
#include "signals/SignalGenerators.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local bool countAllocations = false;
thread_local long allocationCount = 0;

void* countedAlloc (std::size_t size)
{
    if (countAllocations)
        ++allocationCount;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

struct AllocationGuard
{
    AllocationGuard() { allocationCount = 0; countAllocations = true; }
    ~AllocationGuard() { countAllocations = false; }
    long count() const { return allocationCount; }
};
} // namespace

void* operator new (std::size_t size) { return countedAlloc (size); }
void* operator new[] (std::size_t size) { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    if (countAllocations)
        ++allocationCount;
    return std::malloc (size == 0 ? 1 : size);
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    if (countAllocations)
        ++allocationCount;
    return std::malloc (size == 0 ? 1 : size);
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace novapitch;
using namespace novapitch::test;

TEST_CASE ("processBlock does not allocate in any mode", "[realtime]")
{
    JuceFixture fixture;
    const int mode = GENERATE (0, 1, 2, 3);
    const double sr = 48000.0;

    NovaPitchProcessor p;
    setParam (p, ParamIDs::qualityMode, static_cast<float> (mode));
    setParam (p, ParamIDs::transpose, 5.0f);
    setParam (p, ParamIDs::formantShift, 3.0f);
    setParam (p, ParamIDs::scale, 0.0f);
    setParam (p, ParamIDs::vibDepth, 30.0f);
    for (int v = 1; v <= 4; ++v) // all four harmony voices, every mode
        setParam (p, ParamIDs::harmonyVoice ("hvMode", v), static_cast<float> (1 + (v % 3)));
    setParam (p, ParamIDs::midiTarget, 1.0f);
    setParam (p, ParamIDs::midiOut, 1.0f);
    p.prepareToPlay (sr, 512);

    const auto sig = signals::concat ({ signals::sawtooth (233.0, sr, 0.6), signals::whiteNoise (sr, 0.2, 0.2f, 4u),
                                        signals::sawtooth (311.0, sr, 0.6) });
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (2048); // as every plugin wrapper does (VST3, AU, AAX)
    std::mt19937 rng (9u);
    std::uniform_int_distribution<int> sizes (1, 512);

    auto run = [&] (size_t from, size_t to) {
        for (size_t pos = from; pos < to;)
        {
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (sizes (rng)), to - pos));
            buffer.setSize (2, n, false, false, true); // no reallocation: keeps the 512-sample storage
            buffer.copyFrom (0, 0, sig.data() + pos, n);
            buffer.copyFrom (1, 0, sig.data() + pos, n);
            midi.clear();
            if ((pos / 512) % 7 == 0) // MIDI chords in and out
                midi.addEvent (juce::MidiMessage::noteOn (1, 60 + static_cast<int> (pos % 12), 0.8f), 0);
            if ((pos / 512) % 7 == 3)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p.processBlock (buffer, midi);
            pos += static_cast<size_t> (n);
        }
    };

    run (0, sig.size() / 4); // warm-up (first blocks may touch lazily initialised state)

    long allocations = 0;
    {
        AllocationGuard guard;
        run (sig.size() / 4, sig.size());
        allocations = guard.count();
    }
    INFO ("mode " << mode << ": " << allocations << " allocations on the audio thread");
    CHECK (allocations == 0);
}

TEST_CASE ("processBlock does not allocate in graphical mode (playback + capture)", "[realtime][graph]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    NovaPitchProcessor p;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("NovaPitchRealtime-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    p.setCaptureRoot (root); // never write into the user's capture folder
    TestPlayHead playHead;
    playHead.sampleRate = sr;
    p.setPlayHead (&playHead);

    auto& model = p.getGraphModel();
    model.addNote (0.0, 0.5, 58.0f);
    model.addLine (0.5, 0.9, 58.0f, 63.0f);
    model.addCurve ({ { 0.9, 63.0f }, { 1.0, 63.5f }, { 1.2, 62.0f } });
    setParam (p, ParamIDs::graphMode, 1.0f);
    setParam (p, ParamIDs::harmonyVoice ("hvMode", 1), 1.0f);
    p.setGraphCapture (true);
    p.prepareToPlay (sr, 512);

    const auto sig = signals::concat ({ signals::sawtooth (233.0, sr, 0.7), signals::sawtooth (311.0, sr, 0.7) });
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (5u);
    std::uniform_int_distribution<int> sizes (1, 512);
    auto run = [&] (size_t from, size_t to) {
        for (size_t pos = from; pos < to;)
        {
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (sizes (rng)), to - pos));
            buffer.setSize (2, n, false, false, true);
            buffer.copyFrom (0, 0, sig.data() + pos, n);
            buffer.copyFrom (1, 0, sig.data() + pos, n);
            playHead.timeInSamples = static_cast<int64_t> (pos);
            p.processBlock (buffer, midi);
            pos += static_cast<size_t> (n);
        }
    };

    run (0, sig.size() / 4);
    long allocations = 0;
    {
        AllocationGuard guard;
        run (sig.size() / 4, sig.size());
        allocations = guard.count();
    }
    INFO (allocations << " allocations on the audio thread");
    CHECK (allocations == 0);
    p.setPlayHead (nullptr);
    p.setGraphCapture (false);
    p.flushGraphCapture();
    root.deleteRecursively();
}

TEST_CASE ("processBlock does not allocate while playing time edits", "[realtime][time]")
{
    JuceFixture fixture;
    const double sr = 48000.0;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("NovaPitchRealtime-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    NovaPitchProcessor p;
    p.setCaptureRoot (root);
    TestPlayHead playHead;
    playHead.sampleRate = sr;
    p.setPlayHead (&playHead);
    p.prepareToPlay (sr, 512);

    const auto sig = signals::concat ({ signals::sawtooth (233.0, sr, 0.8), signals::silence (sr, 0.1),
                                        signals::sawtooth (311.0, sr, 0.8) });
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (11u);
    std::uniform_int_distribution<int> sizes (1, 512);
    auto run = [&] {
        for (size_t pos = 0; pos < sig.size();)
        {
            const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (sizes (rng)), sig.size() - pos));
            buffer.setSize (2, n, false, false, true);
            buffer.copyFrom (0, 0, sig.data() + pos, n);
            buffer.copyFrom (1, 0, sig.data() + pos, n);
            playHead.timeInSamples = static_cast<int64_t> (pos);
            p.processBlock (buffer, midi);
            pos += static_cast<size_t> (n);
        }
    };

    // Capture pass (warm-up), then a time edit rendered and played.
    p.setGraphCapture (true);
    run();
    p.setGraphCapture (false);
    p.flushGraphCapture();
    REQUIRE (p.getGraphModel().getAudioPasses().size() == 1);
    p.importGraphFromAuto();
    dsp::TimeMap map;
    map.anchors = { { 0.2, 0.2 }, { 0.65, 0.6 }, { 1.2, 1.2 } };
    p.getGraphModel().setTimeMap (map);
    setParam (p, ParamIDs::graphMode, 1.0f);
    p.renderTimeEditsNow();

    long allocations = 0;
    {
        AllocationGuard guard;
        run();
        allocations = guard.count();
    }
    INFO (allocations << " allocations on the audio thread");
    CHECK (allocations == 0);
    p.setPlayHead (nullptr);
    root.deleteRecursively();
}

TEST_CASE ("The allocation counter detects allocations (self-test)", "[realtime]")
{
    long allocations = 0;
    {
        AllocationGuard guard;
        auto* v = new std::vector<float> (1024);
        delete v;
        juce::String s ("this string is long enough to need heap storage for sure");
        allocations = guard.count();
    }
    CHECK (allocations >= 2);
}
