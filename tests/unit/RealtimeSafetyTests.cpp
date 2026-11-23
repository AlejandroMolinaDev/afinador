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
    p.prepareToPlay (sr, 512);

    const auto sig = signals::concat ({ signals::sawtooth (233.0, sr, 0.6), signals::whiteNoise (sr, 0.2, 0.2f, 4u),
                                        signals::sawtooth (311.0, sr, 0.6) });
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
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
