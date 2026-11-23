#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace novapitch::dsp
{

/** Held MIDI notes (audio thread writes, UI reads a lock-free snapshot) and
    the pitch classes collected by "learn scale from MIDI". */
class MidiNoteState
{
public:
    void noteOn (int note) noexcept
    {
        if (note < 0 || note > 127)
            return;
        held[static_cast<size_t> (note)] = true;
        learnedMask.fetch_or (static_cast<uint16_t> (1u << (note % 12)));
        publish();
    }

    void noteOff (int note) noexcept
    {
        if (note < 0 || note > 127)
            return;
        held[static_cast<size_t> (note)] = false;
        publish();
    }

    void allOff() noexcept
    {
        held.fill (false);
        publish();
    }

    /** Held notes ascending into out (up to out.size()). Audio thread. */
    template <size_t N>
    int heldNotes (std::array<int, N>& out) const noexcept
    {
        int count = 0;
        for (int n = 0; n < 128 && count < static_cast<int> (N); ++n)
            if (held[static_cast<size_t> (n)])
                out[static_cast<size_t> (count++)] = n;
        return count;
    }

    /** Snapshot for the UI: bit n of the 128-bit set. */
    bool isHeldSnapshot (int note) const noexcept
    {
        const auto word = note < 64 ? snapshotLo.load (std::memory_order_relaxed) : snapshotHi.load (std::memory_order_relaxed);
        return ((word >> (note & 63)) & 1u) != 0;
    }

    // "Learn scale" (pitch classes played since the last clear)
    uint16_t getLearnedMask() const noexcept { return learnedMask.load(); }
    void clearLearned() noexcept { learnedMask.store (0); }

private:
    void publish() noexcept
    {
        uint64_t lo = 0, hi = 0;
        for (int n = 0; n < 64; ++n)
        {
            lo |= static_cast<uint64_t> (held[static_cast<size_t> (n)]) << n;
            hi |= static_cast<uint64_t> (held[static_cast<size_t> (n + 64)]) << n;
        }
        snapshotLo.store (lo, std::memory_order_relaxed);
        snapshotHi.store (hi, std::memory_order_relaxed);
    }

    std::array<bool, 128> held {};
    std::atomic<uint64_t> snapshotLo { 0 }, snapshotHi { 0 };
    std::atomic<uint16_t> learnedMask { 0 };
};

} // namespace novapitch::dsp
