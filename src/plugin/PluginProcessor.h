#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/PitchDetector.h"
#include "plugin/PitchFrameFifo.h"

#include <atomic>
#include <cstdint>

#if HAS_CLAP_JUCE_EXTENSIONS
 #include <clap-juce-extensions/clap-juce-extensions.h>
#endif

namespace novapitch
{

/** Lock-free runtime diagnostics written by the audio thread and read by the
    editor. Used in phase 1 to verify how FL Studio (or any host) drives the
    plugin: block sizes, sample rate, channel layout and incoming MIDI. */
struct HostDiagnostics
{
    std::atomic<double>   sampleRate { 0.0 };
    std::atomic<int>      maxBlockFromPrepare { 0 };
    std::atomic<int>      lastBlockSize { 0 };
    std::atomic<int>      minBlockSize { 0 };   // smallest non-zero block seen
    std::atomic<int>      maxBlockSize { 0 };
    std::atomic<int>      numInputChannels { 0 };
    std::atomic<int>      numOutputChannels { 0 };
    std::atomic<uint32_t> midiEventsReceived { 0 };
    std::atomic<uint32_t> processCalls { 0 };

    void resetBlockStats() noexcept
    {
        lastBlockSize.store (0, std::memory_order_relaxed);
        minBlockSize.store (0, std::memory_order_relaxed);
        maxBlockSize.store (0, std::memory_order_relaxed);
    }
};

class NovaPitchProcessor final : public juce::AudioProcessor,
                                 private dsp::PitchDetector::Listener
                              #if HAS_CLAP_JUCE_EXTENSIONS
                               , public clap_juce_extensions::clap_properties
                              #endif
{
public:
    NovaPitchProcessor();
    ~NovaPitchProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override { return "NovaPitch"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** Human readable plugin format (VST3 / AU / CLAP / Standalone ...). */
    juce::String getFormatName() const;

    /** Latency of the current processing mode, in samples. Phase 1: always 0. */
    int computeLatencySamples() const noexcept { return 0; }

    HostDiagnostics& getDiagnostics() noexcept { return diagnostics; }
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    /** Pitch frames for the UI (consumer: message thread). */
    PitchFrameFifo& getPitchFrameFifo() noexcept { return pitchFifo; }
    /** Analysis frames per second (for UI time bases). */
    double getPitchFrameRate() const noexcept { return pitchFrameRate.load (std::memory_order_relaxed); }

private:
    /** Reports latency to the host only when it actually changes. Must be called
        from prepareToPlay or the message thread, never from processBlock. FL
        Studio uses this value for plugin delay compensation (PDC). */
    void updateReportedLatency();

    // PitchDetector::Listener (audio thread)
    void pitchFrameReady (const dsp::PitchEstimate&) noexcept override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* inputProfileParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;

    HostDiagnostics diagnostics;

    dsp::PitchDetector pitchDetector;
    std::vector<float> monoScratch;
    PitchFrameFifo pitchFifo;
    std::atomic<double> pitchFrameRate { 0.0 };
    double inputSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NovaPitchProcessor)
};

} // namespace novapitch
