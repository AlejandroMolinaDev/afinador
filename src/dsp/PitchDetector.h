#pragma once

#include "dsp/Biquad.h"
#include "dsp/InputProfile.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace novapitch::dsp
{

/** Result of one analysis frame. */
struct PitchEstimate
{
    float frequencyHz = 0.0f;     ///< smoothed (median) f0; 0 when unvoiced
    float rawFrequencyHz = 0.0f;  ///< f0 picked in this frame before smoothing (0 if none)
    float confidence = 0.0f;      ///< 1 - aperiodicity, 0..1
    float rmsDb = -120.0f;        ///< level of the analysis window
    bool voiced = false;          ///< voicing decision (with hysteresis)
    int64_t sampleIndex = 0;      ///< input-rate sample index at the end of the analysed window
};

/** Monophonic f0 detector.

    - YIN difference function computed through FFT cross-correlation (O(N log N))
      + cumulative mean normalised difference + parabolic interpolation.
    - MPM (McLeod) normalised square difference computed from the same terms, used
      as an octave-error validator in Hybrid mode.
    - Analysis runs at 20-40 kHz (decimated, after an anti-alias low-pass): the
      analysis signal is low-passed to <= 8 kHz anyway, so this halves the FFT
      cost at 44.1/48 kHz and keeps it constant at 96/192 kHz.
    - Octave hysteresis against the previous estimate, short median filter.
    - Voicing from aperiodicity + RMS gate + zero-crossing rate, with hysteresis.
    - Input is high-passed and, above ~48 kHz, low-passed and decimated so the
      analysis always runs at 40-48 kHz (constant CPU cost at 96/192 kHz).

    Real-time safe after prepare(): no allocation in process(), setProfile() or
    setSensitivity(). Not thread-safe: call everything from the audio thread. */
class PitchDetector
{
public:
    enum class Algorithm { Yin, Mpm, Hybrid };

    struct Config
    {
        double hopMs = 2.67;      ///< analysis hop (~128 samples at 48 kHz)
        int medianLength = 5;     ///< 1..kMaxMedian frames
        Algorithm algorithm = Algorithm::Hybrid;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        /** Called on the audio thread once per analysis frame. Must be RT-safe. */
        virtual void pitchFrameReady (const PitchEstimate&) noexcept = 0;
    };

    static constexpr int kMaxMedian = 7;

    PitchDetector();
    ~PitchDetector();

    void prepare (double sampleRate, Config config);
    void prepare (double sampleRate) { prepare (sampleRate, Config {}); }
    void reset() noexcept;

    void setProfile (InputProfile) noexcept;
    InputProfile getProfile() const noexcept { return profile; }

    /** 0..100, default 50. Higher accepts noisier / breathier signals. */
    void setSensitivity (float sensitivity0to100) noexcept;

    void setListener (Listener* l) noexcept { listener = l; }

    /** Feeds mono input-rate samples. Returns the number of analysis frames produced. */
    int process (const float* mono, int numSamples) noexcept;

    const PitchEstimate& getEstimate() const noexcept { return current; }

    double getInputSampleRate() const noexcept { return inputRate; }
    double getAnalysisSampleRate() const noexcept { return analysisRate; }
    int getDecimationFactor() const noexcept { return decimation; }
    int getHopSize() const noexcept { return hopSize; }
    /** Analysis frames per second. */
    double getFrameRate() const noexcept { return analysisRate / hopSize; }
    /** Current analysis window length (analysis-rate samples). */
    int getWindowLength() const noexcept { return frameLength; }

    /** Approximate age of a reported estimate, in input samples: half the
        analysis window (its centre) plus the median filter's group delay. */
    int getDetectionLagSamples() const noexcept
    {
        return static_cast<int> ((0.5 * frameLength + 0.5 * (config.medianLength - 1) * hopSize) * decimation);
    }

    // Exposed for unit tests --------------------------------------------------
    float getYinThreshold() const noexcept { return yinThreshold; }
    /** Runs the raw (non-smoothed) analysis on an arbitrary analysis-rate buffer
        of at least getWindowLength() samples (the last window is used). */
    struct RawResult { float yinHz, mpmHz, chosenHz, aperiodicity; };
    RawResult analyseBuffer (const float* analysisRateSamples, int numSamples) noexcept;
    /** d(τ) of the last analysed frame (YIN difference function), for tests. */
    const std::vector<float>& getDifferenceFunction() const noexcept { return diff; }

private:
    void configureForProfile() noexcept;
    void pushAnalysisSample (float x) noexcept;
    void analyseFrame() noexcept;
    void computeDifferenceFunctions (const float* frame) noexcept;
    int findFirstDip (float threshold) const noexcept;
    float pickYin (float& aperiodicityOut) const noexcept;
    float pickMpm (float& clarityOut) const noexcept;
    float aperiodicityAt (float tau) const noexcept;
    float refineLocalMinimum (int tauGuess, int radius) const noexcept;
    RawResult chooseCandidate (float yinTau, float yinAp, float mpmTau, float mpmClarity) noexcept;
    float medianMidi() const noexcept;

    Config config;
    Listener* listener = nullptr;

    double inputRate = 48000.0;
    double analysisRate = 48000.0;
    int decimation = 1;
    int decimationPhase = 0;
    int baseHopSize = 64;  // from Config::hopMs
    int hopSize = 64;      // per profile: longer for very low f0 ranges
    std::array<Biquad, 2> antiAlias;
    Biquad highPass;
    std::array<Biquad, 2> analysisLowPass; // profile dependent, rejects broadband noise

    InputProfile profile = kDefaultInputProfile;
    float sensitivity = 50.0f;
    float yinThreshold = 0.15f;
    float voicingThreshold = 0.25f;
    float gateDb = -55.0f;
    float zcrLimitHz = 3500.0f;

    // Analysis geometry for the current profile (analysis-rate samples)
    int tauMin = 1, tauMax = 2, integrationLength = 2, frameLength = 4, fftIndex = 0;

    // Ring buffer of analysis-rate samples
    std::vector<float> ring;
    int ringMask = 0, ringWrite = 0, samplesSinceHop = 0, samplesInRing = 0;
    int64_t inputSamplesSeen = 0;

    // Zero crossings of the high-passed (not low-passed) signal, counted per hop
    static constexpr int kMaxHopsPerFrame = 128;
    std::array<int, kMaxHopsPerFrame> hopCrossings {};
    int hopCrossingsWrite = 0, crossingsThisHop = 0;
    float previousHp = 0.0f;

    // Work buffers (allocated in prepare)
    std::vector<float> frame, fftA, fftB, diff, cmnd, nsdf;
    std::vector<double> energyPrefix;
    std::vector<std::unique_ptr<juce::dsp::FFT>> ffts; // orders kMinFftOrder..kMaxFftOrder
    static constexpr int kMinFftOrder = 8, kMaxFftOrder = 13;

    // Tracking state
    PitchEstimate current;
    float lastRawTau = 0.0f;     // tau of last voiced raw estimate (0 = none)
    int octaveChallengeFrames = 0;
    std::array<float, kMaxMedian> medianBuf {};
    int medianCount = 0, medianWrite = 0;
    int voicedRun = 0, unvoicedRun = 0;
    bool voicedState = false;
    float frameRmsDb = -120.0f, frameZcrHz = 0.0f;
};

} // namespace novapitch::dsp
