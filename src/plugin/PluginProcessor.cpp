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
    {
        params.noteStates[static_cast<size_t> (pc)] = get (ParamIDs::noteState (pc));
        params.noteDetunes[static_cast<size_t> (pc)] = get (ParamIDs::noteDetune (pc));
    }
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
    params.midiTarget = get (ParamIDs::midiTarget);
    params.midiScaleLearn = get (ParamIDs::midiScaleLearn);
    for (int v = 0; v < dsp::HarmonyEngine::kVoices; ++v)
    {
        auto id = [v] (const char* field) { return ParamIDs::harmonyVoice (field, v + 1); };
        params.voices[static_cast<size_t> (v)] = { get (id ("hvMode")), get (id ("hvDegrees")), get (id ("hvSemitones")),
                                                   get (id ("hvFormant")), get (id ("hvPan")), get (id ("hvLevel")),
                                                   get (id ("hvHumanize")), get (id ("hvMute")), get (id ("hvSolo")) };
    }
    params.busHpf = get (ParamIDs::hBusHpf);
    params.busLpf = get (ParamIDs::hBusLpf);
    params.busEqFreq = get (ParamIDs::hBusEqFreq);
    params.busEqGain = get (ParamIDs::hBusEqGain);
    params.busAttack = get (ParamIDs::hBusAttack);
    params.busRelease = get (ParamIDs::hBusRelease);
    params.busGate = get (ParamIDs::hBusGate);
    params.busWidth = get (ParamIDs::hBusWidth);
    params.graphMode = get (ParamIDs::graphMode);
    params.midiOut = get (ParamIDs::midiOut);
    params.midiTargetExact = get (ParamIDs::midiTargetExact);
    params.vibShape = get (ParamIDs::vibShape);

    pitchDetector.setListener (this);
    presetManager = std::make_unique<PresetManager> (apvts);

    graphModel.onChanged = [this] { publishGraphTrack(); };
    graphModel.bind (apvts.state);
    InstanceRegistry::getInstance().add (this);

    // Report the default mode's latency from the start (hosts query it before prepare).
    setLatencySamples (latencyForMode (kDefaultQualityMode, 44100.0));
    startTimerHz (30); // latency changes + MIDI Learn values
}

NovaPitchProcessor::~NovaPitchProcessor()
{
    InstanceRegistry::getInstance().remove (this);
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

    // Harmony voices use PSOLA with the lead's latency (up to the HQ one).
    harmony.prepare (sampleRate, latencyForMode (QualityMode::HQ, sampleRate) + 16, kControlBlock, pitchDetector.getFrameRate());

    for (int m = 0; m < kNumQualityModes; ++m)
        modeLatency[static_cast<size_t> (m)] = m == static_cast<int> (QualityMode::HQ)
                                                   ? hqShifter.getLatency()
                                                   : latencyForMode (static_cast<QualityMode> (m), sampleRate);

    updateDspFromParameters();
    shifter.reset();
    hqShifter.reset();
    harmony.reset();
    midiNotes.allOff();
    trackPublisher.collectAll(); // the audio thread is not running here
    timePublisher.collectAll();
    lastCaptureTime = -1.0e9;
    audioCapture.prepare (sampleRate, juce::jlimit (1, AudioCapture::kMaxChannels, getTotalNumInputChannels()));

    // prepareToPlay runs outside the audio callback: report latency right away.
    const int latency = modeLatency[static_cast<size_t> (blockMode)];
    desiredLatency.store (latency);
    if (latency != getLatencySamples())
        setLatencySamples (latency); // FL Studio uses this for plugin delay compensation
}

void NovaPitchProcessor::releaseResources()
{
    trackPublisher.collectAll();
    timePublisher.collectAll();
}

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
    midiOutNote = midiOutCandidate = -1;
    numMidiOutEvents = 0;
}

void NovaPitchProcessor::timerCallback()
{
    const int latency = desiredLatency.load();
    if (latency != getLatencySamples())
        setLatencySamples (latency);

    if (midiLearn.dispatchPending())
        ++midiMappingGeneration;

    applyLearnedScale();
    processGraphMessages();
    applyPendingSync();
    if (keyListening.load() && keyVoicedSeconds.load() >= keyListenTarget)
        finishKeyListening (true);
}

