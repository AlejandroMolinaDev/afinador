#pragma once

#include <juce_core/juce_core.h>

#include "dsp/graph/CorrectionTrack.h"
#include "plugin/graph/GraphModel.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace novapitch
{

/** Captured frames from the audio thread to the message thread (SPSC, lock-free).
    When the consumer falls behind, new frames are dropped: the audio thread never blocks. */
class CaptureFifo
{
public:
    static constexpr int kCapacity = 8192; // > 80 s of frames at 100 frames/s

    void push (const CaptureFrame& frame) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            buffer[static_cast<size_t> (scope.startIndex1)] = frame;
        else if (scope.blockSize2 > 0)
            buffer[static_cast<size_t> (scope.startIndex2)] = frame;
    }

    int pop (CaptureFrame* dest, int maxFrames) noexcept
    {
        const auto scope = fifo.read (maxFrames);
        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = buffer[static_cast<size_t> (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = buffer[static_cast<size_t> (scope.startIndex2 + i)];
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::vector<CaptureFrame> buffer = std::vector<CaptureFrame> (static_cast<size_t> (kCapacity)); // heap: see PitchFrameFifo
};

/** Hands immutable objects (compiled CorrectionTracks, rendered time edits)
    to the audio thread.

    The message thread publishes a new immutable object by swapping an atomic
    pointer. The previous one may still be in use by an audio block that
    started before the swap, so it goes to a graveyard and is deleted only once
    the audio thread has completed a block that started after the swap (the
    processor counts finished blocks), or when the audio is known to be stopped
    (prepareToPlay / releaseResources). The audio thread never allocates,
    frees or locks. */
template <typename T>
class Publisher
{
public:
    Publisher() = default;
    ~Publisher()
    {
        delete current.exchange (nullptr);
        for (auto& g : graveyard)
            delete g.object;
    }

    /** Message thread. `blocksDone` = audio blocks finished so far. nullptr clears. */
    void publish (std::unique_ptr<T> object, uint64_t blocksDone)
    {
        const T* old = current.exchange (object.release(), std::memory_order_acq_rel);
        if (old != nullptr)
            graveyard.push_back ({ old, blocksDone });
        collect (blocksDone);
    }

    /** Message thread: deletes retired objects no audio block can still be reading. */
    void collect (uint64_t blocksDone)
    {
        for (auto it = graveyard.begin(); it != graveyard.end();)
            if (blocksDone >= it->blocksAtRetire + 2)
            {
                delete it->object;
                it = graveyard.erase (it);
            }
            else
                ++it;
    }

    /** Only while the audio thread is guaranteed not to run (prepare/release). */
    void collectAll()
    {
        for (auto& g : graveyard)
            delete g.object;
        graveyard.clear();
    }

    /** Audio thread: load once per block and use that pointer for the whole block. */
    const T* acquire() const noexcept { return current.load (std::memory_order_acquire); }

    size_t graveyardSize() const noexcept { return graveyard.size(); }

private:
    struct Retired
    {
        const T* object;
        uint64_t blocksAtRetire;
    };

    std::atomic<const T*> current { nullptr };
    std::vector<Retired> graveyard;

    JUCE_DECLARE_NON_COPYABLE (Publisher)
};

using TrackPublisher = Publisher<dsp::CorrectionTrack>;

} // namespace novapitch
