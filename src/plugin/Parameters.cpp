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

juce::String ParamIDs::harmonyVoice (const char* field, int voice)
{
    return juce::String (field) + juce::String (voice);
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
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
            .withValueFromStringFunction ([] (const juce::String& t) {
                // Accepts what toText produces ("8.0 kHz", "+3.5 dB", "40 %"...) and plain numbers.
                const auto number = t.trim().replaceCharacter (',', '.').retainCharacters ("0123456789.+-");
                const float v = number.getFloatValue();
                return t.containsIgnoreCase ("khz") ? v * 1000.0f : v;
            }));
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

    // ---- Phase 5 ------------------------------------------------------------
    auto percent = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    layout.add (makeFloat (ParamIDs::vibratoPreserve, "Conservar vibrato", 0.0f, 200.0f, 0.1f, 100.0f, "%", percent));
    layout.add (makeFloat (ParamIDs::vibRate, "Vibrato: velocidad", 0.1f, 10.0f, 0.01f, 5.5f, "Hz",
                           [] (float v) { return juce::String (v, 2) + " Hz"; }, 3.0f));
    layout.add (makeFloat (ParamIDs::vibDepth, "Vibrato: profundidad", 0.0f, 100.0f, 0.1f, 0.0f, "cents",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + u8 (" ¢"); }));
    layout.add (makeFloat (ParamIDs::vibDelay, "Vibrato: retardo", 0.0f, 2000.0f, 1.0f, 300.0f, "ms",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }, 400.0f));
    layout.add (makeFloat (ParamIDs::vibVariation, u8 ("Vibrato: variación").toRawUTF8(), 0.0f, 100.0f, 0.1f, 20.0f, "%", percent));
    layout.add (makeFloat (ParamIDs::vibAmpMod, "Vibrato: amplitud", 0.0f, 100.0f, 0.1f, 0.0f, "%", percent));
    layout.add (makeFloat (ParamIDs::vibFormantMod, "Vibrato: formante", 0.0f, 100.0f, 0.1f, 0.0f, "%", percent));

    // ---- Phase 7 ------------------------------------------------------------
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::midiTarget, 1 }, u8 ("MIDI: nota objetivo"), false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIDs::midiScaleLearn, 1 }, u8 ("MIDI: aprender escala"), false));

    auto degreeText = [] (int d) {
        if (d == 0)
            return u8 ("unísono");
        return juce::String (std::abs (d) + 1) + u8 ("ª ") + (d > 0 ? u8 ("arriba") : u8 ("abajo"));
    };
    struct VoiceDefaults { int degrees; int semitones; float pan; };
    const VoiceDefaults defaults[] = { { 2, 4, -40.0f }, { -2, -3, 40.0f }, { 4, 7, -70.0f }, { 7, 12, 70.0f } };

    for (int v = 1; v <= 4; ++v)
    {
        const auto& d = defaults[v - 1];
        const auto prefix = u8 ("Voz ") + juce::String (v) + ": ";
        auto id = [v] (const char* field) { return ParamIDs::harmonyVoice (field, v); };

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { id ("hvMode"), 1 }, prefix + u8 ("modo"),
            juce::StringArray { u8 ("Apagada"), u8 ("Diatónica"), u8 ("Semitonos"), "MIDI" }, 0,
            juce::AudioParameterChoiceAttributes().withValueFromStringFunction ([] (const juce::String& t) {
                const juce::StringArray items { "Apagada", "Diatonica", "Semitonos", "MIDI" };
                for (int i = 0; i < items.size(); ++i)
                    if (t.startsWithIgnoreCase (items[i].substring (0, 4)))
                        return i;
                return juce::jlimit (0, 3, t.getIntValue());
            })));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id ("hvDegrees"), 1 }, prefix + u8 ("intervalo"), -14, 14, d.degrees,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([degreeText] (int x, int) { return degreeText (x); })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    // "3ª arriba" -> +2, "6ª abajo" -> -5, "unísono" -> 0, plain numbers as degrees
                    const auto digits = t.retainCharacters ("0123456789");
                    if (digits.isEmpty())
                        return 0;
                    if (t.containsIgnoreCase ("arriba") || t.containsIgnoreCase ("abajo"))
                        return (digits.getIntValue() - 1) * (t.containsIgnoreCase ("abajo") ? -1 : 1);
                    return t.trim().getIntValue();
                })));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id ("hvSemitones"), 1 }, prefix + u8 ("semitonos"), -24, 24, d.semitones,
            juce::AudioParameterIntAttributes().withLabel ("st").withStringFromValueFunction ([] (int x, int) {
                return (x > 0 ? "+" : "") + juce::String (x) + " st";
            })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id ("hvFormant"), 1 }, prefix + u8 ("formante"), juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float x, int) {
                return (x > 0.05f ? "+" : "") + juce::String (x, 1) + " st";
            })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id ("hvPan"), 1 }, prefix + u8 ("panorama"), juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), d.pan,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float x, int) {
                    const int p = juce::roundToInt (x);
                    return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
                })
                .withValueFromStringFunction ([] (const juce::String& t) {
                    const auto txt = t.trim();
                    const float amount = txt.retainCharacters ("0123456789.").getFloatValue();
                    if (txt.startsWithIgnoreCase ("L"))
                        return -amount;
                    if (txt.startsWithIgnoreCase ("R"))
                        return amount;
                    return txt.getFloatValue(); // "C" -> 0, or a signed number
                })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id ("hvLevel"), 1 }, prefix + u8 ("nivel"), juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -6.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float x, int) {
                return juce::String (x, 1) + " dB";
            })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id ("hvHumanize"), 1 }, prefix + u8 ("humanizar"), juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float x, int) {
                return juce::String (juce::roundToInt (x)) + " %";
            })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("hvMute"), 1 }, prefix + "mute", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id ("hvSolo"), 1 }, prefix + "solo", false));
    }

    auto hz = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto ms = [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    auto db = [] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    layout.add (makeFloat (ParamIDs::hBusHpf, "Armonías: paso-alto", 20.0f, 1000.0f, 1.0f, 120.0f, "Hz", hz, 150.0f));
    layout.add (makeFloat (ParamIDs::hBusLpf, "Armonías: paso-bajo", 1000.0f, 20000.0f, 1.0f, 12000.0f, "Hz", hz, 6000.0f));
    layout.add (makeFloat (ParamIDs::hBusEqFreq, "Armonías: EQ frecuencia", 200.0f, 8000.0f, 1.0f, 2500.0f, "Hz", hz, 1500.0f));
    layout.add (makeFloat (ParamIDs::hBusEqGain, "Armonías: EQ ganancia", -12.0f, 12.0f, 0.1f, 0.0f, "dB", db));
    layout.add (makeFloat (ParamIDs::hBusAttack, "Armonías: ataque", 1.0f, 500.0f, 1.0f, 30.0f, "ms", ms, 60.0f));
    layout.add (makeFloat (ParamIDs::hBusRelease, "Armonías: liberación", 10.0f, 2000.0f, 1.0f, 200.0f, "ms", ms, 300.0f));
    layout.add (makeFloat (ParamIDs::hBusGate, "Armonías: gate", -80.0f, -20.0f, 0.1f, -60.0f, "dB", db));
    layout.add (makeFloat (ParamIDs::hBusWidth, "Armonías: ancho", 0.0f, 200.0f, 0.1f, 100.0f, "%",
                           [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }));

    return layout;
}

} // namespace novapitch
