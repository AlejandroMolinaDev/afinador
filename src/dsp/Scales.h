#pragma once

#include <array>
#include <cstdint>

namespace novapitch::dsp
{

/** Scale types. Values are stored in the plugin state (choice parameter
    "scale"): never reorder, only append. */
enum class ScaleType : int
{
    Major = 0,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Chromatic,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    Custom
};

inline constexpr int kNumScaleTypes = 14;
inline constexpr ScaleType kDefaultScale = ScaleType::Chromatic;

/** UTF-8 display names (Spanish UI), in enum order. */
inline constexpr std::array<const char*, kNumScaleTypes> kScaleNames {
    "Mayor", "Menor natural", "Menor armónica", "Menor melódica", "Cromática",
    "Dórica", "Frigia", "Lidia", "Mixolidia", "Locria",
    "Pentatónica mayor", "Pentatónica menor", "Blues", "Personalizada"
};

/** Key (tonic) names, pitch class 0 = C. */
inline constexpr std::array<const char*, 12> kKeyNames {
    "C", "C# / Db", "D", "D# / Eb", "E", "F", "F# / Gb", "G", "G# / Ab", "A", "A# / Bb", "B"
};

/** 12-bit mask of the scale degrees relative to the tonic (bit i = i semitones above). */
constexpr uint16_t scaleMask (ScaleType type) noexcept
{
    auto bits = [] (std::initializer_list<int> degrees) {
        uint16_t m = 0;
        for (int d : degrees)
            m = static_cast<uint16_t> (m | (1u << d));
        return m;
    };

    switch (type)
    {
        case ScaleType::Major:           return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case ScaleType::NaturalMinor:    return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case ScaleType::HarmonicMinor:   return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case ScaleType::MelodicMinor:    return bits ({ 0, 2, 3, 5, 7, 9, 11 });
        case ScaleType::Chromatic:       return 0x0fff;
        case ScaleType::Dorian:          return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case ScaleType::Phrygian:        return bits ({ 0, 1, 3, 5, 7, 8, 10 });
        case ScaleType::Lydian:          return bits ({ 0, 2, 4, 6, 7, 9, 11 });
        case ScaleType::Mixolydian:      return bits ({ 0, 2, 4, 5, 7, 9, 10 });
        case ScaleType::Locrian:         return bits ({ 0, 1, 3, 5, 6, 8, 10 });
        case ScaleType::PentatonicMajor: return bits ({ 0, 2, 4, 7, 9 });
        case ScaleType::PentatonicMinor: return bits ({ 0, 3, 5, 7, 10 });
        case ScaleType::Blues:           return bits ({ 0, 3, 5, 6, 7, 10 });
        case ScaleType::Custom:          return 0x0fff; // all notes, shaped by per-note states
    }
    return 0x0fff;
}

constexpr ScaleType scaleTypeFromIndex (int index) noexcept
{
    return (index >= 0 && index < kNumScaleTypes) ? static_cast<ScaleType> (index) : kDefaultScale;
}

/** Per-note override (keyboard). Values stored in state: append only. */
enum class NoteState : int
{
    FollowScale = 0, ///< in scale -> Active, out of scale -> Remove
    Active = 1,      ///< valid target, corrected
    Ignore = 2,      ///< valid target, original pitch passes uncorrected
    Remove = 3       ///< not a target: redirected to the nearest valid note
};

inline constexpr int kNumNoteStates = 4;
inline constexpr std::array<const char*, kNumNoteStates> kNoteStateNames { "Escala", "Activa", "Ignorar", "Eliminar" };

constexpr NoteState noteStateFromIndex (int index) noexcept
{
    return (index >= 0 && index < kNumNoteStates) ? static_cast<NoteState> (index) : NoteState::FollowScale;
}

} // namespace novapitch::dsp
