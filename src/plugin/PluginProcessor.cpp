#include "plugin/PluginProcessor.h"
#include "dsp/PitchUtils.h"
#include "plugin/Parameters.h"
#include "ui/PluginEditor.h"

#include <cmath>

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

int choiceIndex (const std::atomic<float>* p) noexcept
{
    return juce::roundToInt (p->load (std::memory_order_relaxed));
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

    auto get = [this] (const juce::String& id) {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    params.inputProfile = get (ParamIDs::inputProfile);
    params.sensitivity = get (ParamIDs::sensitivity);
    params.qualityMode = get (ParamIDs::qualityMode);
    params.key = get (ParamIDs::key);
    params.scale = get (ParamIDs::scale);
    params.referenceA4 = get (ParamIDs::referenceA4);
    params.retuneSpeed = get (ParamIDs::retuneSpeed);
    params.tolerance = get (ParamIDs::tolerance);
    params.naturalHold = get (ParamIDs::naturalHold);
    params.transpose = get (ParamIDs::transpose);
    params.mix = get (ParamIDs::mix);
    for (int pc = 0; pc < 12; ++pc)
        params.noteStates[static_cast<size_t> (pc)] = get (ParamIDs::noteState (pc));
    params.formantPreserve = get (ParamIDs::formantPreserve);
    params.formantShift = get (ParamIDs::formantShift);
    params.formantLink = get (ParamIDs::formantLink);
    params.character = get (ParamIDs::character);
    params.vibratoPreserve = get (ParamIDs::vibratoPreserve);
    params.vibRate = get (ParamIDs::vibRate);
    params.vibDepth = get (ParamIDs::vibDepth);
    params.vibDelay = get (ParamIDs::vibDelay);
    params.vibVariation = get (ParamIDs::vibVariation);
    params.vibAmpMod = get (ParamIDs::vibAmpMod);
    params.vibFormantMod = get (ParamIDs::vibFormantMod);

    pitchDetector.setListener (this);
    presetManager = std::make_unique<PresetManager> (apvts);

    // Report the default mode's latency from the start (hosts query it before prepare).
    setLatencySamples (latencyForMode (kDefaultQualityMode, 44100.0));
    startTimerHz (30); // latency changes + MIDI Learn values
}

NovaPitchProcessor::~NovaPitchProcessor()
{
    stopTimer();
}

//==============================================================================
int NovaPitchProcessor::latencyForMode (QualityMode mode, double sampleRate)
{
    switch (mode)
    {
        case QualityMode::Live:
        case QualityMode::Vintage:
            return static_cast<int> (std::lround (kLiveLatencySeconds * sampleRate));
        case QualityMode::HQ:
            return dsp::HqShifter::computeLatency (sampleRate);
        case QualityMode::Tracking:
            break;
    }
    return static_cast<int> (std::lround (kTrackingLatencySeconds * sampleRate));
}

QualityMode NovaPitchProcessor::currentMode() const noexcept
{
    const int m = choiceIndex (params.qualityMode);
    return (m >= 0 && m < kNumQualityModes) ? static_cast<QualityMode> (m) : kDefaultQualityMode;
}

void NovaPitchProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    diagnostics.sampleRate.store (sampleRate, std::memory_order_relaxed);
    diagnostics.maxBlockFromPrepare.store (maximumExpectedSamplesPerBlock, std::memory_order_relaxed);
    diagnostics.numInputChannels.store (getTotalNumInputChannels(), std::memory_order_relaxed);
    diagnostics.numOutputChannels.store (getTotalNumOutputChannels(), std::memory_order_relaxed);
    diagnostics.resetBlockStats();

    // All DSP buffers are allocated here (never in processBlock).
    inputSampleRate = sampleRate;
    monoScratch.assign (static_cast<size_t> (kControlBlock), 0.0f);

    pitchDetector.prepare (sampleRate);
    correctionCurve.prepare (pitchDetector.getFrameRate());
    vibratoAnalyzer.prepare (pitchDetector.getFrameRate());
    vibratoSynth.prepare (pitchDetector.getFrameRate());
    synthNote = -1;
    wasVoiced = false;
    outputGain = outputGainTarget = 1.0f;
    noteMapper.reset();
    shifter.prepare (sampleRate, latencyForMode (QualityMode::Tracking, sampleRate) + 16);
    hqShifter.prepare (sampleRate, juce::jlimit (1, dsp::HqShifter::kMaxChannels, getTotalNumInputChannels()), kControlBlock);

    for (int m = 0; m < kNumQualityModes; ++m)
        modeLatency[static_cast<size_t> (m)] = m == static_cast<int> (QualityMode::HQ)
                                                   ? hqShifter.getLatency()
                                                   : latencyForMode (static_cast<QualityMode> (m), sampleRate);

    updateDspFromParameters();
    shifter.reset();
    hqShifter.reset();

    // prepareToPlay runs outside the audio callback: report latency right away.
    const int latency = modeLatency[static_cast<size_t> (blockMode)];
    desiredLatency.store (latency);
    if (latency != getLatencySamples())
        setLatencySamples (latency); // FL Studio uses this for plugin delay compensation
}