//==============================================================================
void NovaPitchProcessor::setParameterValue (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void NovaPitchProcessor::startKeyListening (double voicedSeconds)
{
    keyListening.store (false);
    for (auto& bin : keyChroma)
        bin.store (0.0f);
    keyVoicedSeconds.store (0.0);
    keyListenTarget = std::max (1.0, voicedSeconds);
    keyListening.store (true);
}

dsp::KeyDetector::Result NovaPitchProcessor::finishKeyListening (bool apply)
{
    keyListening.store (false);
    dsp::KeyDetector::Chroma chroma {};
    for (size_t i = 0; i < 12; ++i)
        chroma[i] = keyChroma[i].load();
    auto result = dsp::KeyDetector::estimate (chroma);
    result.valid = result.valid && keyVoicedSeconds.load() >= 1.0; // too little material otherwise
    lastKeyResult = result;
    ++keyResultGeneration;
    if (apply && result.valid)
        applyKey (result);
    return result;
}

dsp::KeyDetector::Result NovaPitchProcessor::detectKeyFromCapture (bool apply)
{
    dsp::KeyDetector detector;
    double voiced = 0.0;
    const auto& frames = graphModel.getCaptured();
    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto& f = frames[i];
        if (f.midi < 0.0f)
            continue;
        const double next = i + 1 < frames.size() ? frames[i + 1].time : f.time + GraphModel::kFrameSeconds;
        const auto seconds = static_cast<float> (std::clamp (next - f.time, 0.0, 0.05));
        detector.add (f.midi, dsp::KeyDetector::frameWeight (f.midi, seconds, f.rmsDb));
        voiced += seconds;
    }
    auto result = detector.estimate();
    result.valid = result.valid && voiced >= 1.0;
    lastKeyResult = result;
    ++keyResultGeneration;
    if (apply && result.valid)
        applyKey (result);
    return result;
}

void NovaPitchProcessor::applyKey (const dsp::KeyDetector::Result& r)
{
    setParameterValue (ParamIDs::key, static_cast<float> (r.key));
    setParameterValue (ParamIDs::scale, static_cast<float> (r.scale()));
}

int NovaPitchProcessor::syncToOtherInstances()
{
    SongSettings s;
    s.key = choiceIndex (params.key);
    s.scale = choiceIndex (params.scale);
    for (size_t pc = 0; pc < 12; ++pc)
    {
        s.noteStates[pc] = choiceIndex (params.noteStates[pc]);
        s.noteDetuneCents[pc] = params.noteDetunes[pc]->load();
    }
    s.referenceA4 = params.referenceA4->load();
    return InstanceRegistry::getInstance().broadcast (s, this);
}

void NovaPitchProcessor::receiveSongSettings (const SongSettings& s)
{
    const juce::SpinLock::ScopedLockType lock (syncLock);
    pendingSync = s;
    hasPendingSync.store (true);
}

void NovaPitchProcessor::applyPendingSync()
{
    if (! hasPendingSync.exchange (false))
        return;
    SongSettings s;
    {
        const juce::SpinLock::ScopedLockType lock (syncLock);
        s = pendingSync;
    }
    setParameterValue (ParamIDs::key, static_cast<float> (s.key));
    setParameterValue (ParamIDs::scale, static_cast<float> (s.scale));
    for (int pc = 0; pc < 12; ++pc)
    {
        setParameterValue (ParamIDs::noteState (pc), static_cast<float> (s.noteStates[static_cast<size_t> (pc)]));
        setParameterValue (ParamIDs::noteDetune (pc), s.noteDetuneCents[static_cast<size_t> (pc)]);
    }
    setParameterValue (ParamIDs::referenceA4, s.referenceA4);
}

//==============================================================================
void NovaPitchProcessor::publishGraphTrack()
{
    trackPublisher.publish (graphModel.compile(), audioBlocksDone.load());
}

void NovaPitchProcessor::setGraphCapture (bool shouldCapture)
{
    if (shouldCapture)
        audioCapture.setDirectory (captureRoot.getChildFile (graphModel.getAudioId (true)));
    captureRequested.store (shouldCapture);
    if (! shouldCapture)
        processGraphMessages(); // drain + commit the pass now
}

juce::File NovaPitchProcessor::defaultCaptureRoot()
{
   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #elif JUCE_MAC
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    const auto base = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local").getChildFile ("share");
   #endif
    return base.getChildFile ("NovaPitch").getChildFile ("Captures");
}

