#include "plugin/Parameters.h"

namespace novapitch
{

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Intentionally empty in phase 1 (passthrough skeleton).
    return layout;
}

} // namespace novapitch