void NovaPitchProcessor::releaseResources() {}

void NovaPitchProcessor::reset()
{
    pitchDetector.reset();
    correctionCurve.reset();
    noteMapper.reset();
    shifter.reset();
    hqShifter.reset();
    vibratoAnalyzer.reset();
    vibratoSynth.reset();
    synthNote = -1;
    wasVoiced = false;
    outputGain = outputGainTarget = 1.0f;
}

void NovaPitchProcessor::timerCallback()
{
    const int latency = desiredLatency.load();
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    if (midiLearn.dispatchPending())
        ++midiMappingGeneration;
}

//==============================================================================
void NovaPitchProcessor::updateDspFromParameters() noexcept
{
    pitchDetector.setProfile (dsp::inputProfileFromIndex (choiceIndex (params.inputProfile)));
    pitchDetector.setSensitivity (params.sensitivity->load (std::memory_order_relaxed));
    correctionCurve.setFrameRate (pitchDetector.getFrameRate());
    vibratoAnalyzer.setFrameRate (pitchDetector.getFrameRate());
    vibratoSynth.setFrameRate (pitchDetector.getFrameRate());

    noteMapper.setKeyAndScale (choiceIndex (params.key), dsp::scaleTypeFromIndex (choiceIndex (params.scale)));
    for (int pc = 0; pc < 12; ++pc)
        noteMapper.setNoteState (pc, dsp::noteStateFromIndex (choiceIndex (params.noteStates[static_cast<size_t> (pc)])));

    blockMode = currentMode();
    a4Hz = params.referenceA4->load (std::memory_order_relaxed);
    transposeSemitones = static_cast<float> (choiceIndex (params.transpose));

    dsp::CorrectionCurve::Settings s;
    if (blockMode == QualityMode::Vintage)
    {
        // Classic hard tune: instant steps, no tolerance, no natural hold.
        s.retuneMs = 0.0f;
    }
    else
    {
        s.retuneMs = params.retuneSpeed->load (std::memory_order_relaxed);
        s.toleranceCents = params.tolerance->load (std::memory_order_relaxed);
        s.naturalHold = params.naturalHold->load (std::memory_order_relaxed) / 100.0f;
    }
    correctionCurve.setSettings (s);

    // Vibrato: Vintage flattens it like the classic effect.
    vibratoKeep = blockMode == QualityMode::Vintage ? 0.0f : params.vibratoPreserve->load (std::memory_order_relaxed) / 100.0f;
    dsp::VibratoSynth::Settings vs;
    vs.rateHz = params.vibRate->load (std::memory_order_relaxed);
    vs.depthCents = params.vibDepth->load (std::memory_order_relaxed);
    vs.delayMs = params.vibDelay->load (std::memory_order_relaxed);
    vs.variation = params.vibVariation->load (std::memory_order_relaxed) / 100.0f;
    vs.amplitudeMod = params.vibAmpMod->load (std::memory_order_relaxed) / 100.0f;
    vs.formantMod = params.vibFormantMod->load (std::memory_order_relaxed) / 100.0f;
    vibratoSynth.setSettings (vs);

    // Formants: shift x character (x transposition when linked). With "preserve"
    // off (and always in Vintage) the formants also follow the pitch ratio.
    formantPreserve = params.formantPreserve->load (std::memory_order_relaxed) > 0.5f && blockMode != QualityMode::Vintage;
    formantLink = params.formantLink->load (std::memory_order_relaxed) > 0.5f;
    formantStaticFactor = std::exp2 (params.formantShift->load (std::memory_order_relaxed) / 12.0f)
                          * (1.0f + params.character->load (std::memory_order_relaxed) / 100.0f)
                          * (formantPreserve && formantLink ? std::exp2 (transposeSemitones / 12.0f) : 1.0f);
    hqShifter.setFormant (formantStaticFactor, formantPreserve);
    hqShifter.setControlLead (pitchDetector.getDetectionLagSamples());
    shifter.setGrainScale (blockMode == QualityMode::Vintage ? kVintageGrainScale : 1.0f);

    const float mixValue = params.mix->load (std::memory_order_relaxed) / 100.0f;
    shifter.setMix (mixValue);
    hqShifter.setMix (mixValue);

    // Engine + latency (RT-safe; the host is told from the message thread).
    const bool wantHq = blockMode == QualityMode::HQ;
    if (wantHq != hqActive)
    {
        hqActive = wantHq;
        if (hqActive)
            hqShifter.reset();
        else
            shifter.reset();
    }

    const int latency = modeLatency[static_cast<size_t> (blockMode)];
    if (! hqActive && latency != shifter.getLatency())
        shifter.setLatency (latency); // fades in
    if (latency != desiredLatency.load (std::memory_order_relaxed))
        desiredLatency.store (latency);
}

