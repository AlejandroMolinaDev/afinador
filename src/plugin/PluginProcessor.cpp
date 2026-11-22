#include "plugin/PluginProcessor.h"
#include "plugin/Parameters.h"
#include "ui/PluginEditor.h"

namespace novapitch
{

namespace
{
void updateMin (std::atomic<int>& target, int value) noexcept
{
    const int current = target.load (std::memory_order_relaxed);
    if (current == 0 || value < current)
        target.store (value, std::memory_order_relaxed);
}

void updateMax (std::atomic<int>& target, int value) noexcept
{
    if (value > target.load (std::memory_order_relaxed))
        target.store (value, std::memory_order_relaxed);
}

/** Migrates a state tree from any older version to kStateVersion. */
juce::ValueTree migrateState (juce::ValueTree state)
{
    const int version = state.getProperty (kStateVersionProperty, 1);
    juce::ignoreUnused (version);
    // if (version < 2) { ... migrate 1 -> 2 ... }
    state.setProperty (kStateVersionProperty, kStateVersion, nullptr);
    return state;
}
} // namespace

//==============================================================================
NovaPitchProcessor::NovaPitchProcessor()
    // FL Studio mixer inserts are stereo, so Stereo->Stereo is the default layout.
    // Mono->Mono and Mono->Stereo are also accepted (see isBusesLayoutSupported).
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, juce::Identifier (kStateTreeType), createParameterLayout())
{
    apvts.state.setProperty (kStateVersionProperty, kStateVersion, nullptr);
}

//==============================================================================
void NovaPitchProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    diagnostics.sampleRate.store (sampleRate, std::memory_order_relaxed);
    diagnostics.maxBlockFromPrepare.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);
    diagnostics.numInputChannels.store (getTotalNumInputChannels(), std::memory_order_relaxed);
    diagnostics.numOutputChannels.store (getTotalNumOutputChannels(), std::memory_order_relaxed);
    diagnostics.resetBlockStats();

    // All DSP buffers will be allocated here in later phases (never in processBlock).
    updateReportedLatency();
}

void NovaPitchProcessor::releaseResources() {}

void NovaPitchProcessor::reset()
{
    // Clears DSP state (delay lines, detectors...) in later phases.
}

void NovaPitchProcessor::updateReportedLatency()
{
    const int latency = computeLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency); // notifies the host (PDC in FL Studio)
}

//==============================================================================
bool NovaPitchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    if (in == mono && (out == mono || out == stereo))
        return true;

    return in == stereo && out == stereo;
}

//==============================================================================
void NovaPitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // REAL-TIME THREAD: no allocation, no locks, no I/O, no logging.
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    diagnostics.processCalls.fetch_add (1, std::memory_order_relaxed);

    if (const int numMidi = midi.getNumEvents(); numMidi > 0)
        diagnostics.midiEventsReceived.fetch_add (static_cast<uint32_t> (numMidi), std::memory_order_relaxed);

    // FL Studio may deliver blocks of irregular size, including empty ones.
    if (numSamples <= 0)
        return;

    diagnostics.lastBlockSize.store (numSamples, std::memory_order_relaxed);
    updateMin (diagnostics.minBlockSize, numSamples);
    updateMax (diagnostics.maxBlockSize, numSamples);

    // Passthrough. Mono->Stereo: duplicate the mono input; any other extra
    // output channel is cleared so it never contains garbage.
    for (int ch = numIn; ch < numOut; ++ch)
    {
        if (numIn == 1)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
        else
            buffer.clear (ch, 0, numSamples);
    }
}

//==============================================================================
juce::AudioProcessorEditor* NovaPitchProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void NovaPitchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (kStateVersionProperty, kStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void NovaPitchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (apvts.state.getType()))
            return; // unknown / foreign data: keep current state

        apvts.replaceState (migrateState (juce::ValueTree::fromXml (*xml)));
    }
}

//==============================================================================
juce::String NovaPitchProcessor::getFormatName() const
{
   #if HAS_CLAP_JUCE_EXTENSIONS
    if (is_clap)
        return "CLAP";
   #endif

    switch (wrapperType)
    {
        case wrapperType_VST3:       return "VST3";
        case wrapperType_AudioUnit:  return "AU";
        case wrapperType_AAX:        return "AAX";
        case wrapperType_Standalone: return "Standalone";
        case wrapperType_LV2:        return "LV2";
        case wrapperType_Undefined:
        case wrapperType_VST:
        case wrapperType_AudioUnitv3:
        case wrapperType_Unity:      break;
    }
    return "Unknown";
}

} // namespace novapitch

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new novapitch::NovaPitchProcessor();
}