void NovaPitchProcessor::clearGraphCapture()
{
    graphModel.clearCaptured();
    const auto id = graphModel.getAudioId (false);
    const auto dir = id.isNotEmpty() ? captureRoot.getChildFile (id) : juce::File();
    for (const auto& pass : graphModel.removeAudioPasses())
        if (dir != juce::File())
            dir.getChildFile (pass.fileName).deleteFile();
    if (dir.isDirectory() && dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        dir.deleteFile();
}

void NovaPitchProcessor::flushGraphCapture()
{
    audioCapture.endPass();
    audioCapture.flush();
    processGraphMessages();
}

TimeRenderer::Job NovaPitchProcessor::makeTimeJob()
{
    TimeRenderer::Job job;
    job.map = graphModel.getTimeMap();
    job.passes = graphModel.getAudioPasses();
    const auto id = graphModel.getAudioId (false);
    job.directory = id.isNotEmpty() ? captureRoot.getChildFile (id) : juce::File();
    job.sampleRate = inputSampleRate;
    job.channels = juce::jlimit (1, 2, getTotalNumInputChannels());
    job.id = ++timeJobCounter;
    return job;
}

void NovaPitchProcessor::renderTimeEditsNow()
{
    timeKey = { graphModel.getTimeMapGeneration(), graphModel.getAudioGeneration(), inputSampleRate, getTotalNumInputChannels() };
    timeDirty = false;
    auto job = makeTimeJob();
    auto rendered = TimeRenderer::render (job);
    timeAudioMissing = rendered != nullptr && rendered->audioMissing;
    timePublisher.publish (std::move (rendered), audioBlocksDone.load());
}

void NovaPitchProcessor::updateTimeRender()
{
    const auto key = std::make_tuple (graphModel.getTimeMapGeneration(), graphModel.getAudioGeneration(), inputSampleRate,
                                      getTotalNumInputChannels());
    const auto now = juce::Time::getMillisecondCounter();
    if (key != timeKey)
    {
        timeKey = key;
        timeDirty = true;
        timeChangedAt = now;
    }
    if (timeDirty && now - timeChangedAt >= 150) // debounce while dragging
    {
        timeDirty = false;
        if (graphModel.getTimeMap().isIdentity())
        {
            ++timeJobCounter; // drop any render in flight
            timeAudioMissing = false;
            timePublisher.publish (nullptr, audioBlocksDone.load());
        }
        else
            timeRenderer.request (makeTimeJob());
    }

    int id = -1;
    if (auto rendered = timeRenderer.takeResult (id); id == timeJobCounter)
    {
        timeAudioMissing = rendered != nullptr && rendered->audioMissing;
        timePublisher.publish (std::move (rendered), audioBlocksDone.load());
    }
    timePublisher.collect (audioBlocksDone.load());
}

void NovaPitchProcessor::processGraphMessages()
{
    std::array<CaptureFrame, 256> frames {};
    for (int n; (n = captureFifo.pop (frames.data(), static_cast<int> (frames.size()))) > 0;)
        for (int i = 0; i < n; ++i)
        {
            const auto& f = frames[static_cast<size_t> (i)];
            if (f.time < lastDrainedTime - 0.25)
                graphModel.commitCapture(); // the host jumped back (loop / relocation): new pass
            graphModel.appendCaptured (&f, 1);
            lastDrainedTime = f.time;
        }

    const bool capturing = captureRequested.load();
    const bool playing = hostPlaying.load();
    if ((wasCapturing && ! capturing) || (wasHostPlaying && ! playing))
    {
        graphModel.commitCapture();
        audioCapture.endPass();
        lastDrainedTime = -1.0e9;
    }
    wasCapturing = capturing;
    wasHostPlaying = playing;

    // Captured audio: folder of this instance, finished passes, time render.
    if (const auto id = graphModel.getAudioId (false); id.isNotEmpty())
        if (const auto dir = captureRoot.getChildFile (id); dir != audioCapture.getDirectory())
            audioCapture.setDirectory (dir);
    if (auto passes = audioCapture.takeFinishedPasses(); ! passes.empty())
        graphModel.addAudioPasses (passes);
    updateTimeRender();

    trackPublisher.collect (audioBlocksDone.load());
}

int NovaPitchProcessor::importGraphFromAuto (int detail)
{
    dsp::NoteMapper mapper;
    mapper.setKeyAndScale (choiceIndex (params.key), dsp::scaleTypeFromIndex (choiceIndex (params.scale)));
    for (int pc = 0; pc < 12; ++pc)
    {
        mapper.setNoteState (pc, dsp::noteStateFromIndex (choiceIndex (params.noteStates[static_cast<size_t> (pc)])));
        mapper.setNoteDetune (pc, params.noteDetunes[static_cast<size_t> (pc)]->load());
    }
    // detail: 0 = fewer, longer notes; 1 = normal; 2 = more, shorter notes (fast runs, ornaments)
    static constexpr double minNote[] = { 0.15, 0.08, 0.04 };
    static constexpr double smooth[] = { 0.08, 0.04, 0.015 };
    const auto d = static_cast<size_t> (juce::jlimit (0, 2, detail));
    if (detail >= 2)
        mapper.setHysteresis (0.08f);
    else if (detail <= 0)
        mapper.setHysteresis (0.25f);
    return graphModel.importFromAuto (mapper, minNote[d], smooth[d]);
}

int NovaPitchProcessor::importGraphAsCurves()
{
    return graphModel.importAsCurves();
}

void NovaPitchProcessor::readPlayHead() noexcept
{
    blockPlaying = false;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition(); position.hasValue() && position->getIsPlaying())
        {
            if (const auto samples = position->getTimeInSamples(); samples.hasValue())
            {
                blockHostSample = *samples;
                blockHostTime = static_cast<double> (*samples) / inputSampleRate;
                blockPlaying = true;
            }
            else if (const auto seconds = position->getTimeInSeconds(); seconds.hasValue())
            {
                blockHostTime = *seconds;
                blockHostSample = static_cast<int64_t> (std::llround (*seconds * inputSampleRate));
                blockPlaying = true;
            }
        }
    }
    hostPlaying.store (blockPlaying, std::memory_order_relaxed);
    if (blockPlaying)
        hostTimeSeconds.store (blockHostTime, std::memory_order_relaxed);
    blockTrack = trackPublisher.acquire();
    blockTime = timePublisher.acquire();
}

