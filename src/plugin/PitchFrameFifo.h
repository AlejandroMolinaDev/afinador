#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace novapitch
{

/** One pitch analysis frame as seen by the UI. */
struct PitchFrame
{
    double timeSeconds = 0.0;    ///< input-stream time since prepareToPlay
    float frequencyHz = 0.0f;    ///< detected f0; 0 when unvoiced
    float outputHz = 0.0f;       ///< f0 after correction + transposition; 0 when unvoiced
    float targetMidi = -1.0f;    ///< target note (relative to the A4 reference), -1 if none
    float correctionCents = 0.0f;
    float referenceA4 = 440.0f;
    float confidence = 0.0f;
    float rmsDb = -120.0f;
    bool voiced = false;
    bool correcting = false;     ///< false for "Ignore" notes / no target
};

/** Single-producer (audio thread) / single-consumer (message thread) lock-free
    queue. When the editor is closed the queue fills up and new frames are
    dropped: the audio thread never blocks. */
class PitchFrameFifo
{
public:
    static constexpr int kCapacity = 4096; // ~10 s at 375 frames/s

    void push (const PitchFrame& frame) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            buffer[static_cast<size_t> (scope.startIndex1)] = frame;
        else if (scope.blockSize2 > 0)
            buffer[static_cast<size_t> (scope.startIndex2)] = frame;
    }

    /** Pops up to maxFrames frames into dest; returns how many were read. */
    int pop (PitchFrame* dest, int maxFrames) noexcept
    {
        const auto scope = fifo.read (maxFrames);
        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = buffer[static_cast<size_t> (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = buffer[static_cast<size_t> (scope.startIndex2 + i)];
        return scope.blockSize1 + scope.blockSize2;
    }

    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<PitchFrame, kCapacity> buffer {};
};

} // namespace novapitch
