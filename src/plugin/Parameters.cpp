#include "plugin/Parameters.h"
#include "dsp/InputProfile.h"

namespace novapitch
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray profileNames;
    for (const auto* name : dsp::kInputProfileNames)
        profileNames.add (juce::String::fromUTF8 (name));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::inputProfile, 1 },
        juce::String::fromUTF8 ("Perfil de entrada"),
        profileNames,
        static_cast<int> (dsp::kDefaultInputProfile)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::sensitivity, 1 },
        juce::String::fromUTF8 ("Sensibilidad"),
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        kDefaultSensitivity,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));

    return layout;
}

} // namespace novapitch