void NovaPitchProcessor::applyLearnedScale()
{
    const bool learning = params.midiScaleLearn->load() > 0.5f;
    if (learning && ! wasLearningScale)
    {
        midiNotes.clearLearned(); // a new learning pass starts empty
        appliedLearnedMask = 0;
    }
    wasLearningScale = learning;
    if (! learning)
        return;

    const auto mask = midiNotes.getLearnedMask();
    if (mask == 0 || mask == appliedLearnedMask)
        return;
    appliedLearnedMask = mask;

    auto set = [this] (const juce::String& id, float value) {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ParamIDs::scale, static_cast<float> (dsp::ScaleType::Custom));
    for (int pc = 0; pc < 12; ++pc)
        set (ParamIDs::noteState (pc), static_cast<float> ((mask >> pc) & 1u ? dsp::NoteState::Active : dsp::NoteState::Remove));
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
    {
        noteMapper.setNoteState (pc, dsp::noteStateFromIndex (choiceIndex (params.noteStates[static_cast<size_t> (pc)])));
        noteMapper.setNoteDetune (pc, params.noteDetunes[static_cast<size_t> (pc)]->load (std::memory_order_relaxed));
    }

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
    autoCurveSettings = s;
    graphModeOn = params.graphMode->load (std::memory_order_relaxed) > 0.5f;

    // Vibrato: Vintage flattens it like the classic effect.
    vibratoKeep = blockMode == QualityMode::Vintage ? 0.0f : params.vibratoPreserve->load (std::memory_order_relaxed) / 100.0f;
    dsp::VibratoSynth::Settings vs;
    vs.rateHz = params.vibRate->load (std::memory_order_relaxed);
    vs.depthCents = params.vibDepth->load (std::memory_order_relaxed);
    vs.delayMs = params.vibDelay->load (std::memory_order_relaxed);
    vs.variation = params.vibVariation->load (std::memory_order_relaxed) / 100.0f;
    vs.amplitudeMod = params.vibAmpMod->load (std::memory_order_relaxed) / 100.0f;
    vs.formantMod = params.vibFormantMod->load (std::memory_order_relaxed) / 100.0f;
    vs.shape = choiceIndex (params.vibShape);
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
    harmony.setLatency (latency);

    // MIDI + harmonies
    midiTargetOn = params.midiTarget->load (std::memory_order_relaxed) > 0.5f;
    midiTargetExactOn = params.midiTargetExact->load (std::memory_order_relaxed) > 0.5f;
    midiOutOn = params.midiOut->load (std::memory_order_relaxed) > 0.5f;
    for (int v = 0; v < dsp::HarmonyEngine::kVoices; ++v)
    {
        const auto& pv = params.voices[static_cast<size_t> (v)];
        dsp::HarmonyVoiceSettings hv;
        hv.mode = static_cast<dsp::HarmonyMode> (juce::jlimit (0, dsp::kNumHarmonyModes - 1, choiceIndex (pv.mode)));
        hv.degrees = choiceIndex (pv.degrees);
        hv.semitones = choiceIndex (pv.semitones);
        hv.formantSt = pv.formant->load (std::memory_order_relaxed);
        hv.pan = pv.pan->load (std::memory_order_relaxed) / 100.0f;
        hv.levelDb = pv.level->load (std::memory_order_relaxed);
        hv.humanize = pv.humanize->load (std::memory_order_relaxed) / 100.0f;
        hv.mute = pv.mute->load (std::memory_order_relaxed) > 0.5f;
        hv.solo = pv.solo->load (std::memory_order_relaxed) > 0.5f;
        harmony.setVoice (v, hv);
    }
    dsp::HarmonyBusSettings hb;
    hb.hpfHz = params.busHpf->load (std::memory_order_relaxed);
    hb.lpfHz = params.busLpf->load (std::memory_order_relaxed);
    hb.eqFreqHz = params.busEqFreq->load (std::memory_order_relaxed);
    hb.eqGainDb = params.busEqGain->load (std::memory_order_relaxed);
    hb.attackMs = params.busAttack->load (std::memory_order_relaxed);
    hb.releaseMs = params.busRelease->load (std::memory_order_relaxed);
    hb.gateDb = params.busGate->load (std::memory_order_relaxed);
    hb.width = params.busWidth->load (std::memory_order_relaxed) / 100.0f;
    harmony.setBus (hb);
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

    std::array<int, 16> held {};
    const int numHeld = midiNotes.heldNotes (held);

    // Key detection: accumulate the chromagram while listening (single writer).
    if (voiced && keyListening.load (std::memory_order_relaxed))
    {
        const auto frameSeconds = static_cast<float> (1.0 / pitchDetector.getFrameRate());
        const float w = dsp::KeyDetector::frameWeight (detectedMidi, frameSeconds, e.rmsDb);
        auto& bin = keyChroma[static_cast<size_t> (((static_cast<int> (std::lround (detectedMidi)) % 12) + 12) % 12)];
        bin.store (bin.load (std::memory_order_relaxed) + w, std::memory_order_relaxed);
        keyVoicedSeconds.store (keyVoicedSeconds.load (std::memory_order_relaxed) + frameSeconds, std::memory_order_relaxed);
    }

    // Host-timeline time of the analysed audio (capture and graphical playback).
    const double frameTime = chunkHostTime
                             + static_cast<double> (e.sampleIndex - chunkStartIndex - pitchDetector.getDetectionLagSamples())
                                   / inputSampleRate;
    if (blockPlaying && captureRequested.load (std::memory_order_relaxed)
        && (frameTime - lastCaptureTime >= GraphModel::kFrameSeconds - 1.0e-4 || frameTime < lastCaptureTime
            || voiced != lastCaptureVoiced))
    {
        captureFifo.push ({ frameTime, voiced ? detectedMidi : -1.0f, e.rmsDb });
        lastCaptureTime = frameTime;
        lastCaptureVoiced = voiced;
    }

    // Graphical mode: the objects under the playhead decide; elsewhere (or when
    // the host is stopped) the original voice passes uncorrected.
    dsp::CorrectionTrack::Result graph;
    const bool graphOverride = graphModeOn && blockPlaying && blockTrack != nullptr && blockTrack->lookup (frameTime, graph);
    float frameVibratoKeep = vibratoKeep;
    float graphFormant = 1.0f;

    dsp::NoteTarget target;
    if (graphModeOn)
    {
        auto cs = autoCurveSettings;
        if (graphOverride && voiced)
        {
            const int note = static_cast<int> (std::lround (graph.pitch));
            target = { true, true, note, graph.pitch };
            cs.toleranceCents = 0.0f; // objects are explicit decisions
            if (graph.retuneMs >= 0.0f)
                cs.retuneMs = graph.retuneMs;
            if (graph.kind != dsp::CorrectionTrack::Kind::Note)
                cs.naturalHold = 0.0f;
            if (graph.vibratoKeep >= 0.0f)
                frameVibratoKeep = graph.vibratoKeep;
            graphFormant = std::exp2 (graph.formantSt / 12.0f);
        }
        correctionCurve.setSettings (cs);
        noteMapper.reset();
    }
    else if (voiced && midiTargetOn && numHeld > 0)
    {
        // MIDI Target: the held note closest to the sung pitch — in any octave, or
        // exactly as played ("octava exacta", e.g. to force an octave jump).
        float best = 1.0e9f;
        for (int i = 0; i < numHeld; ++i)
        {
            const int h = held[static_cast<size_t> (i)];
            const int candidate = midiTargetExactOn ? h
                                                    : h + 12 * static_cast<int> (std::lround ((centreMidi - static_cast<float> (h)) / 12.0f));
            if (std::abs (centreMidi - static_cast<float> (candidate)) < best)
            {
                best = std::abs (centreMidi - static_cast<float> (candidate));
                target = { true, true, candidate, noteMapper.position (candidate) }; // incl. per-note detune
            }
        }
    }
    else if (voiced)
        target = noteMapper.map (centreMidi);
    else
        noteMapper.reset();

    const float correction = correctionCurve.process (voiced, centreMidi, target);
    const bool correcting = target.hasTarget && target.correct;
    const float vibratoScale = correcting ? (frameVibratoKeep - 1.0f) * vib.vibratoCents : 0.0f;

    // Artificial vibrato on sustained notes (restarts on every new note).
    const bool noteStarted = voiced && (! wasVoiced || target.note != synthNote);
    synthNote = voiced ? target.note : -1;
    wasVoiced = voiced;
    const auto synth = vibratoSynth.process (voiced, noteStarted);
    outputGainTarget = synth.gain;

    // MIDI note output: the corrected note (target + transposition), or the
    // nearest note when nothing is corrected. A change must last 2 frames
    // (~5 ms) so detector flicker never produces stray notes.
    if (midiOutOn)
    {
        const int wanted = ! voiced ? -1
                                    : (target.hasTarget ? target.note + static_cast<int> (transposeSemitones)
                                                        : static_cast<int> (std::lround (detectedMidi + transposeSemitones)));
        if (wanted == midiOutCandidate)
            ++midiOutCandidateFrames;
        else
        {
            midiOutCandidate = wanted;
            midiOutCandidateFrames = 1;
        }
        if (midiOutCandidate != midiOutNote && (midiOutCandidateFrames >= 2 || midiOutCandidate < 0))
        {
            const int offset = chunkOffsetInBlock + static_cast<int> (e.sampleIndex - chunkStartIndex);
            if (midiOutNote >= 0)
                queueMidiOut (offset, midiOutNote, 0);
            if (midiOutCandidate >= 0 && midiOutCandidate <= 127)
            {
                const int velocity = juce::jlimit (1, 127, static_cast<int> (std::lround (20.0f + (e.rmsDb + 60.0f) * 107.0f / 60.0f)));
                queueMidiOut (offset, midiOutCandidate, velocity);
                midiOutNote = midiOutCandidate;
            }
            else
                midiOutNote = -1;
        }
    }

    const float totalCents = correction + vibratoScale + synth.pitchCents + 100.0f * transposeSemitones;
    const float ratio = std::exp2 (totalCents / 1200.0f);
    const float formant = formantStaticFactor * synth.formantFactor * graphFormant;
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

    if (harmony.anyVoiceEnabled())
    {
        dsp::HarmonyFrame hf;
        hf.voiced = voiced;
        hf.periodSamples = voiced ? static_cast<float> (inputSampleRate / e.frequencyHz) : 0.0f;
        hf.detectedMidi = detectedMidi;
        hf.leadOutputMidi = detectedMidi + totalCents / 100.0f;
        hf.leadNote = target.hasTarget ? target.note + static_cast<int> (transposeSemitones) : -1;
        hf.leadTargetMidi = target.hasTarget ? target.targetMidi + transposeSemitones : hf.leadTargetMidi;
        for (int pc = 0; pc < 12; ++pc)
            hf.detune[static_cast<size_t> (pc)] = noteMapper.getNoteDetune (pc);
        uint16_t mask = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (const auto st = noteMapper.getEffectiveState (pc); st == dsp::NoteState::Active || st == dsp::NoteState::Ignore)
                mask = static_cast<uint16_t> (mask | (1u << pc));
        hf.scaleMask = mask;
        hf.inputRmsDb = e.rmsDb;
        hf.numHeld = numHeld;
        hf.held = held;
        harmony.updateControl (hf);
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

    // Captured audio / time edits (whole block, host-timeline positions). While
    // capturing, the live input is recorded and never replaced.
    if (! bypassed && blockPlaying)
    {
        std::array<float*, dsp::PsolaShifter::kMaxChannels> io {};
        for (int ch = 0; ch < numIn; ++ch)
            io[static_cast<size_t> (ch)] = buffer.getWritePointer (ch);
        if (captureRequested.load (std::memory_order_relaxed))
            audioCapture.push (io.data(), numIn, numSamples, blockHostSample);
        else if (graphModeOn && blockTime != nullptr && std::abs (blockTime->sampleRate - inputSampleRate) < 0.5)
            blockTime->substitute (io.data(), numIn, numSamples, blockHostSample);
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
            chunkHostTime = blockHostTime + static_cast<double> (start) / inputSampleRate;
            chunkOffsetInBlock = start;
            chunkStartIndex = pitchDetector.getInputSamplesSeen();
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

        // Mono -> Stereo: duplicate the lead, then the stereo harmony bus goes on top.
        if (numIn == 1 && numOut >= 2)
            buffer.copyFrom (1, start, buffer, 0, start, n);

        if (! bypassed && harmony.anyVoiceEnabled())
            harmony.process (monoScratch.data(), buffer.getWritePointer (0, start),
                             numOut >= 2 ? buffer.getWritePointer (1, start) : buffer.getWritePointer (0, start), n);
    }

    // Any further output channel is cleared.
    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

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

    readPlayHead();

    diagnostics.lastBlockSize.store (numSamples, std::memory_order_relaxed);
    updateMin (diagnostics.minBlockSize, numSamples);
    updateMax (diagnostics.maxBlockSize, numSamples);

    midiLearn.processMidi (midi); // CC -> learned parameters (applied on the message thread)
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
            midiNotes.noteOn (m.getNoteNumber());
        else if (m.isNoteOff())
            midiNotes.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            midiNotes.allOff();
    }
    midi.clear(); // the output carries only our own notes (no MIDI thru); keeps its storage

    updateDspFromParameters();
    if (! midiOutOn && midiOutNote >= 0)
        queueMidiOut (0, midiOutNote, 0); // switched off while a note was sounding
    runChain (buffer, false);
    flushMidiOut (midi);
    audioBlocksDone.fetch_add (1);
}

