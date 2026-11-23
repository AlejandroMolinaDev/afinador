#include "plugin/Parameters.h"

#include "dsp/InputProfile.h"
#include "dsp/PitchUtils.h"
#include "dsp/Scales.h"

namespace novapitch
{

juce::String ParamIDs::noteState (int pitchClass)
{
    return "noteState" + juce::String (pitchClass);
}

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

template <size_t N>
juce::StringArray toStringArray (const std::array<const char*, N>& names)
{
    juce::StringArray out;
    for (const auto* n : names)
        out.add (u8 (n));
    return out;
}

/** Text -> choice index that also accepts UTF-8 text decoded as Latin-1 (the
    CLAP wrapper passes const char* straight to juce::String, which breaks
    accented names such as "Cromática"), case-insensitive matches and indices. */
int choiceIndexFromText (const juce::StringArray& choices, const juce::String& text)
{
    auto find = [&choices] (const juce::String& t) {
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].equalsIgnoreCase (t.trim()))
                return i;
        return -1;
    };

    if (const int i = find (text); i >= 0)
        return i;

    juce::MemoryBlock bytes;
    bool allBytes = true;
    for (auto c : text)
    {
        if (c > 0xff)
        {
            allBytes = false;
            break;
        }
        const auto b = static_cast<char> (c);
        bytes.append (&b, 1);
    }
    if (allBytes)
        if (const int i = find (juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), static_cast<int> (bytes.getSize()))); i >= 0)
            return i;

    if (text.trim().containsOnly ("0123456789"))
        return juce::jlimit (0, choices.size() - 1, text.getIntValue());
    return 0;
}

std::unique_ptr<juce::AudioParameterChoice> makeChoice (const juce::String& id, const char* name,
                                                        const juce::StringArray& choices, int defaultIndex)
{
    return std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id, 1 }, u8 (name), choices, defaultIndex,
        juce::AudioParameterChoiceAttributes().withValueFromStringFunction (
            [choices] (const juce::String& text) { return choiceIndexFromText (choices, text); }));
}

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const char* name, float lo, float hi,
                                                      float step, float def, const char* unit,
                                                      std::function<juce::String (float)> toText,
                                                      float skewCentre = -1.0f)
{
    juce::NormalisableRange<float> range (lo, hi, step);
    if (skewCentre > lo && skewCentre < hi)
        range.setSkewForCentre (skewCentre);
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, u8 (name), range, def,
        juce::AudioParameterFloatAttributes()
            .withLabel (u8 (unit))
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Phase 2 (order must never change) -------------------------------
    layout.add (makeChoice (ParamIDs::inputProfile, "Perfil de entrada", toStringArray (dsp::kInputProfileNames),
                            static_cast<int> (dsp::kDefaultInputProfile)));

    layout.add (makeFloat (ParamIDs::sensitivity, "Sensibilidad", 0.0f, 100.0f, 0.1f, kDefaultSensitivity, "%",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }));

    // ---- Phase 3 ------------------------------------------------------------
    layout.add (makeChoice (ParamIDs::qualityMode, "Modo", juce::StringArray { "Live", "Tracking", "HQ", "Vintage" },
                            static_cast<int> (kDefaultQualityMode)));

    layout.add (makeChoice (ParamIDs::key, "Tonalidad", toStringArray (dsp::kKeyNames), 0));

    layout.add (makeChoice (ParamIDs::scale, "Escala", toStringArray (dsp::kScaleNames), static_cast<int> (dsp::kDefaultScale)));

    layout.add (makeFloat (ParamIDs::referenceA4, "Referencia A4", 400.0f, 480.0f, 0.1f, dsp::kDefaultA4Hz, "Hz",
                           [] (float v) { return juce::String (v, 1) + " Hz"; }));

    layout.add (makeFloat (ParamIDs::retuneSpeed, "Velocidad", 0.0f, 400.0f, 0.1f, kDefaultRetuneMs, "ms",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, 60.0f));

    layout.add (makeFloat (ParamIDs::tolerance, "Tolerancia", 0.0f, 100.0f, 0.1f, 0.0f, "cents",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + u8 (" ¢"); }));

    layout.add (makeFloat (ParamIDs::naturalHold, "Sostenido natural", 0.0f, 100.0f, 0.1f, 0.0f, "%",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIDs::transpose, 1 }, u8 ("Transposición"), -12, 12, 0,
        juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int v, int) {
            return (v > 0 ? "+" : "") + juce::String (v) + " st";
        })));

    layout.add (makeFloat (ParamIDs::mix, "Mezcla", 0.0f, 100.0f, 0.1f, 100.0f, "%",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }));

    static constexpr std::array<const char*, 12> noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int pc = 0; pc < 12; ++pc)
        layout.add (makeChoice (ParamIDs::noteState (pc), (std::string ("Nota ") + noteNames[static_cast<size_t> (pc)]).c_str(),
                                toStringArray (dsp::kNoteStateNames), 0));

    // ---- Phase 4 ------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::formantPreserve, 1 }, u8 ("Preservar formantes"), true));

    layout.add (makeFloat (ParamIDs::formantShift, "Desplazar formantes", -12.0f, 12.0f, 0.1f, 0.0f, "st",
                           [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st"; }));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::formantLink, 1 }, u8 ("Enlazar formantes a transposición"), false));

    layout.add (makeFloat (ParamIDs::character, "Carácter", -50.0f, 50.0f, 0.1f, 0.0f, "%",
                           [] (float v) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; }));

    return layout;
}

} // namespace novapitch
