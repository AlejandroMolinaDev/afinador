#pragma once

#include <array>
#include <cmath>
#include <string>

namespace novapitch::dsp
{

inline constexpr float kDefaultA4Hz = 440.0f;

/** Frequency -> fractional MIDI note (69 = A4 at the given reference). */
inline float hzToMidi (float hz, float a4Hz = kDefaultA4Hz) noexcept
{
    return 69.0f + 12.0f * std::log2 (hz / a4Hz);
}

inline float midiToHz (float midi, float a4Hz = kDefaultA4Hz) noexcept
{
    return a4Hz * std::exp2 ((midi - 69.0f) / 12.0f);
}

/** Interval between two frequencies in cents (positive if a > b). */
inline float centsBetween (float a, float b) noexcept
{
    return 1200.0f * std::log2 (a / b);
}

/** Signed deviation (cents, -50..+50) of a fractional MIDI note from the nearest semitone. */
inline float centsFromNearestNote (float midi) noexcept
{
    return 100.0f * (midi - std::round (midi));
}

/** Note name with octave, e.g. 69 -> "A4", 61 -> "C#4". Sharps only. */
inline std::string midiNoteName (int midiNote)
{
    static constexpr std::array<const char*, 12> names { "C", "C#", "D", "D#", "E", "F",
                                                        "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midiNote % 12) + 12) % 12;
    const int octave = (midiNote - pc) / 12 - 1;
    return std::string (names[static_cast<size_t> (pc)]) + std::to_string (octave);
}

} // namespace novapitch::dsp