void NovaPitchProcessor::queueMidiOut (int offset, int note, int velocity) noexcept
{
    if (velocity == 0 && note == midiOutNote && ! midiOutOn)
        midiOutNote = -1;
    if (numMidiOutEvents < kMaxMidiOutEvents)
        midiOutEvents[static_cast<size_t> (numMidiOutEvents++)] = { std::max (0, offset), note, velocity };
}

void NovaPitchProcessor::flushMidiOut (juce::MidiBuffer& midi) noexcept
{
    for (int i = 0; i < numMidiOutEvents; ++i)
    {
        const auto& ev = midiOutEvents[static_cast<size_t> (i)];
        const auto msg = ev.velocity > 0 ? juce::MidiMessage::noteOn (1, ev.note, static_cast<juce::uint8> (ev.velocity))
                                         : juce::MidiMessage::noteOff (1, ev.note);
        midi.addEvent (msg, ev.offset); // the host's MIDI buffer is pre-sized; 3-byte events
    }
    numMidiOutEvents = 0;
}

void NovaPitchProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() <= 0)
        return;

    midi.clear();
    if (midiOutNote >= 0) // bypass never leaves a hanging note
    {
        queueMidiOut (0, midiOutNote, 0);
        midiOutNote = midiOutCandidate = -1;
    }
    updateDspFromParameters();
    runChain (buffer, true);
    flushMidiOut (midi);
    audioBlocksDone.fetch_add (1);
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
        graphModel.bind (apvts.state); // recompiles and publishes the correction track
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