void NovaPitchProcessor::pitchFrameReady (const dsp::PitchEstimate& e) noexcept
{
    const bool voiced = e.voiced && e.frequencyHz > 0.0f;
    const float detectedMidi = voiced ? dsp::hzToMidi (e.frequencyHz, a4Hz) : 0.0f;

    // Vibrato split: correction works on the centre; the oscillation is put back
    // scaled by "Conservar vibrato" (0 = remove, 1 = original, 2 = double).
    const auto vib = vibratoAnalyzer.process (voiced, detectedMidi * 100.0f);
    const float centreMidi = voiced ? vib.centreCents / 100.0f : 0.0f;

    dsp::NoteTarget target;
    if (voiced)
        target = noteMapper.map (centreMidi);
    else
        noteMapper.reset();

    const float correction = correctionCurve.process (voiced, centreMidi, target);
    const bool correcting = target.hasTarget && target.correct;
    const float vibratoScale = correcting ? (vibratoKeep - 1.0f) * vib.vibratoCents : 0.0f;

    // Artificial vibrato on sustained notes (restarts on every new note).
    const bool noteStarted = voiced && (! wasVoiced || target.note != synthNote);
    synthNote = voiced ? target.note : -1;
    wasVoiced = voiced;
    const auto synth = vibratoSynth.process (voiced, noteStarted);
    outputGainTarget = synth.gain;

    const float totalCents = correction + vibratoScale + synth.pitchCents + 100.0f * transposeSemitones;
    const float ratio = std::exp2 (totalCents / 1200.0f);
    const float formant = formantStaticFactor * synth.formantFactor;
    if (hqActive)
    {
        hqShifter.setControl (voiced, e.frequencyHz, ratio);
        hqShifter.setFormant (formant, formantPreserve);
    }
    else
    {
        const float period = voiced ? static_cast<float> (inputSampleRate / e.frequencyHz) : 0.0f;
        shifter.setControl (voiced, period, ratio);
        shifter.setFormantFactor (formantPreserve ? formant : formant * ratio);
    }

    PitchFrame frame;
    frame.timeSeconds = static_cast<double> (e.sampleIndex) / inputSampleRate;
    frame.frequencyHz = voiced ? e.frequencyHz : 0.0f;
    frame.outputHz = voiced ? e.frequencyHz * ratio : 0.0f;
    frame.targetMidi = target.hasTarget ? target.targetMidi : -1.0f;
    frame.correctionCents = correction;
    frame.referenceA4 = a4Hz;
    frame.confidence = e.confidence;
    frame.rmsDb = e.rmsDb;
    frame.voiced = voiced;
    frame.correcting = correcting;
    frame.vibratoRateHz = vib.rateHz;
    frame.vibratoDepthCents = vib.depthCents;
    frame.vibratoCents = vib.vibratoCents;
    pitchFifo.push (frame);
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
void NovaPitchProcessor::runChain (juce::AudioBuffer<float>& buffer, bool bypassed) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), dsp::PsolaShifter::kMaxChannels);
    const int numOut = getTotalNumOutputChannels();

    if (numIn <= 0 || monoScratch.empty())
    {
        buffer.clear();
        return;
    }

    if (bypassed)
    {
        // Smoothed to dry, still delayed by the latency (keeps FL's PDC aligned).
        shifter.setMix (0.0f);
        hqShifter.setMix (0.0f);
    }

    // Sanitise the input: a single NaN/Inf would otherwise poison filters and delay lines.
    for (int ch = 0; ch < numIn; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }

    const float gain = 1.0f / static_cast<float> (numIn);
    std::array<float*, dsp::PsolaShifter::kMaxChannels> channels {};

    for (int start = 0; start < numSamples; start += kControlBlock)
    {
        const int n = juce::jmin (kControlBlock, numSamples - start);

        juce::FloatVectorOperations::copyWithMultiply (monoScratch.data(), buffer.getReadPointer (0, start), gain, n);
        for (int ch = 1; ch < numIn; ++ch)
            juce::FloatVectorOperations::addWithMultiply (monoScratch.data(), buffer.getReadPointer (ch, start), gain, n);

        if (! bypassed)
        {
            pitchDetector.process (monoScratch.data(), n); // -> pitchFrameReady -> engine control
        }
        else
        {
            shifter.setControl (false, 0.0f, 1.0f);
            hqShifter.setControl (false, 0.0f, 1.0f);
        }

        for (int ch = 0; ch < numIn; ++ch)
            channels[static_cast<size_t> (ch)] = buffer.getWritePointer (ch, start);

        if (hqActive)
            hqShifter.process (channels.data(), numIn, n);
        else
            shifter.process (channels.data(), numIn, monoScratch.data(), n);

        // Vibrato amplitude modulation: linear ramp to the frame's gain (no zipper).
        const float target = bypassed ? 1.0f : outputGainTarget;
        if (! juce::exactlyEqual (target, outputGain))
        {
            const float step = (target - outputGain) / static_cast<float> (n);
            for (int ch = 0; ch < numIn; ++ch)
            {
                float g = outputGain;
                auto* d = channels[static_cast<size_t> (ch)];
                for (int i = 0; i < n; ++i)
                {
                    g += step;
                    d[i] *= g;
                }
            }
            outputGain = target;
        }
        else if (! juce::exactlyEqual (outputGain, 1.0f))
        {
            for (int ch = 0; ch < numIn; ++ch)
                juce::FloatVectorOperations::multiply (channels[static_cast<size_t> (ch)], outputGain, n);
        }
    }

    // Mono -> Stereo: duplicate; any other extra output channel is cleared.
    for (int ch = numIn; ch < numOut; ++ch)
    {
        if (numIn == 1)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
        else
            buffer.clear (ch, 0, numSamples);
    }

    // Never send NaN / Inf to the host.
    for (int ch = 0; ch < numOut; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }
}

void NovaPitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // REAL-TIME THREAD: no allocation, no locks, no I/O, no logging.
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    diagnostics.processCalls.fetch_add (1, std::memory_order_relaxed);

    if (const int numMidi = midi.getNumEvents(); numMidi > 0)
        diagnostics.midiEventsReceived.fetch_add (static_cast<uint32_t> (numMidi), std::memory_order_relaxed);

    // FL Studio may deliver blocks of irregular size, including empty ones.
    if (numSamples <= 0)
        return;

    diagnostics.lastBlockSize.store (numSamples, std::memory_order_relaxed);
    updateMin (diagnostics.minBlockSize, numSamples);
    updateMax (diagnostics.maxBlockSize, numSamples);

    midiLearn.processMidi (midi); // CC -> learned parameters (applied on the message thread)
    updateDspFromParameters();
    runChain (buffer, false);
}

void NovaPitchProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() <= 0)
        return;

    updateDspFromParameters();
    runChain (buffer, true);
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
    state.removeChild (state.getChildWithName (MidiLearn::kStateTag), nullptr);
    state.appendChild (midiLearn.toValueTree(), nullptr);

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
        midiLearn.fromValueTree (apvts.state.getChildWithName (MidiLearn::kStateTag));
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
