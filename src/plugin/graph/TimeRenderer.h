#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "dsp/graph/TimeMap.h"
#include "plugin/graph/AudioCapture.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace novapitch
{

/** Rendered time edits: output-timeline spans of audio that replace the live
    input during playback. Immutable once published to the audio thread. */
struct RenderedTime
{
    struct Span
    {
        int64_t start = 0;              ///< host-timeline sample at `sampleRate`
        juce::AudioBuffer<float> audio; ///< 1 or 2 channels
        int64_t end() const noexcept { return start + audio.getNumSamples(); }
    };

    double sampleRate = 48000.0;
    std::vector<Span> spans; ///< sorted, non-overlapping
    bool audioMissing = false; ///< some edited range had no captured audio (files gone)
    static constexpr double kFadeSeconds = 0.01;

    /** Replaces the parts of `channels` covered by spans (block starting at host
        sample `blockStart`), with short crossfades at the span edges.
        Real-time safe. Returns true if anything was replaced. */
    bool substitute (float* const* channels, int numChannels, int numSamples, int64_t blockStart) const noexcept;
};

/** Renders the time edits from the captured audio with signalsmith-stretch on
    a background thread (pitch untouched, time map applied piecewise). */
class TimeRenderer : private juce::Thread
{
public:
    struct Job
    {
        dsp::TimeMap map;
        std::vector<AudioPass> passes;
        juce::File directory;
        double sampleRate = 48000.0;
        int channels = 2;
        int id = 0;
    };

    /** Margin of unwarped audio around every warped range (crossfades happen in it). */
    static constexpr double kPadSeconds = 0.25;

    TimeRenderer();
    ~TimeRenderer() override;

    /** Message thread: queue a job (replaces any queued one). */
    void request (Job);
    /** Message thread: the newest finished result, if any (`id` = the job's id). */
    std::unique_ptr<RenderedTime> takeResult (int& id);
    bool isBusy() const noexcept { return busy.load(); }

    /** Synchronous render (background thread, tests, offline tools). Returns
        nullptr when the map is the identity; spans whose audio is missing are skipped. */
    static std::unique_ptr<RenderedTime> render (const Job&, bool* audioMissing = nullptr);

    /** Captured audio for [start, start + numSamples) at `sampleRate` (latest pass wins; zeros where none). */
    static juce::AudioBuffer<float> readSource (const std::vector<AudioPass>&, const juce::File& dir, double sampleRate,
                                                int channels, int64_t start, int numSamples, bool* missing = nullptr);

private:
    void run() override;

    std::mutex mutex;
    std::unique_ptr<Job> queued;
    std::unique_ptr<RenderedTime> result;
    int resultId = -1;
    bool hasResult = false;
    std::atomic<bool> busy { false };
};

} // namespace novapitch
