#pragma once

#include <array>

namespace novapitch::dsp
{

/** Input profiles: they bound the f0 search range of the PitchDetector.
    The numeric values are stored in the plugin state (choice parameter
    "inputProfile"): never reorder, only append. */
enum class InputProfile : int
{
    Soprano = 0,
    AltoTenor = 1,
    LowMale = 2,
    Instrument = 3,
    LowInstrument = 4
};

inline constexpr int kNumInputProfiles = 5;
inline constexpr InputProfile kDefaultInputProfile = InputProfile::AltoTenor;

struct F0Range
{
    float minHz;
    float maxHz;
};

constexpr F0Range getF0Range (InputProfile profile) noexcept
{
    switch (profile)
    {
        case InputProfile::Soprano:       return { 150.0f, 1200.0f };
        case InputProfile::AltoTenor:     return { 80.0f, 900.0f };
        case InputProfile::LowMale:       return { 60.0f, 500.0f };
        case InputProfile::Instrument:    return { 50.0f, 2000.0f };
        case InputProfile::LowInstrument: return { 30.0f, 400.0f };
    }
    return { 80.0f, 900.0f };
}

/** UTF-8 display names (Spanish UI), in enum order. */
inline constexpr std::array<const char*, kNumInputProfiles> kInputProfileNames {
    "Soprano", "Alto / Tenor", "Voz grave masculina", "Instrumento", "Instrumento grave"
};

constexpr InputProfile inputProfileFromIndex (int index) noexcept
{
    return (index >= 0 && index < kNumInputProfiles) ? static_cast<InputProfile> (index)
                                                     : kDefaultInputProfile;
}

} // namespace novapitch::dsp
