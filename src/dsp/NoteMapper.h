#pragma once

#include "dsp/Scales.h"

#include <array>
#include <cstddef>

namespace novapitch::dsp
{

/** Decision of the NoteMapper for one analysis frame. */
struct NoteTarget
{
    bool hasTarget = false;  ///< a valid note was found
    bool correct = false;    ///< true: pull to targetMidi; false: let the original pitch pass (Ignore)
    int note = -1;           ///< target MIDI note number (integer, relative to the A4 reference)
    float targetMidi = 0.0f; ///< note + its per-note detune (fractional MIDI)
};

/** Maps a detected pitch (fractional MIDI note, relative to the A4 reference)
    to a target note according to key, scale and per-note states, with
    hysteresis around the decision boundary to avoid flickering between two
    notes. Real-time safe, no allocation. */
class NoteMapper
{
public:
    NoteMapper();

    void setKeyAndScale (int keyPitchClass, ScaleType scale) noexcept;
    void setNoteState (int pitchClass, NoteState state) noexcept;
    /** Hysteresis in semitones beyond the midpoint before switching notes (default 0.15). */
    void setHysteresis (float semitones) noexcept { hysteresis = semitones; }

    /** Per-note detune (microtonal scales): every note of this pitch class is
        targeted `cents` away from equal temperament (clamped to ±50). The
        nearest-note decision uses the detuned positions too. */
    void setNoteDetune (int pitchClass, float cents) noexcept;
    /** Detune of a pitch class in semitones. */
    float getNoteDetune (int pitchClass) const noexcept { return detune[static_cast<std::size_t> (wrap (pitchClass))]; }
    /** Target position of a MIDI note: note + its pitch class detune. */
    float position (int midiNote) const noexcept { return static_cast<float> (midiNote) + getNoteDetune (midiNote); }

    /** Effective state of a pitch class after applying key, scale and overrides. */
    NoteState getEffectiveState (int pitchClass) const noexcept { return effective[static_cast<std::size_t> (wrap (pitchClass))]; }

    NoteTarget map (float detectedMidi) noexcept;
    void reset() noexcept { currentNote = -1; }

private:
    static int wrap (int pc) noexcept { return ((pc % 12) + 12) % 12; }
    void recompute() noexcept;
    bool isValid (int midiNote) const noexcept;
    int nearestValid (float midi) const noexcept;

    int key = 0;
    ScaleType scale = kDefaultScale;
    std::array<NoteState, 12> overrides {};
    std::array<NoteState, 12> effective {};
    std::array<float, 12> detune {}; // semitones
    float hysteresis = 0.15f;
    int currentNote = -1;
};

} // namespace novapitch::dsp
